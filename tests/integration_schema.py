"""D2 real QMYSQL checks; all fixtures live in a disposable loopback server."""
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
    with tempfile.TemporaryDirectory(prefix='speed-sync-schema-') as temporary:
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
                sql("CREATE DATABASE d2_left; CREATE DATABASE d2_right;")
                for database, table, different in [('d2_left', 'sample', False), ('d2_right', 'renamed', False),
                                                    ('d2_right', 'different', True)]:
                    sql(f"""CREATE TABLE {database}.{table} (
                        id BIGINT NOT NULL AUTO_INCREMENT,
                        parent_id BIGINT NULL,
                        amount DECIMAL(30,10) NOT NULL DEFAULT {'2.0000000001' if different else '1.0000000001'},
                        label VARCHAR({80 if different else 60}) CHARACTER SET utf8mb4 COLLATE utf8mb4_bin DEFAULT 'a  b' COMMENT 'two  spaces',
                        generated_amount DECIMAL(31,10) GENERATED ALWAYS AS (amount + 1) STORED,
                        hidden_value INT INVISIBLE DEFAULT 7,
                        PRIMARY KEY (id), UNIQUE KEY uq_label (label(20)),
                        KEY ix_amount (amount {'DESC' if different else 'ASC'}) INVISIBLE,
                        KEY ix_expression ((lower(label))),
                        CONSTRAINT ck_amount{'_diff' if different else ''} CHECK (amount >= {1 if different else 0}),
                        CONSTRAINT fk_parent{'_diff' if different else ''} FOREIGN KEY (parent_id) REFERENCES {database}.{table}(id) ON DELETE {'CASCADE' if different else 'SET NULL'}
                    ) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_bin COMMENT='{'different' if different else 'same'}';
                    CREATE TRIGGER {database}.tr_{table} BEFORE INSERT ON {database}.{table}
                      FOR EACH ROW SET NEW.amount = NEW.amount + {2 if different else 1};""")
                # Trigger names are object identity: align the same-structure pair across databases.
                sql('DROP TRIGGER d2_left.tr_sample; DROP TRIGGER d2_right.tr_renamed; '
                    'CREATE TRIGGER d2_left.tr_same BEFORE INSERT ON d2_left.sample FOR EACH ROW SET NEW.amount = NEW.amount + 1; '
                    'CREATE TRIGGER d2_right.tr_same BEFORE INSERT ON d2_right.renamed FOR EACH ROW SET NEW.amount = NEW.amount + 1; '
                    'ALTER TABLE d2_right.renamed AUTO_INCREMENT=900; '
                    'CREATE TABLE d2_left.partitioned (id INT NOT NULL PRIMARY KEY) PARTITION BY HASH(id) PARTITIONS 2; '
                    'CREATE TABLE d2_right.partitioned LIKE d2_left.partitioned; '
                    'CREATE VIEW d2_left.sample_view AS SELECT id FROM d2_left.sample; '
                    'CREATE TABLE d2_left.`odd``table` (`a``b` INT DEFAULT 1); '
                    'CREATE TABLE d2_right.`odd``table` LIKE d2_left.`odd``table`;')
                for database, different in [('d2_left', False), ('d2_right', True)]:
                    sql(f"""CREATE TABLE {database}.index_features (
                        id INT PRIMARY KEY, description TEXT,
                        location POINT NOT NULL, other_location POINT NOT NULL,
                        {'KEY ix_text (description(20))' if different else 'FULLTEXT KEY ix_text (description)'},
                        SPATIAL KEY ix_space ({'other_location' if different else 'location'})
                    ) ENGINE=InnoDB;
                    CREATE TABLE {database}.long_table (
                        id INT PRIMARY KEY COMMENT '{'id-description-' * 55}',
                        note TEXT COMMENT '{'note-description-' * 55}'
                    ) COMMENT='{'right' if different else 'left'} long definition fixture';
                    CREATE TRIGGER {database}.long_trigger BEFORE INSERT ON {database}.long_table
                      FOR EACH ROW SET NEW.note = CONCAT('{'right' if different else 'left'}-',
                        '{'long-trigger-body-segment-' * 65}', COALESCE(NEW.note, ''));
                    """)
                sql(f"""CREATE USER 'd2_reader'@'localhost' IDENTIFIED BY '{secret}';
                    GRANT SELECT, TRIGGER ON *.* TO 'd2_reader'@'localhost';
                    CREATE USER 'd2_no_trigger'@'localhost' IDENTIFIED BY '{secret}';
                    GRANT SELECT ON d2_left.* TO 'd2_no_trigger'@'localhost';
                    GRANT SELECT ON d2_right.* TO 'd2_no_trigger'@'localhost';
                    CREATE USER 'd2_column'@'localhost' IDENTIFIED BY '{secret}';
                    GRANT SELECT(id) ON d2_left.sample TO 'd2_column'@'localhost';
                    GRANT SELECT(id) ON d2_right.renamed TO 'd2_column'@'localhost';""")
                base = dict(name='D2 isolated fixture', host='127.0.0.1', port=port, user='d2_reader',
                            password=secret, database='', tls='preferred', ca='', timeout=3)
                tables = [('d2_left', 'sample'), ('d2_right', 'renamed'), ('d2_right', 'different'),
                          ('d2_left', 'partitioned'), ('d2_right', 'partitioned'),
                          ('d2_left', 'odd`table'), ('d2_right', 'odd`table'),
                          ('d2_left', 'index_features'), ('d2_right', 'index_features'),
                          ('d2_left', 'long_table'), ('d2_right', 'long_table')]

                def definitions():
                    return [sql(f'SHOW CREATE TABLE `{db}`.`{table.replace("`", "``")}`;') for db, table in tables] + [
                        sql('SHOW CREATE TRIGGER d2_left.tr_same;'), sql('SHOW CREATE TRIGGER d2_right.tr_same;'),
                        sql('SHOW CREATE TRIGGER d2_right.tr_different;'),
                        sql('SHOW CREATE TRIGGER d2_left.long_trigger;'), sql('SHOW CREATE TRIGGER d2_right.long_trigger;')]

                before = definitions()
                sql("SET GLOBAL log_output='TABLE'; SET GLOBAL general_log=ON;")

                def invoke(name, payload):
                    proc = subprocess.run([str(args.app.resolve()), '--schema'], input=json.dumps(payload),
                                          text=True, capture_output=True, timeout=25)
                    assert secret not in proc.stdout + proc.stderr, f'{name}: credential leak'
                    assert proc.returncode == 0, f'{name}: process failed: {proc.stderr}'
                    return json.loads(proc.stdout)

                def check(name, payload, predicate):
                    result = invoke(name, payload)
                    passed = bool(predicate(result))
                    evidence['checks'].append({'name': name, 'result': 'passed' if passed else 'failed', 'evidence': result})
                    assert passed, f'{name}: unexpected result (see evidence file)'
                    return result

                def schema(action, database='', table='', connection=None):
                    return {'operation': 'schema', 'args': dict(action=action, side='left', database=database, table=table),
                            'left': connection or base}

                def compare(left='sample', right='renamed', connection=None):
                    return {'operation': 'compare', 'args': {'left': {'database': 'd2_left', 'table': left},
                                                            'right': {'database': 'd2_right', 'table': right}},
                            'left': connection or base, 'right': connection or base}

                def complete_same(result):
                    return result.get('ok') and result.get('comparison', {}).get('complete') is True and result['comparison']['status'] == 'same'

                def incomplete(result):
                    comparison = result.get('comparison', {})
                    return result.get('ok') and comparison.get('complete') is False and comparison.get('status') != 'same'

                check('databases', schema('databases'), lambda r: r.get('ok') and 'd2_left' in json.dumps(r['items']) and 'd2_right' in json.dumps(r['items']))
                check('tables_and_view', schema('tables', 'd2_left'), lambda r: r.get('ok') and 'sample_view' in json.dumps(r['items']) and 'sample' in json.dumps(r['items']))
                check('snapshot', schema('snapshot', 'd2_left', 'sample'), lambda r: r.get('ok') and 'CREATE TABLE' in json.dumps(r))
                check('same_across_database_table_and_auto_increment_counter', compare(), complete_same)
                check('field_index_constraint_trigger_table_differences', compare(right='different'),
                      lambda r: r.get('ok') and r['comparison']['complete'] and r['comparison']['status'] == 'different'
                      and {row['category'] for row in r['comparison']['rows'] if row['status'] != 'same'}
                      >= {'columns', 'indexes', 'constraints', 'triggers', 'table'})
                check('quoted_identifiers', compare('odd`table', 'odd`table'), complete_same)
                check('partition_is_incomplete', compare('partitioned', 'partitioned'), incomplete)
                check('no_trigger_privilege_is_incomplete', compare(connection=base | {'user': 'd2_no_trigger'}), incomplete)
                check('column_privilege_is_incomplete', compare(connection=base | {'user': 'd2_column'}), incomplete)
                check('one_missing_table', compare(right='does_not_exist'), lambda r: r.get('ok') and r['comparison']['status'] != 'same')
                check('both_missing_tables', compare('does_not_exist', 'does_not_exist'), incomplete)
                check('view_is_unsupported', compare(left='sample_view'), incomplete)
                check('unseen_table_is_not_same', compare(right='does_not_exist', connection=base | {'user': 'd2_column'}), incomplete)
                def index_features(result):
                    if not result.get('ok') or not result['comparison']['complete']:
                        return False
                    rows = {row['name']: row for row in result['comparison']['rows'] if row['category'] == 'indexes'}
                    text_index, spatial_index = rows['ix_text'], rows['ix_space']
                    text_left = text_index['left']['properties']['parts'][0]
                    text_right = text_index['right']['properties']['parts'][0]
                    spatial_left = spatial_index['left']['properties']['parts'][0]
                    spatial_right = spatial_index['right']['properties']['parts'][0]
                    return (text_index['status'] == spatial_index['status'] == 'different'
                            and 'parts' in text_index['changed'] and 'parts' in spatial_index['changed']
                            and text_left['method'] == 'FULLTEXT' and text_right['method'] == 'BTREE'
                            and text_left['columnName'] == text_right['columnName'] == 'description'
                            and text_left['prefixLength'] is None and text_right['prefixLength'] == '20'
                            and spatial_left['method'] == spatial_right['method'] == 'SPATIAL'
                            and spatial_left['columnName'] == 'location' and spatial_right['columnName'] == 'other_location')

                check('fulltext_and_spatial_structured_differences', compare('index_features', 'index_features'), index_features)
                check('long_table_and_trigger_definitions', compare('long_table', 'long_table'),
                      lambda r: r.get('ok') and r['comparison']['complete']
                      and all(len(r['comparison'][side]['ddl']) > 1500 for side in ['left', 'right'])
                      and any(row['name'] == 'long_trigger' and row['status'] == 'different'
                              and 'body' in row['changed']
                              and all(len(row[side]['properties']['body']) > 1000 and len(row[side]['ddl']) > 1000
                                      for side in ['left', 'right']) for row in r['comparison']['rows']))
                sql('SET GLOBAL general_log=OFF;')
                queries = [json.loads(line) for line in sql("SELECT JSON_QUOTE(CONVERT(argument USING utf8mb4)) FROM mysql.general_log WHERE command_type IN ('Query','Execute','Prepare') "
                              "AND user_host LIKE 'd2_%' ORDER BY event_time;").splitlines()]
                assert queries, 'No application queries captured'
                # Metadata reads and connector session setup only; reject business-row reads and mutations.
                for query in queries:
                    normalized = query.strip().upper()
                    assert normalized.startswith(('SELECT ', 'SHOW ', 'SET NAMES ', 'SET CHARACTER_SET_RESULTS', 'SET AUTOCOMMIT', 'SET TIME_ZONE ')), f'Unexpected application SQL: {query}'
                    if normalized.startswith('SELECT ') and ' FROM ' in normalized:
                        assert 'INFORMATION_SCHEMA' in normalized or 'PERFORMANCE_SCHEMA' in normalized, f'Business-row read: {query}'
                assert definitions() == before, 'Application changed fixture structure'
                evidence['checks'].append({'name': 'application_is_read_only', 'result': 'passed',
                                           'query_count': len(queries), 'queries': sorted(set(queries)), 'definitions_unchanged': True})
                if args.desktop_script:
                    sql("CREATE USER 'd2_ui'@'localhost' IDENTIFIED BY ''; "
                        "GRANT SELECT, TRIGGER ON d2_left.* TO 'd2_ui'@'localhost'; "
                        "GRANT SELECT, TRIGGER ON d2_right.* TO 'd2_ui'@'localhost';")
                    ui_state = root / 'ui-state'
                    ui_state.mkdir()
                    left_id, right_id = str(uuid.uuid4()), str(uuid.uuid4())
                    connections = []
                    for identifier, name in [(left_id, 'D2 Left fixture'), (right_id, 'D2 Right fixture')]:
                        connection = base | {'id': identifier, 'name': name, 'user': 'd2_ui', 'remember': False, 'timeout': 2}
                        connection.pop('password')
                        connections.append(connection)
                    state = {'connections': connections, 'left': left_id, 'right': right_id,
                             'settings': {'theme': 'light', 'density': 'standard', 'timeout': 2},
                             'workspaces': {f'{left_id}:{right_id}': {'left': {'database': 'd2_left', 'table': 'sample'},
                                                                    'right': {'database': 'd2_right', 'table': 'different'},
                                                                    'width': 240}}}
                    (ui_state / 'connections.json').write_text(json.dumps(state) + '\n')
                    with socket.socket() as debug_socket:
                        debug_socket.bind(('127.0.0.1', 0))
                        debug_port = debug_socket.getsockname()[1]
                    desktop_env = os.environ | {'SPEED_SYNC_DATA_DIR': str(ui_state),
                                                'QTWEBENGINE_REMOTE_DEBUGGING': f'127.0.0.1:{debug_port}'}
                    with (root / 'desktop.log').open('wb') as desktop_log:
                        desktop = subprocess.Popen([str(args.app.resolve())], env=desktop_env,
                                                   stdout=desktop_log, stderr=desktop_log)
                        try:
                            subprocess.run(['node', str(args.desktop_script.resolve()), '--port', str(debug_port),
                                            '--output', str(args.output.parent / 'd2-desktop.json')], check=True, timeout=120)
                        finally:
                            desktop.terminate()
                            try:
                                desktop.wait(timeout=10)
                            except subprocess.TimeoutExpired:
                                desktop.kill()
                                desktop.wait()
                    assert definitions() == before, 'Desktop application changed fixture structure'
                print(f"{len(evidence['checks'])} real schema checks passed; fixture removed on exit")
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
