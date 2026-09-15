"""Real QMYSQL checks against an isolated, disposable loopback MySQL instance."""
import argparse
import json
import os
from pathlib import Path
import secrets
import socket
import subprocess
import tempfile
import time

parser = argparse.ArgumentParser()
parser.add_argument('--mysql-home', required=True, type=Path)
parser.add_argument('--app', required=True, type=Path)
parser.add_argument('--output', required=True, type=Path)
args = parser.parse_args()
args.output.parent.mkdir(parents=True, exist_ok=True)
with tempfile.TemporaryDirectory(prefix='speed-sync-mysql-') as temporary:
    root = Path(temporary)
    data = root / 'data'
    sock = root / 'mysql.sock'
    with socket.socket() as port_socket:
        port_socket.bind(('127.0.0.1', 0))
        port = port_socket.getsockname()[1]
    init = subprocess.run([str(args.mysql_home / 'bin/mysqld'), '--no-defaults', '--initialize-insecure', f'--basedir={args.mysql_home}', f'--datadir={data}'], capture_output=True, timeout=60)
    if init.returncode:
        raise RuntimeError('Isolated MySQL initialization failed; no existing server was touched')
    with (root / 'server.log').open('wb') as log:
        server = subprocess.Popen([str(args.mysql_home / 'bin/mysqld'), '--no-defaults', f'--basedir={args.mysql_home}', f'--datadir={data}', f'--socket={sock}', f'--port={port}', '--bind-address=127.0.0.1', '--mysqlx=OFF', f'--pid-file={root / "server.pid"}'], stdout=log, stderr=log)
        try:
            client = [str(args.mysql_home / 'bin/mysql'), '--no-defaults', '--protocol=socket', f'--socket={sock}', '-uroot', '--batch', '--skip-column-names']
            for _ in range(100):
                if server.poll() is not None: raise RuntimeError('Isolated server exited')
                ping = subprocess.run(client, input='SELECT 1;', text=True, capture_output=True)
                if ping.returncode == 0: break
                time.sleep(.1)
            else: raise RuntimeError('Isolated server startup timed out')
            secret = secrets.token_urlsafe(24)
            setup = subprocess.run(client, input=f"ALTER USER 'root'@'localhost' IDENTIFIED BY '{secret}';", text=True, capture_output=True)
            if setup.returncode: raise RuntimeError('Fixture account initialization failed')
            base = dict(name='Isolated integration fixture', host='127.0.0.1', port=port, user='root', password=secret, database='', tls='preferred', ca='', timeout=2)
            results = []
            def probe(name, patch, expected, extra=None):
                payload = base | patch
                proc = subprocess.run([str(args.app.resolve()), '--probe'], input=json.dumps(payload), text=True, capture_output=True, timeout=12)
                assert proc.returncode == 0, f'{name}: probe process failed'
                assert secret not in proc.stdout and secret not in proc.stderr, f'{name}: credential leak'
                result = json.loads(proc.stdout)
                assert result.get('code', 'success' if result.get('ok') else 'unknown') == expected, f'{name}: unexpected outcome {result}'
                if extra: extra(result)
                results.append(dict(name=name, result='passed', evidence=result))
            probe('real_mysql_tls_preferred', {}, 'success')
            probe('real_mysql_tls_required', {'tls': 'required'}, 'success', lambda r: r['encrypted'] or (_ for _ in ()).throw(AssertionError('TLS missing')))
            probe('real_mysql_tls_disabled', {'tls': 'disabled'}, 'success', lambda r: not r['encrypted'] or (_ for _ in ()).throw(AssertionError('TLS unexpectedly enabled')))
            probe('wrong_password', {'password': 'deliberately-wrong-fixture-password'}, 'authentication')
            probe('missing_database', {'database': 'does_not_exist_fixture'}, 'database')
            probe('tls_wrong_ca', {'tls': 'verify', 'ca': str(root / 'missing-ca.pem')}, 'tls')
            with socket.socket() as closed:
                closed.bind(('127.0.0.1', 0)); unused = closed.getsockname()[1]
            probe('unreachable_port', {'port': unused}, 'network')
            probe('invalid_port', {'port': 0}, 'validation')
            args.output.write_text(json.dumps({'mysql': '8.0.46', 'checks': results}, ensure_ascii=False, indent=2) + '\n')
            print(f'{len(results)} real QMYSQL checks passed; fixture removed on exit')
        finally:
            server.terminate()
            try: server.wait(timeout=15)
            except subprocess.TimeoutExpired:
                server.kill(); server.wait()
