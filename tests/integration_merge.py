#!/usr/bin/env python3
"""D5 writes against a disposable loopback MySQL; never uses saved connections."""
import argparse
import json
import os
from pathlib import Path
import secrets
import subprocess
import tempfile
import time
import uuid

from integration_data import free_port


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--mysql-home', required=True, type=Path)
    parser.add_argument('--app', required=True, type=Path)
    parser.add_argument('--output', required=True, type=Path)
    parser.add_argument('--desktop-script', type=Path, default=Path(__file__).with_name('desktop_merge.mjs'))
    parser.add_argument('--skip-desktop', action='store_true')
    args = parser.parse_args()
    args.output.parent.mkdir(parents=True, exist_ok=True)
    evidence = {'result': 'running', 'checks': []}
    with tempfile.TemporaryDirectory(prefix='speed-sync-merge-') as temporary:
        root = Path(temporary)
        data, sock, port = root / 'data', root / 'mysql.sock', free_port()
        initialized = subprocess.run([str(args.mysql_home / 'bin/mysqld'), '--no-defaults', '--initialize-insecure',
                                     f'--basedir={args.mysql_home}', f'--datadir={data}'], capture_output=True, timeout=60)
        if initialized.returncode:
            raise RuntimeError('Isolated MySQL initialization failed; no existing server was touched')
        with (root / 'server.log').open('wb') as log:
            server = subprocess.Popen([str(args.mysql_home / 'bin/mysqld'), '--no-defaults',
                f'--basedir={args.mysql_home}', f'--datadir={data}', f'--socket={sock}', f'--port={port}',
                '--bind-address=127.0.0.1', '--mysqlx=OFF', f'--pid-file={root / "server.pid"}'], stdout=log, stderr=log)
            desktop = None
            secret = ''
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

                def sql(statement, authenticated=True):
                    result = subprocess.run(client, input=statement, text=True, capture_output=True,
                        env=os.environ | {'MYSQL_PWD': secret} if authenticated else os.environ, timeout=30)
                    if result.returncode:
                        raise RuntimeError(result.stderr.replace(secret, '[redacted]'))
                    return result.stdout.strip()

                sql(f"ALTER USER 'root'@'localhost' IDENTIFIED BY '{secret}';", False)
                sql('CREATE DATABASE d5_left; CREATE DATABASE d5_right;')
                evidence['mysql'] = sql('SELECT VERSION();')
                base = dict(name='D5 isolated fixture', host='127.0.0.1', port=port, user='root',
                            password=secret, database='', tls='preferred', ca='', timeout=3)

                def invoke(operation, path, command_args, **extra):
                    payload = dict(operation=operation, left=base, right=base, path=str(path), args=command_args) | extra
                    proc = subprocess.run([str(args.app.resolve()), '--data'], input=json.dumps(payload),
                                          text=True, capture_output=True, timeout=90)
                    assert secret not in proc.stdout + proc.stderr, 'Credential leak'
                    assert proc.returncode == 0, 'Native data process failed: ' + proc.stderr
                    return json.loads(proc.stdout)

                def record(name, **details):
                    evidence['checks'].append({'name': name, 'result': 'passed', **details})

                def scan(table, fields=None, filters=None, key=None, mode='compare'):
                    path = root / str(uuid.uuid4())
                    path.mkdir()
                    task_id = str(uuid.uuid4())
                    context = {'left': {'database': 'd5_left', 'table': table},
                               'right': {'database': 'd5_right', 'table': table}}
                    prepared = invoke('data-prepare', path, context)
                    assert prepared.get('ok'), prepared
                    preparation = prepared['preparation']
                    result = invoke('data-start', path, context | {
                        'key': preparation['defaultKey'] if key is None else key,
                        'fields': preparation['defaultFields'] if fields is None else fields,
                        'filters': filters or [], 'mode': mode}, taskId=task_id)
                    assert result.get('ok'), result
                    return path, task_id

                def plan(table, direction='left-to-right', mode='merge', **options):
                    path, task_id = scan(table, **options)
                    result = invoke('merge-plan', path, {'taskId': task_id, 'direction': direction, 'mode': mode})
                    return path, result

                def batch(path, current, offset=0):
                    return invoke('merge-batch', path, {'planId': current['id'], 'offset': offset, 'limit': 256})

                def execute(table, direction='left-to-right', mode='merge', **options):
                    before = sql(f'CHECKSUM TABLE d5_left.`{table}`,d5_right.`{table}`;')
                    path, result = plan(table, direction, mode, **options)
                    assert result.get('ok'), result
                    current = result['plan']
                    assert before == sql(f'CHECKSUM TABLE d5_left.`{table}`,d5_right.`{table}`;'), 'Preview wrote data'
                    assert current['counts']['delete'] == 0
                    total = 0
                    for offset in range(0, current['total'], 256):
                        performed = batch(path, current, offset)
                        assert performed.get('ok') and performed['status'] == 'passed', performed
                        total += performed['committed']
                    assert total == current['total'], (total, current)
                    return current

                def pair(table, definition='id INT PRIMARY KEY, value INT, untouched INT DEFAULT 9', engine='InnoDB'):
                    sql(f'CREATE TABLE d5_left.`{table}` ({definition}) ENGINE={engine}; '
                        f'CREATE TABLE d5_right.`{table}` LIKE d5_left.`{table}`;')

                for direction in ['left-to-right', 'right-to-left']:
                    for mode in ['fill', 'merge']:
                        table = direction.replace('-', '_') + '_' + mode
                        source, target_db = ('d5_left', 'd5_right') if direction == 'left-to-right' else ('d5_right', 'd5_left')
                        pair(table)
                        sql(f'INSERT INTO {source}.{table} VALUES(1,10,100),(2,20,200); '
                            f'INSERT INTO {target_db}.{table} VALUES(1,11,101),(3,30,300);')
                        current = execute(table, direction, mode, fields=['id', 'value'])
                        assert current['counts'] == {'add': 1, 'modify': int(mode == 'merge'), 'delete': 0}, current
                        assert sql(f'SELECT id,value,untouched FROM {target_db}.{table} ORDER BY id;') == (
                            f'1\t{10 if mode == "merge" else 11}\t101\n2\t20\t9\n3\t30\t300')
                        assert sql(f'SELECT id,value,untouched FROM {source}.{table} ORDER BY id;') == '1\t10\t100\n2\t20\t200'
                        record(direction + ' ' + mode + ': selected fields, keys and target-only rows preserved')

                pair('auto_zero', 'id INT PRIMARY KEY AUTO_INCREMENT,value INT')
                sql("SET SESSION sql_mode='NO_AUTO_VALUE_ON_ZERO'; INSERT INTO d5_left.auto_zero VALUES(0,7),(12,8);")
                execute('auto_zero', fields=['value'])
                assert sql('SELECT id,value FROM d5_right.auto_zero ORDER BY id;') == '0\t7\n12\t8'
                record('unselected matching key and AUTO_INCREMENT zero retained exactly')

                pair('on_update', 'id INT PRIMARY KEY,value INT,touched TIMESTAMP(6) DEFAULT CURRENT_TIMESTAMP(6) ON UPDATE CURRENT_TIMESTAMP(6)')
                sql("INSERT INTO d5_left.on_update VALUES(1,10,'2020-01-01 00:00:00'); "
                    "INSERT INTO d5_right.on_update VALUES(1,11,'2021-01-01 00:00:00');")
                execute('on_update', fields=['value'])
                assert sql('SELECT value,touched FROM d5_right.on_update;') == '10\t2021-01-01 00:00:00.000000'
                record('unselected ON UPDATE timestamp preserved explicitly')

                pair('collated_key', 'id VARCHAR(10) COLLATE utf8mb4_general_ci PRIMARY KEY,value INT')
                sql("INSERT INTO d5_left.collated_key VALUES('A',10); INSERT INTO d5_right.collated_key VALUES('a',11);")
                execute('collated_key')
                assert sql('SELECT HEX(id),value FROM d5_right.collated_key;') == '61\t10'
                record('MySQL equivalent key matched while target key bytes remain unchanged')

                pair('exact_values', '''id BIGINT UNSIGNED PRIMARY KEY, amount DECIMAL(40,15), note LONGTEXT,
                     raw_value LONGBLOB, stamp DATETIME(6), ts TIMESTAMP(6) NULL, tm TIME(6), doc JSON,
                     bits BIT(9), flt FLOAT, dbl DOUBLE''')
                sql("""SET time_zone='+00:00'; INSERT INTO d5_left.exact_values VALUES
                    (18446744073709551615,12345678901234567890123.123456789012345,
                     CONCAT(REPEAT('长😀',12000),CHAR(0),'尾'),X'00FF0010','2026-09-15 08:09:10.123456',
                     '2026-09-15 08:09:10.654321','-12:34:56.123456',JSON_OBJECT('large',9007199254740993),
                     b'100000001',1.0000001192092896,1.234567890123456),
                    (9007199254740993,0,NULL,X'',NULL,NULL,NULL,NULL,b'000000000',NULL,NULL),
                    (9007199254740994,NULL,'',NULL,NULL,NULL,NULL,JSON_OBJECT('empty',''),NULL,0,0);
                    INSERT INTO d5_right.exact_values SELECT * FROM d5_left.exact_values WHERE id=18446744073709551615;
                    UPDATE d5_right.exact_values SET amount=1,note='old',raw_value=X'FF',stamp=NULL,ts=NULL,tm=NULL,
                        doc=NULL,bits=b'000000000',flt=0,dbl=0;""")
                execute('exact_values')
                columns = ['amount', 'note', 'raw_value', 'stamp', 'ts', 'tm', 'doc', 'bits', 'flt', 'dbl']
                equal = ' AND '.join(f'BINARY l.`{column}` <=> BINARY r.`{column}`' for column in columns)
                matching = sql(f'SELECT COUNT(*) FROM d5_left.exact_values l JOIN d5_right.exact_values r USING(id) WHERE {equal};')
                mismatches = {column: sql(f'SELECT COUNT(*) FROM d5_left.exact_values l JOIN d5_right.exact_values r USING(id) WHERE NOT (BINARY l.`{column}` <=> BINARY r.`{column}`);') for column in columns}
                if matching != '3':
                    mismatches['nullAndLength'] = sql('SELECT l.id,l.note IS NULL,r.note IS NULL,LENGTH(l.note),LENGTH(r.note),l.raw_value IS NULL,r.raw_value IS NULL,LENGTH(l.raw_value),LENGTH(r.raw_value) FROM d5_left.exact_values l JOIN d5_right.exact_values r USING(id) ORDER BY l.id;')
                assert matching == '3', mismatches
                record('exact BIGINT DECIMAL NULL empty binary long unicode microseconds JSON BIT float values')

                for change, mutation in [('changed', 'UPDATE d5_right.{table} SET value=99 WHERE id=2'),
                                         ('deleted', 'DELETE FROM d5_right.{table} WHERE id=2'),
                                         ('inserted', 'INSERT INTO d5_right.{table} VALUES(2,99,9)')]:
                    table = 'conflict_' + change
                    pair(table)
                    sql(f'INSERT INTO d5_left.{table} VALUES(1,10,9),(2,20,9); '
                        f'INSERT INTO d5_right.{table} VALUES(1,11,9)' + (',(2,21,9);' if change != 'inserted' else ';'))
                    path, result = plan(table)
                    assert result.get('ok'), result
                    sql(mutation.format(table=table) + ';')
                    before = sql(f'SELECT * FROM d5_right.{table} ORDER BY id;')
                    failed = batch(path, result['plan'])
                    assert not failed.get('ok') and failed.get('status') == 'failed', failed
                    assert failed.get('committed', 0) == 0, failed
                    assert sql(f'SELECT * FROM d5_right.{table} ORDER BY id;') == before, 'Earlier row in failed batch was not rolled back'
                    record('target ' + change + ': whole batch rollback', status=failed.get('status'))

                pair('unselected_conflict')
                sql('INSERT INTO d5_left.unselected_conflict VALUES(1,10,9); INSERT INTO d5_right.unselected_conflict VALUES(1,11,9);')
                path, result = plan('unselected_conflict', fields=['value'], filters=[{'field': 'untouched', 'op': '=', 'value': '9'}])
                assert result.get('ok'), result
                sql('UPDATE d5_right.unselected_conflict SET untouched=10;')
                failed = batch(path, result['plan'])
                assert not failed.get('ok'), failed
                assert sql('SELECT value,untouched FROM d5_right.unselected_conflict;') == '11\t10'
                record('unselected range field drift blocks overwrite')

                pair('outside_scope')
                sql('INSERT INTO d5_left.outside_scope VALUES(1,1,9); INSERT INTO d5_right.outside_scope VALUES(1,2,9);')
                path, result = plan('outside_scope', filters=[{'field': 'value', 'op': '=', 'value': '1'}])
                assert result.get('ok'), result
                failed = batch(path, result['plan'])
                assert not failed.get('ok') and failed.get('status') == 'failed', failed
                assert sql('SELECT value FROM d5_right.outside_scope;') == '2'
                record('shared filter cannot overwrite same key outside target scope')

                for table in ['many_batches', 'later_conflict']:
                    pair(table)
                    sql(f'INSERT INTO d5_left.{table} WITH RECURSIVE seq AS '
                        '(SELECT 1 n UNION ALL SELECT n+1 FROM seq WHERE n<600) SELECT n,n,9 FROM seq;')
                current = execute('many_batches')
                assert current['total'] == 600 and sql('SELECT COUNT(*) FROM d5_right.many_batches;') == '600'
                record('600 operations across three committed batches')
                path, result = plan('later_conflict')
                assert result.get('ok'), result
                current = result['plan']
                first = batch(path, current)
                assert first.get('ok') and first['committed'] == 256, first
                # Insert every remaining key, so the second batch conflicts regardless of cache ordering.
                sql('INSERT INTO d5_right.later_conflict SELECT id,-1,9 FROM d5_left.later_conflict '
                    'WHERE id NOT IN (SELECT id FROM d5_right.later_conflict);')
                failed = batch(path, current, 256)
                assert not failed.get('ok') and failed.get('committed', 0) == 0, failed
                assert sql('SELECT COUNT(*) FROM d5_right.later_conflict WHERE value<>-1;') == '256'
                record('later batch conflict retains only previously confirmed writes', committed=256)

                pair('drift')
                sql('INSERT INTO d5_left.drift VALUES(1,1,9);')
                path, result = plan('drift')
                assert result.get('ok'), result
                sql('ALTER TABLE d5_right.drift ADD external_change INT;')
                failed = batch(path, result['plan'])
                assert not failed.get('ok'), failed
                assert sql('SELECT COUNT(*) FROM d5_right.drift;') == '0'
                record('target structure drift rejects write')

                for table, definition, engine, options in [
                    ('myisam', 'id INT PRIMARY KEY,value INT', 'MyISAM', {}),
                    ('no_key', 'id INT,value INT', 'InnoDB', {'key': [], 'mode': 'browse'}),
                    ('required_column', 'id INT PRIMARY KEY,value INT', 'InnoDB', {})]:
                    pair(table, definition, engine)
                    sql(f'INSERT INTO d5_left.{table} VALUES(1,1);')
                    if table == 'required_column':
                        sql('ALTER TABLE d5_right.required_column ADD must_supply INT NOT NULL;')
                    path, task_id = scan(table, **options)
                    result = invoke('merge-plan', path, {'taskId': task_id, 'direction': 'left-to-right', 'mode': 'fill'})
                    assert not result.get('ok'), result
                    assert sql(f'SELECT COUNT(*) FROM d5_right.{table};') == '0'
                    record(table + ': unsafe plan rejected')

                pair('incompatible')
                sql('ALTER TABLE d5_right.incompatible MODIFY value VARCHAR(4);')
                path = root / 'incompatible-cache'
                path.mkdir()
                rejected = invoke('data-start', path, {'left': {'database': 'd5_left', 'table': 'incompatible'},
                    'right': {'database': 'd5_right', 'table': 'incompatible'}, 'key': ['id'],
                    'fields': ['id', 'value'], 'filters': []}, taskId=str(uuid.uuid4()))
                assert not rejected.get('ok'), rejected
                record('incompatible selected type rejected before writable scan')

                pair('constraint_failure')
                sql('ALTER TABLE d5_right.constraint_failure ADD CONSTRAINT positive_value CHECK (value>0); '
                    'INSERT INTO d5_left.constraint_failure VALUES(1,1,9),(2,-1,9);')
                path, result = plan('constraint_failure')
                assert result.get('ok'), result
                failed = batch(path, result['plan'])
                assert not failed.get('ok') and failed.get('status') == 'failed', failed
                assert sql('SELECT COUNT(*) FROM d5_right.constraint_failure;') == '0'
                record('CHECK failure rolls back batch with constraints enabled')

                pair('unknown_trigger')
                sql('CREATE TABLE d5_right.side_effect (id INT); '
                    'CREATE TRIGGER d5_right.cross_table AFTER INSERT ON d5_right.unknown_trigger '
                    'FOR EACH ROW INSERT INTO d5_right.side_effect VALUES(NEW.id); '
                    'INSERT INTO d5_left.unknown_trigger VALUES(1,1,9);')
                path, result = plan('unknown_trigger')
                assert not result.get('ok'), result
                assert sql('SELECT COUNT(*) FROM d5_right.side_effect;') == '0'
                record('unproved cross-table trigger effects block plan')

                for event in ['INSERT', 'UPDATE']:
                    table = 'trigger_key_' + event.lower()
                    pair(table)
                    sql(f'INSERT INTO d5_left.{table} VALUES(1,10,9);')
                    if event == 'UPDATE':
                        sql(f'INSERT INTO d5_right.{table} VALUES(1,11,9);')
                    sql(f'CREATE TRIGGER d5_right.{table}_key BEFORE {event} ON d5_right.{table} '
                        'FOR EACH ROW SET NEW.ID=999;')
                    before = sql(f'SELECT * FROM d5_right.{table};')
                    path, result = plan(table)
                    assert not result.get('ok'), result
                    assert sql(f'SELECT * FROM d5_right.{table};') == before
                    record(event + ' trigger cannot evade matching-key guard through column case')

                pair('trigger_parent', 'id INT PRIMARY KEY,note VARCHAR(30),code INT NOT NULL UNIQUE')
                sql("INSERT INTO d5_left.trigger_parent VALUES(1,'source',10); "
                    "INSERT INTO d5_right.trigger_parent VALUES(1,'target',10); "
                    'CREATE TABLE d5_right.trigger_child (id INT PRIMARY KEY,code INT, '
                    'FOREIGN KEY(code) REFERENCES d5_right.trigger_parent(code) ON UPDATE CASCADE); '
                    'INSERT INTO d5_right.trigger_child VALUES(1,10); '
                    'CREATE TRIGGER d5_right.cascade_code BEFORE UPDATE ON d5_right.trigger_parent '
                    'FOR EACH ROW SET NEW.code=20;')
                path, result = plan('trigger_parent', fields=['note'])
                assert not result.get('ok'), result
                assert sql('SELECT code FROM d5_right.trigger_child;') == '10'
                assert sql('SELECT note,code FROM d5_right.trigger_parent;') == 'target\t10'
                record('trigger write to unselected externally referenced column blocks cascade')

                pair('safe_trigger', 'id INT PRIMARY KEY,note VARCHAR(30)')
                sql("INSERT INTO d5_left.safe_trigger VALUES(1,'source'),(2,'source'); "
                    "INSERT INTO d5_right.safe_trigger VALUES(1,'target'); "
                    'CREATE TRIGGER d5_right.safe_note_insert BEFORE INSERT ON d5_right.safe_trigger '
                    "FOR EACH ROW SET NEW.note='trigger'; "
                    'CREATE TRIGGER d5_right.safe_note_update BEFORE UPDATE ON d5_right.safe_trigger '
                    "FOR EACH ROW SET NEW.note='trigger';")
                current = execute('safe_trigger')
                assert current['warnings'] and any('触发器' in warning for warning in current['warnings']), current
                assert sql('SELECT id,note FROM d5_right.safe_trigger ORDER BY id;') == '1\ttrigger\n2\ttrigger'
                assert sql("SELECT COUNT(*) FROM information_schema.TRIGGERS WHERE TRIGGER_SCHEMA='d5_right' AND EVENT_OBJECT_TABLE='safe_trigger';") == '2'
                record('safe BEFORE assignment triggers remain active and disclosed in preview')

                pair('reader_only')
                sql("INSERT INTO d5_left.reader_only VALUES(1,1,9); CREATE USER 'd5_reader'@'localhost' IDENTIFIED BY ''; "
                    "GRANT SELECT ON d5_left.* TO 'd5_reader'@'localhost'; GRANT SELECT ON d5_right.* TO 'd5_reader'@'localhost';")
                original_base = base
                base = base | {'user': 'd5_reader', 'password': ''}
                try:
                    path, result = plan('reader_only')
                    assert not result.get('ok'), result
                finally:
                    base = original_base
                assert sql('SELECT COUNT(*) FROM d5_right.reader_only;') == '0'
                record('SELECT-only account can compare but cannot plan unproved writes')

                pair('strict_values', 'id INT PRIMARY KEY, value DATETIME')
                sql("SET SESSION sql_mode=''; INSERT INTO d5_left.strict_values VALUES(1,'0000-00-00 00:00:00'); SET GLOBAL sql_mode='';")
                path, result = plan('strict_values')
                if result.get('ok'):
                    failed = batch(path, result['plan'])
                    assert not failed.get('ok') and failed.get('status') == 'failed', failed
                assert sql('SELECT COUNT(*) FROM d5_right.strict_values;') == '0'
                record('strict writer refuses zero date despite permissive server default')
                sql("SET GLOBAL sql_mode='STRICT_TRANS_TABLES,NO_ZERO_DATE,NO_ZERO_IN_DATE,NO_ENGINE_SUBSTITUTION';")

                if not args.skip_desktop:
                    pair('ui_sample')
                    sql('INSERT INTO d5_left.ui_sample VALUES(1,10,9),(2,20,9); INSERT INTO d5_right.ui_sample VALUES(1,11,9),(3,30,9);')
                    pair('ui_stop')
                    sql('INSERT INTO d5_left.ui_stop WITH RECURSIVE seq AS '
                        '(SELECT 1 n UNION ALL SELECT n+1 FROM seq WHERE n<800) SELECT n,n,9 FROM seq;')
                    sql("CREATE USER 'd5_ui'@'localhost' IDENTIFIED BY ''; GRANT SELECT ON *.* TO 'd5_ui'@'localhost'; GRANT ALL ON d5_left.* TO 'd5_ui'@'localhost'; GRANT ALL ON d5_right.* TO 'd5_ui'@'localhost';")
                    ui_state = root / 'ui-state'
                    ui_state.mkdir()
                    left_id, right_id = str(uuid.uuid4()), str(uuid.uuid4())
                    connections = []
                    for identifier, name in [(left_id, 'D5 Left fixture'), (right_id, 'D5 Right fixture')]:
                        connection = base | {'id': identifier, 'name': name, 'user': 'd5_ui', 'remember': False}
                        connection.pop('password')
                        connections.append(connection)
                    state = {'connections': connections, 'left': left_id, 'right': right_id,
                        'settings': {'theme': 'light', 'density': 'standard', 'timeout': 3},
                        'workspaces': {f'{left_id}:{right_id}': {'left': {'database': 'd5_left', 'table': 'ui_sample'},
                            'right': {'database': 'd5_right', 'table': 'ui_sample'}, 'width': 240}}}
                    (ui_state / 'connections.json').write_text(json.dumps(state) + '\n')
                    debug_port = free_port()
                    with (root / 'desktop.log').open('wb') as desktop_log:
                        desktop = subprocess.Popen([str(args.app.resolve())], stdout=desktop_log, stderr=desktop_log,
                            env=os.environ | {'SPEED_SYNC_DATA_DIR': str(ui_state), 'QTWEBENGINE_REMOTE_DEBUGGING': f'127.0.0.1:{debug_port}'})
                        desktop_output = args.output.parent / 'd5-desktop.json'
                        subprocess.run(['node', str(args.desktop_script.resolve()), '--port', str(debug_port),
                                        '--output', str(desktop_output)], check=True, timeout=240)
                    evidence['desktop'] = json.loads(desktop_output.read_text())
                    assert sql('SELECT id,value FROM d5_right.ui_sample ORDER BY id;') == '1\t11\n2\t20\n3\t30'
                    record('real Qt default fill independently verified in target database')
                else:
                    evidence['desktop'] = {'result': 'NOT RUN', 'reason': '--skip-desktop'}
                evidence['result'] = 'passed'
                print(f"{len(evidence['checks'])} D5 isolated MySQL checks passed")
            except Exception as error:
                evidence['result'] = 'failed'
                evidence['error'] = str(error).replace(secret, '[redacted]') if secret else str(error)
                raise
            finally:
                for process in [desktop, server]:
                    if process is not None:
                        process.terminate()
                        try:
                            process.wait(timeout=15)
                        except subprocess.TimeoutExpired:
                            process.kill()
                            process.wait()
                args.output.write_text(json.dumps(evidence, ensure_ascii=False, indent=2) + '\n')


if __name__ == '__main__':
    main()
