"""D3 real QMYSQL write checks; all fixtures live in a disposable loopback server."""
import argparse
import json
import os
from pathlib import Path
import secrets
import socket
import subprocess
import tempfile
import time
import uuid


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--mysql-home', required=True, type=Path)
    parser.add_argument('--app', required=True, type=Path)
    parser.add_argument('--output', required=True, type=Path)
    parser.add_argument('--desktop-script', type=Path, help='Optional Node CDP checks against the real Qt window')
    args = parser.parse_args()
    args.output.parent.mkdir(parents=True, exist_ok=True)
    evidence = {'checks': []}
    with tempfile.TemporaryDirectory(prefix='speed-sync-write-') as temporary:
        root = Path(temporary)
        data, sock = root / 'data', root / 'mysql.sock'
        with socket.socket() as port_socket:
            port_socket.bind(('127.0.0.1', 0))
            port = port_socket.getsockname()[1]
        init = subprocess.run([str(args.mysql_home / 'bin/mysqld'), '--no-defaults', '--initialize-insecure',
                               f'--basedir={args.mysql_home}', f'--datadir={data}'], capture_output=True, timeout=60)
        if init.returncode:
            raise RuntimeError('Isolated MySQL initialization failed; no existing server was touched')
        with (root / 'server.log').open('wb') as log:
            server = subprocess.Popen([str(args.mysql_home / 'bin/mysqld'), '--no-defaults',
                                       f'--basedir={args.mysql_home}', f'--datadir={data}', f'--socket={sock}',
                                       f'--port={port}', '--bind-address=127.0.0.1', '--mysqlx=OFF',
                                       f'--pid-file={root / "server.pid"}'], stdout=log, stderr=log)
            try:
                client = [str(args.mysql_home / 'bin/mysql'), '--no-defaults', '--protocol=socket',
                          f'--socket={sock}', '-uroot', '--batch', '--skip-column-names', '--raw']
                for _ in range(100):
                    if server.poll() is not None:
                        raise RuntimeError('Isolated server exited')
                    if subprocess.run(client, input='SELECT 1;', text=True, capture_output=True, timeout=3).returncode == 0:
                        break
                    time.sleep(.1)
                else:
                    raise RuntimeError('Isolated server startup timed out')
                secret = secrets.token_urlsafe(24)
                env = os.environ | {'MYSQL_PWD': secret}

                def sql(statement, authenticated=True):
                    result = subprocess.run(client, input=statement, text=True, capture_output=True,
                                            env=env if authenticated else os.environ, timeout=15)
                    if result.returncode:
                        raise RuntimeError('Fixture SQL failed: ' + result.stderr.replace(secret, '[redacted]'))
                    return result.stdout

                sql(f"ALTER USER 'root'@'localhost' IDENTIFIED BY '{secret}';", False)
                evidence['mysql'] = sql('SELECT VERSION();').strip()
                sql("CREATE DATABASE d3_left; CREATE DATABASE d3_right;")
                base = dict(name='D3 isolated fixture', host='127.0.0.1', port=port, user='root',
                            password=secret, database='', tls='preferred', ca='', timeout=3)

                def invoke(payload):
                    proc = subprocess.run([str(args.app.resolve()), '--schema'], input=json.dumps(payload),
                                          text=True, capture_output=True, timeout=40)
                    assert secret not in proc.stdout + proc.stderr, 'Credential leak'
                    assert proc.returncode == 0, 'Native schema process failed'
                    return json.loads(proc.stdout)

                def compare(left, right):
                    result = invoke({'operation': 'compare', 'left': base, 'right': base,
                                     'args': {'left': {'database': 'd3_left', 'table': left},
                                              'right': {'database': 'd3_right', 'table': right}}})
                    assert result.get('ok'), result
                    return result['comparison']

                def plan(left, right, selected=None, direction='left-to-right', align=True):
                    comparison = compare(left, right)
                    return invoke({'operation': 'plan-sync', 'left': base, 'right': base,
                                   'expected': {side: comparison[side] for side in ['left', 'right']},
                                   'args': {'direction': direction, 'selected': selected or [], 'alignAll': align}})

                def record(name, result):
                    evidence['checks'].append({'name': name, 'result': 'passed', 'evidence': result})

                def execute(left, right, selected=None, direction='left-to-right', align=True):
                    result = plan(left, right, selected, direction, align)
                    assert result.get('ok'), {'result': result, 'comparison': compare(left, right)}
                    current = result['plan']
                    assert current['steps'], 'Expected real DDL steps'
                    checked = invoke({'operation': 'sync-check', 'left': base, 'right': base,
                                      'expected': {side: current[side] for side in ['left', 'right']}})
                    assert checked.get('ok'), checked
                    target = current['right' if direction == 'left-to-right' else 'left']
                    results = []
                    for step in current['steps']:
                        performed = invoke({'operation': 'sync-step', 'connection': base, 'target': target, 'step': step})
                        assert performed.get('ok'), performed
                        results.append(performed)
                    reviewed = invoke({'operation': 'sync-read', 'left': base, 'right': base,
                                       'expected': {side: current[side] for side in ['left', 'right']}})
                    assert reviewed.get('ok'), reviewed
                    record(left + ':' + direction, {'plan': current, 'steps': results, 'review': reviewed})
                    return compare(left, right)

                sql("""CREATE TABLE d3_left.attributes (
                    id BIGINT NOT NULL AUTO_INCREMENT PRIMARY KEY,
                    label VARCHAR(90) CHARACTER SET utf8mb4 COLLATE utf8mb4_bin NOT NULL DEFAULT 'a;b' COMMENT 'new',
                    added DECIMAL(16,4) DEFAULT 1.2500, hidden INT INVISIBLE DEFAULT 7,
                    UNIQUE KEY uq_label(label(20)), KEY ix_added(added DESC) INVISIBLE COMMENT 'index')
                    DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_bin ROW_FORMAT=DYNAMIC COMMENT='new table';
                    CREATE TABLE d3_right.renamed (
                    id BIGINT NOT NULL AUTO_INCREMENT PRIMARY KEY, obsolete INT,
                    label VARCHAR(30) NULL DEFAULT NULL, KEY ix_old(label))
                    DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_general_ci ROW_FORMAT=COMPACT COMMENT='old table';""")
                assert execute('attributes', 'renamed')['status'] == 'same'
                assert 'renamed' in sql('SHOW CREATE TABLE d3_right.renamed;')
                sql("""CREATE TABLE d3_left.features (id INT NOT NULL PRIMARY KEY, note TEXT,
                    p POINT NOT NULL, q POINT NOT NULL, FULLTEXT KEY ft(note), SPATIAL KEY sp(p));
                    CREATE TABLE d3_right.features (id INT NOT NULL, note TEXT,
                    p POINT NOT NULL, q POINT NOT NULL, KEY ft(note(20)), SPATIAL KEY sp(q));""")
                assert execute('features', 'features')['status'] == 'same'
                sql("CREATE TABLE d3_left.reverse_pair (id INT PRIMARY KEY, value INT); "
                    "CREATE TABLE d3_right.reverse_pair (id INT PRIMARY KEY, value BIGINT DEFAULT 9);")
                assert execute('reverse_pair', 'reverse_pair', direction='right-to-left')['status'] == 'same'
                assert execute('attributes', 'created_target')['status'] == 'same'
                denied = plan('absent_source', 'created_target')
                assert not denied.get('ok'), denied
                record('source_missing_no_drop', denied)
                sql("CREATE TABLE d3_left.triggered (id INT, value INT); CREATE TABLE d3_right.triggered LIKE d3_left.triggered;")
                sql("""SET sql_mode='STRICT_TRANS_TABLES,NO_ENGINE_SUBSTITUTION';
                    SET NAMES utf8mb4 COLLATE utf8mb4_bin;
                    DELIMITER $$
                    CREATE DEFINER='root'@'localhost' TRIGGER d3_left.semicolon_trigger BEFORE INSERT ON d3_left.triggered
                    FOR EACH ROW BEGIN SET NEW.value = 5; SET NEW.value = NEW.value + 2; END$$
                    DELIMITER ;
                    """)
                assert execute('triggered', 'triggered')['status'] == 'same'
                sql('INSERT INTO d3_right.triggered VALUES(1,0);')
                assert sql('SELECT value FROM d3_right.triggered;').strip() == '7'
                context = sql("SELECT DEFINER,SQL_MODE,CHARACTER_SET_CLIENT,COLLATION_CONNECTION FROM information_schema.TRIGGERS "
                              "WHERE TRIGGER_NAME='semicolon_trigger' ORDER BY TRIGGER_SCHEMA;").splitlines()
                assert len(context) == 2 and context[0] == context[1], context
                record('trigger_semicolon_and_context', {'context': context, 'actual_value': 7})
                sql('DROP TRIGGER d3_left.semicolon_trigger; CREATE TRIGGER d3_left.semicolon_trigger BEFORE INSERT ON d3_left.triggered FOR EACH ROW SET NEW.value=11;')
                assert execute('triggered', 'triggered')['status'] == 'same'
                sql('INSERT INTO d3_right.triggered VALUES(2,0);')
                assert sql('SELECT value FROM d3_right.triggered WHERE id=2;').strip() == '11'
                sql('DROP TRIGGER d3_left.semicolon_trigger;')
                assert execute('triggered', 'triggered')['status'] == 'same'
                sql("CREATE TABLE d3_left.drift (id INT, extra INT); CREATE TABLE d3_right.drift (id INT);")
                drift = plan('drift', 'drift')['plan']
                sql('ALTER TABLE d3_left.drift ADD external_change INT;')
                before = sql('SHOW CREATE TABLE d3_right.drift;')
                checked = invoke({'operation': 'sync-check', 'left': base, 'right': base,
                                  'expected': {side: drift[side] for side in ['left', 'right']}})
                assert not checked.get('ok'), checked
                assert sql('SHOW CREATE TABLE d3_right.drift;') == before
                record('drift_rejected_target_unchanged', checked)
                sql("""CREATE TABLE d3_left.parent (id BIGINT PRIMARY KEY);
                    CREATE TABLE d3_right.parent (id INT PRIMARY KEY);
                    CREATE TABLE d3_right.external_child (id INT, CONSTRAINT incoming_fk FOREIGN KEY(id) REFERENCES d3_right.parent(id));
                    CREATE TABLE d3_left.unsupported (id INT, calculated INT GENERATED ALWAYS AS (id+1) STORED);
                    CREATE TABLE d3_right.unsupported (id INT);""")
                external_before = sql('SHOW CREATE TABLE d3_right.external_child;')
                for name in ['parent', 'unsupported']:
                    rejected = plan(name, name)
                    assert not rejected.get('ok'), rejected
                    record(name + '_blocked', rejected)
                assert external_before == sql('SHOW CREATE TABLE d3_right.external_child;')
                sql('CREATE TABLE d3_left.partial_choice (id INT, chosen INT DEFAULT 3, unchosen INT); '
                    'CREATE TABLE d3_right.partial_choice (id INT);')
                partial = execute('partial_choice', 'partial_choice', [{'category': 'columns', 'name': 'chosen'}], align=False)
                assert partial['status'] == 'different'
                target_ddl = sql('SHOW CREATE TABLE d3_right.partial_choice;')
                assert '`chosen`' in target_ddl and '`unchosen`' not in target_ddl
                record('partial_selection_preserves_unselected_difference', {'target_ddl': target_ddl})
                if args.desktop_script:
                    sql("CREATE TABLE d3_left.ui_sample (id INT PRIMARY KEY, added INT DEFAULT 7); "
                        "CREATE TABLE d3_right.ui_sample (id INT PRIMARY KEY); "
                        "CREATE USER 'd3_ui'@'localhost' IDENTIFIED BY ''; GRANT ALL ON *.* TO 'd3_ui'@'localhost';")
                    ui_state = root / 'ui-state'
                    ui_state.mkdir()
                    left_id, right_id = str(uuid.uuid4()), str(uuid.uuid4())
                    connections = []
                    for identifier, name in [(left_id, 'D3 Left fixture'), (right_id, 'D3 Right fixture')]:
                        connection = base | {'id': identifier, 'name': name, 'user': 'd3_ui', 'remember': False}
                        connection.pop('password')
                        connections.append(connection)
                    state = {'connections': connections, 'left': left_id, 'right': right_id,
                             'settings': {'theme': 'light', 'density': 'standard', 'timeout': 3},
                             'workspaces': {f'{left_id}:{right_id}': {'left': {'database': 'd3_left', 'table': 'ui_sample'},
                                                                    'right': {'database': 'd3_right', 'table': 'ui_sample'}, 'width': 240}}}
                    (ui_state / 'connections.json').write_text(json.dumps(state) + '\n')
                    with socket.socket() as debug_socket:
                        debug_socket.bind(('127.0.0.1', 0))
                        debug_port = debug_socket.getsockname()[1]
                    with (root / 'desktop.log').open('wb') as desktop_log:
                        desktop = subprocess.Popen([str(args.app.resolve())], stdout=desktop_log, stderr=desktop_log,
                            env=os.environ | {'SPEED_SYNC_DATA_DIR': str(ui_state), 'QTWEBENGINE_REMOTE_DEBUGGING': f'127.0.0.1:{debug_port}'})
                        try:
                            subprocess.run(['node', str(args.desktop_script.resolve()), '--port', str(debug_port),
                                            '--output', str(args.output.parent / 'd3-desktop.json')], check=True, timeout=180)
                        finally:
                            desktop.terminate()
                            try:
                                desktop.wait(timeout=10)
                            except subprocess.TimeoutExpired:
                                desktop.kill()
                                desktop.wait()
                    assert compare('ui_sample', 'ui_sample')['status'] == 'same'
                    record('desktop_real_target_verified', {'status': 'same'})
                print(f"{len(evidence['checks'])} real sync checks passed; fixture removed on exit")
            except Exception as error:
                evidence['error'] = str(error)
                raise
            finally:
                server.terminate()
                try:
                    server.wait(timeout=15)
                except subprocess.TimeoutExpired:
                    server.kill()
                    server.wait()
                args.output.write_text(json.dumps(evidence, ensure_ascii=False, indent=2) + '\n')


if __name__ == '__main__':
    main()
