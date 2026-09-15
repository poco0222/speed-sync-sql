#!/usr/bin/env python3
"""D4 real Qt/MySQL checks on an isolated disposable server only."""
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


def free_port():
    with socket.socket() as sock:
        sock.bind(('127.0.0.1', 0))
        return sock.getsockname()[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--mysql-home', required=True, type=Path)
    parser.add_argument('--app', required=True, type=Path)
    parser.add_argument('--output', required=True, type=Path)
    args = parser.parse_args()
    args.output.parent.mkdir(parents=True, exist_ok=True)
    evidence = {'checks': []}
    with tempfile.TemporaryDirectory(prefix='speed-sync-data-') as temporary:
        root = Path(temporary)
        data, sock = root / 'data', root / 'mysql.sock'
        port = free_port()
        initialized = subprocess.run([str(args.mysql_home / 'bin/mysqld'), '--no-defaults', '--initialize-insecure',
                                     f'--basedir={args.mysql_home}', f'--datadir={data}'], capture_output=True, timeout=60)
        if initialized.returncode:
            raise RuntimeError('Isolated MySQL initialization failed')
        with (root / 'server.log').open('wb') as log:
            server = subprocess.Popen([str(args.mysql_home / 'bin/mysqld'), '--no-defaults',
                                       f'--basedir={args.mysql_home}', f'--datadir={data}', f'--socket={sock}',
                                       f'--port={port}', '--bind-address=127.0.0.1', '--mysqlx=OFF',
                                       f'--pid-file={root / "server.pid"}'], stdout=log, stderr=log)
            desktop = None
            clipboard = None
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
                                            env=env if authenticated else os.environ, timeout=30)
                    if result.returncode:
                        raise RuntimeError(result.stderr.replace(secret, '[redacted]'))
                    return result.stdout

                sql(f"ALTER USER 'root'@'localhost' IDENTIFIED BY '{secret}';", False)
                evidence['mysql'] = sql('SELECT VERSION();').strip()
                sql('CREATE DATABASE d4_left; CREATE DATABASE d4_right;')
                sql("""CREATE TABLE d4_left.sample (
                    id BIGINT NOT NULL, sub INT NOT NULL, amount DECIMAL(40,15),
                    note LONGTEXT, raw_value LONGBLOB, stamp DATETIME(6), doc JSON,
                    PRIMARY KEY(id,sub)) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_bin;
                    CREATE TABLE d4_right.sample LIKE d4_left.sample;
                    INSERT INTO d4_left.sample VALUES
                    (9007199254740993,1,12345678901234567890123.123456789012345,CONCAT('中文',CHAR(0),'😀'),X'00FF10','2026-09-15 08:09:10.123456',JSON_OBJECT('large',9007199254740993,'a',1)),
                    (9007199254740994,1,0,NULL,X'00','2026-09-15 08:09:10.000001',JSON_OBJECT('a',1)),
                    (9007199254740995,1,1,'left-only',X'FF',NULL,NULL),
                    (9007199254740996,1,1,CONCAT(REPEAT('长😀',12000),'左'),X'FE',NULL,NULL);
                    INSERT INTO d4_right.sample SELECT * FROM d4_left.sample;
                    UPDATE d4_right.sample SET note='',stamp='2026-09-15 08:09:10.000002',doc=JSON_OBJECT('a',2) WHERE id=9007199254740994;
                    DELETE FROM d4_right.sample WHERE id=9007199254740995;
                    INSERT INTO d4_right.sample VALUES(9007199254740997,1,1,'right-only',X'FF',NULL,NULL);
                    UPDATE d4_right.sample SET note=CONCAT(REPEAT('长😀',12000),'右') WHERE id=9007199254740996;
                    CREATE TABLE d4_left.no_key (value INT,note TEXT);
                    CREATE TABLE d4_right.no_key LIKE d4_left.no_key;
                    INSERT INTO d4_left.no_key VALUES(1,'a'),(1,'b');
                    INSERT INTO d4_right.no_key VALUES(1,'b');
                    CREATE TABLE d4_left.unique_key (code VARCHAR(32) NOT NULL UNIQUE,value INT);
                    CREATE TABLE d4_right.unique_key LIKE d4_left.unique_key;
                    INSERT INTO d4_left.unique_key VALUES('a',1); INSERT INTO d4_right.unique_key VALUES('a',1);
                    CREATE TABLE d4_left.nullable_key (code INT UNIQUE,value INT);
                    CREATE TABLE d4_right.nullable_key LIKE d4_left.nullable_key;
                    CREATE TABLE d4_left.batched (id INT PRIMARY KEY,value INT);
                    CREATE TABLE d4_right.batched LIKE d4_left.batched;
                    SET SESSION cte_max_recursion_depth=5000;
                    INSERT INTO d4_left.batched WITH RECURSIVE seq AS (SELECT 1 n UNION ALL SELECT n+1 FROM seq WHERE n<2500) SELECT n,n FROM seq;
                    INSERT INTO d4_right.batched SELECT * FROM d4_left.batched;
                    UPDATE d4_right.batched SET value=-1 WHERE id IN (250,251,500,501,1000,1001,2500);
                    DELETE FROM d4_right.batched WHERE id=2499;
                    INSERT INTO d4_right.batched VALUES(2501,2501);
                    CREATE TABLE d4_left.types (id INT PRIMARY KEY,bits BIT(9),ts TIMESTAMP(6),tm TIME(6),flt FLOAT,dbl DOUBLE);
                    CREATE TABLE d4_right.types LIKE d4_left.types;
                    SET time_zone='+00:00';
                    INSERT INTO d4_left.types VALUES(1,b'100000001','2026-09-15 08:09:10.654321','-12:34:56.123456',1.25,1.23456789012345);
                    INSERT INTO d4_right.types SELECT * FROM d4_left.types;
                    INSERT INTO d4_left.types VALUES(2,b'100000010',NULL,NULL,1.0000001192092896,1.234567890123456);
                    INSERT INTO d4_right.types VALUES(2,b'100000010',NULL,NULL,1.000000238418579,1.234567890123457);
                    CREATE TABLE d4_left.text_key (code VARCHAR(32) CHARACTER SET utf8mb4 COLLATE utf8mb4_general_ci NOT NULL PRIMARY KEY,value INT);
                    CREATE TABLE d4_right.text_key LIKE d4_left.text_key;
                    INSERT INTO d4_left.text_key VALUES('A',1),('pad ',2);
                    INSERT INTO d4_right.text_key VALUES('a',1),('pad',2);
                    CREATE USER 'd4_reader'@'localhost' IDENTIFIED BY '';
                    GRANT SELECT ON d4_left.* TO 'd4_reader'@'localhost';
                    GRANT SELECT ON d4_right.* TO 'd4_reader'@'localhost';
                    CREATE USER 'd4_denied'@'localhost' IDENTIFIED BY '';
                    """)
                evidence['floatConversion'] = sql('SELECT CONVERT(flt USING utf8mb4),CONVERT(dbl USING utf8mb4) FROM d4_left.types WHERE id=2 UNION ALL SELECT CONVERT(flt USING utf8mb4),CONVERT(dbl USING utf8mb4) FROM d4_right.types WHERE id=2;').splitlines()
                evidence['floatPromotedConversion'] = sql('SELECT CONVERT(CAST(flt AS DOUBLE) USING utf8mb4) FROM d4_left.types WHERE id=2 UNION ALL SELECT CONVERT(CAST(flt AS DOUBLE) USING utf8mb4) FROM d4_right.types WHERE id=2;').splitlines()
                before = sql('CHECKSUM TABLE d4_left.sample,d4_right.sample,d4_left.batched,d4_right.batched;')
                state_dir = root / 'ui-state'
                state_dir.mkdir()
                left_id, right_id = str(uuid.uuid4()), str(uuid.uuid4())
                base = dict(host='127.0.0.1',port=port,user='d4_reader',database='',tls='preferred',ca='',timeout=3,remember=False)
                connections = [base | {'id': left_id,'name': 'D4 Left fixture'},base | {'id':right_id,'name':'D4 Right fixture'}]
                state = {'connections':connections,'left':left_id,'right':right_id,
                         'settings':{'theme':'light','density':'standard','timeout':3},
                         'workspaces':{f'{left_id}:{right_id}':{'left':{'database':'d4_left','table':'sample'},
                                                           'right':{'database':'d4_right','table':'sample'},'width':240}}}
                (state_dir/'connections.json').write_text(json.dumps(state)+'\n')
                sql("SET GLOBAL log_output='TABLE'; SET GLOBAL general_log=ON;")
                debug_port = free_port()
                with (root/'desktop.log').open('wb') as desktop_log:
                    desktop = subprocess.Popen([str(args.app.resolve())],stdout=desktop_log,stderr=desktop_log,
                        env=os.environ|{'SPEED_SYNC_DATA_DIR':str(state_dir),'QTWEBENGINE_REMOTE_DEBUGGING':f'127.0.0.1:{debug_port}'})
                    clipboard_tool = Path(__file__).resolve().parents[1]/'.local/test-tools/preserve-pasteboard'
                    if not clipboard_tool.exists():
                        clipboard_tool.parent.mkdir(parents=True,exist_ok=True)
                        subprocess.run(['swiftc',str(Path(__file__).with_name('preserve_pasteboard.swift')), '-o',str(clipboard_tool)],check=True,timeout=60)
                    clipboard = subprocess.Popen([str(clipboard_tool)],stdin=subprocess.PIPE,stdout=subprocess.PIPE,text=True)
                    assert clipboard.stdout.readline().strip()=='ready','Clipboard preservation failed'
                    subprocess.run(['node',str(Path(__file__).with_name('desktop_data.mjs').resolve()),'--port',str(debug_port),
                                    '--output',str(args.output.parent/'d4-desktop.json')],check=True,timeout=240)
                sql('SET GLOBAL general_log=OFF;')
                statements = sql("SELECT argument FROM mysql.general_log WHERE user_host LIKE 'd4_reader%' AND command_type IN ('Query','Prepare','Execute');").splitlines()
                import re
                unexpected = [statement for statement in statements if not re.match(r'^\s*(SELECT|SHOW|SET|START TRANSACTION|COMMIT|ROLLBACK|BEGIN)\b',statement,re.I)]
                assert statements and not unexpected, 'Data reader issued unexpected SQL verbs'
                assert before == sql('CHECKSUM TABLE d4_left.sample,d4_right.sample,d4_left.batched,d4_right.batched;'), 'Fixture changed'
                evidence['checks'] = [{'name':'read-only query audit','result':'passed','statements':len(statements)},
                                      {'name':'sample checksums unchanged','result':'passed'}]
                evidence['desktop'] = json.loads((args.output.parent/'d4-desktop.json').read_text())
                print('D4 real MySQL/Qt integration and read-only audit passed')
            except Exception as error:
                evidence['error'] = str(error)
                raise
            finally:
                if clipboard is not None:
                    clipboard.communicate('restore\n',timeout=10)
                    if clipboard.returncode:
                        evidence['clipboardRestoreError']='Clipboard restoration failed'
                for process in [desktop,server]:
                    if process is not None:
                        process.terminate()
                        try:
                            process.wait(timeout=15)
                        except subprocess.TimeoutExpired:
                            process.kill()
                            process.wait()
                args.output.write_text(json.dumps(evidence,ensure_ascii=False,indent=2)+'\n')


if __name__ == '__main__':
    main()
