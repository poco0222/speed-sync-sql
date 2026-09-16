#!/usr/bin/env python3
"""Build a self-contained distribution on a native Windows/macOS runner."""
import argparse
from contextlib import contextmanager
import hashlib
import json
import os
from pathlib import Path
import platform
import re
import shutil
import subprocess
import tarfile
import time
import urllib.request

ROOT = Path(__file__).resolve().parents[1]
WORK = ROOT / '.local/release'
QT_VERSION = '6.10.2'
MYSQL_VERSION = '8.0.46'
MAC = platform.system() == 'Darwin'
EXTRACT_TIMEOUT = 300


@contextmanager
def log_stage(name):
    started = time.monotonic()
    print(f'[START] {name}', flush=True)
    try:
        yield
    except BaseException as error:
        print(f'[FAIL] {name} ({time.monotonic() - started:.1f}s): {error}', flush=True)
        raise
    else:
        print(f'[DONE] {name} ({time.monotonic() - started:.1f}s)', flush=True)


def run(*args, **kwargs):
    kwargs.setdefault('timeout', 1200)
    with log_stage(subprocess.list2cmdline([str(a) for a in args])):
        return subprocess.run([str(a) for a in args], check=True, **kwargs)


def output(*args):
    return subprocess.check_output([str(a) for a in args], text=True, timeout=60)


def download(url, destination):
    destination.parent.mkdir(parents=True, exist_ok=True)
    # A partial network response must never become a cached archive.
    temporary = destination.with_suffix(destination.suffix + '.partial')
    with log_stage(f'Download {destination.name}'):
        with urllib.request.urlopen(url, timeout=120) as response, temporary.open('wb') as target:
            shutil.copyfileobj(response, target)
        temporary.replace(destination)
        print(f'Downloaded {destination.stat().st_size} bytes', flush=True)
    return destination


def extract(archive):
    if archive.suffix == '.zip':
        # Avoid runner-specific tar/xz resolution on Windows.
        run(os.sys.executable, '-m', 'zipfile', '-e', archive, WORK, timeout=EXTRACT_TIMEOUT)
    else:
        run('tar', '-xf', archive, '-C', WORK, timeout=EXTRACT_TIMEOUT)


def diagnose_tests(build):
    suffix = '.exe' if os.name == 'nt' else ''
    test_env = os.environ.copy()
    test_env.update({'QT_QPA_PLATFORM': 'offscreen', 'SPEED_SYNC_ALLOW_MULTI_INSTANCE': '1'})
    for executable in sorted(build.glob(f'*-tests{suffix}')):
        print(f'[DIAGNOSTIC] Running {executable.name} directly', flush=True)
        try:
            result = subprocess.run(
                [str(executable)], cwd=build, capture_output=True, text=True,
                timeout=120, env=test_env, check=False,
            )
            print(f'[DIAGNOSTIC] {executable.name} exit={result.returncode}', flush=True)
            if result.stdout:
                print(f'[DIAGNOSTIC] stdout:\n{result.stdout}', flush=True)
            if result.stderr:
                print(f'[DIAGNOSTIC] stderr:\n{result.stderr}', flush=True)
        except Exception as error:
            print(f'[DIAGNOSTIC] {executable.name} launcher error: {error}', flush=True)


def sha256(path):
    digest = hashlib.sha256()
    with path.open('rb') as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b''):
            digest.update(block)
    return digest.hexdigest()


def prepare_mysql():
    suffix = 'macos15-arm64.tar.gz' if MAC else 'winx64.zip'
    name = f'mysql-{MYSQL_VERSION}-{suffix}'
    archive = download(f'https://cdn.mysql.com/Downloads/MySQL-8.0/{name}', WORK / name)
    extract(archive)
    return WORK / name.removesuffix('.tar.gz').removesuffix('.zip')


def prepare_qtbase():
    name = f'qtbase-everywhere-src-{QT_VERSION}'
    suffix = '.tar.xz' if MAC else '.zip'
    url = f'https://download.qt.io/archive/qt/6.10/{QT_VERSION}/submodules/{name}{suffix}'
    archive = download(url, WORK / f'{name}{suffix}')
    with log_stage('Verify QtBase SHA-256'):
        with urllib.request.urlopen(url + '.sha256', timeout=30) as response:
            checksum = response.read().decode().split()[0]
        if sha256(archive) != checksum:
            raise RuntimeError('QtBase source checksum mismatch')
    extract(archive)
    return WORK / name


def deploy_windows(qt, mysql, app, stage):
    shutil.copy2(app, stage / app.name)
    run(qt / 'bin/windeployqt.exe', '--release', '--compiler-runtime', '--dir', stage, app)
    plugin_dir = stage / 'sqldrivers'
    plugin_dir.mkdir(exist_ok=True)
    shutil.copy2(WORK / 'driver/plugins/sqldrivers/qsqlmysql.dll', plugin_dir)
    # Resolve every non-system import, including MySQL SSL/crypto and the MSVC CRT.
    search = [stage, mysql / 'lib', mysql / 'bin', qt / 'bin']
    redist = os.environ.get('VCToolsRedistDir')
    if redist:
        search.extend(Path(redist).glob('x64/Microsoft.VC*.CRT'))
    system = Path(os.environ['SystemRoot']) / 'System32'
    pending = list(stage.rglob('*.dll')) + list(stage.rglob('*.exe'))
    seen = set()
    while pending:
        binary = pending.pop()
        if binary in seen:
            continue
        seen.add(binary)
        imports = re.findall(r'^\s+([\w.+-]+\.dll)\s*$', output('dumpbin', '/dependents', binary), re.M | re.I)
        for name in imports:
            if name.lower().startswith(('api-ms-win-', 'ext-ms-win-')):
                continue
            local = binary.parent / name
            if local.is_file():
                pending.append(local)
                continue
            source = next((directory / name for directory in search if (directory / name).is_file()), None)
            if source:
                target = stage / name
                if source != target:
                    shutil.copy2(source, target)
                pending.append(target)
            elif not (system / name).is_file():
                raise RuntimeError(f'Missing dependency: {binary.name}: {name}')
    return stage / app.name


def macho_rpaths(binary):
    return list(dict.fromkeys(re.findall(r'cmd LC_RPATH\s+cmdsize \d+\s+path (.+?) \(offset', output('otool', '-l', binary))))


def loader_path(value, binary):
    if value.startswith('@loader_path/'):
        return binary.parent / value.removeprefix('@loader_path/')
    if value.startswith('@executable_path/'):
        bundle = next(parent for parent in binary.parents if parent.suffix == '.app')
        return bundle / 'Contents/MacOS' / value.removeprefix('@executable_path/')
    return Path(value)


def check_macos_dependencies(app):
    binaries = []
    for path in app.rglob('*'):
        if path.is_file() and not path.is_symlink() and 'Mach-O' in output('file', '-b', path):
            binaries.append(path)
    for binary in binaries:
        rpaths = [loader_path(value, binary) for value in macho_rpaths(binary)]
        for path in rpaths:
            if not path.resolve().is_relative_to(app.resolve()):
                raise RuntimeError(f'External rpath: {binary}: {path}')
        install_ids = output('otool', '-D', binary).splitlines()[1:]
        for line in output('otool', '-L', binary).splitlines()[1:]:
            if ' (compatibility version ' not in line:
                continue
            dependency = line.strip().split(' (', 1)[0]
            if dependency in install_ids:
                continue
            if dependency.startswith(('/usr/lib/', '/System/Library/')):
                continue
            if dependency.startswith('@loader_path/'):
                candidates = [loader_path(dependency, binary)]
            elif dependency.startswith('@executable_path/'):
                candidates = [loader_path(dependency, binary)]
            elif dependency.startswith('@rpath/'):
                tail = dependency.removeprefix('@rpath/')
                candidates = [path / tail for path in rpaths]
            else:
                raise RuntimeError(f'External library: {binary}: {dependency}')
            if not any(p.exists() and p.resolve().is_relative_to(app.resolve()) for p in candidates):
                raise RuntimeError(f'Unresolved library: {binary}: {dependency}')
    if not binaries:
        raise RuntimeError('No Mach-O binaries deployed')
    return binaries


def deploy_macos_client(mysql, plugin, frameworks):
    frameworks.mkdir(parents=True, exist_ok=True)
    first = (mysql / 'lib/libmysqlclient.dylib').resolve()
    pending = [first]
    seen = set()
    while pending:
        source = pending.pop()
        if source in seen:
            continue
        seen.add(source)
        target = frameworks / source.name
        shutil.copy2(source, target)
        ids = output('otool', '-D', source).splitlines()[1:]
        for line in output('otool', '-L', source).splitlines()[1:]:
            if ' (compatibility version ' not in line:
                continue
            dependency = line.strip().split(' (', 1)[0]
            if dependency in ids or dependency.startswith(('/usr/lib/', '/System/Library/')):
                continue
            if dependency.startswith('@loader_path/'):
                resolved = source.parent / dependency.removeprefix('@loader_path/')
            elif dependency.startswith('@rpath/'):
                resolved = mysql / 'lib' / dependency.removeprefix('@rpath/')
            else:
                resolved = Path(dependency)
            resolved = resolved.resolve()
            if not resolved.is_file() or not resolved.is_relative_to(mysql.resolve()):
                raise RuntimeError(f'Missing MySQL dependency: {dependency}')
            pending.append(resolved)
            run('install_name_tool', '-change', dependency, f'@loader_path/{resolved.name}', target)
        run('install_name_tool', '-id', f'@rpath/{source.name}', target)
    for rpath in macho_rpaths(plugin):
        if Path(rpath).is_absolute() and Path(rpath).resolve().is_relative_to(mysql.resolve()):
            run('install_name_tool', '-delete_rpath', rpath, plugin)
    for line in output('otool', '-L', plugin).splitlines()[1:]:
        dependency = line.strip().split(' (', 1)[0]
        if Path(dependency).name == first.name:
            run('install_name_tool', '-change', dependency, f'@loader_path/../../Frameworks/{first.name}', plugin)


def deploy_macos(qt, mysql, app, stage):
    target = stage / app.name
    shutil.copytree(app, target, symlinks=True)
    # Use Qt's standard deployed plugin location; avoid a second undeployed copy.
    old_plugins = target / 'Contents/MacOS/plugins'
    if old_plugins.exists():
        shutil.rmtree(old_plugins)
    plugins = target / 'Contents/PlugIns/sqldrivers'
    plugins.mkdir(parents=True, exist_ok=True)
    plugin = plugins / 'libqsqlmysql.dylib'
    shutil.copy2(WORK / 'driver/plugins/sqldrivers/libqsqlmysql.dylib', plugin)
    deploy_macos_client(mysql, plugin, target / 'Contents/Frameworks')
    selected = [plugin]
    for relative in ['platforms/libqcocoa.dylib', 'sqldrivers/libqsqlite.dylib',
                     'styles/libqmacstyle.dylib', 'tls/libqsecuretransportbackend.dylib']:
        destination = target / 'Contents/PlugIns' / relative
        destination.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(qt / 'plugins' / relative, destination)
        selected.append(destination)
    for category in ['imageformats', 'iconengines']:
        for original in (qt / 'plugins' / category).glob('*.dylib'):
            destination = target / 'Contents/PlugIns' / category / original.name
            destination.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(original, destination)
            selected.append(destination)
    # Avoid deploying unrelated SQL drivers with unavailable vendor libraries.
    # Qt performs inside-out ad-hoc signing after rewriting dependency paths.
    (target / 'Contents/Resources').mkdir(exist_ok=True)
    (target / 'Contents/Resources/qt.conf').write_text('[Paths]\nPlugins = PlugIns\n')
    run(qt / 'bin/macdeployqt', target, '-no-plugins', '-always-overwrite', '-verbose=1',
        '-codesign=-', *(f'-executable={path}' for path in selected))
    check_macos_dependencies(target)
    return target / 'Contents/MacOS/speed-sync-sql'


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--qt-root', type=Path, required=True)
    parser.add_argument('--mysql-root', type=Path)
    parser.add_argument('--qtbase-source', type=Path)
    parser.add_argument('--allow-command-line-tools', action='store_true', help='Local macOS check only; CI uses full Xcode')
    args = parser.parse_args()
    if platform.system() not in ('Darwin', 'Windows'):
        parser.error('Run on native macOS arm64 or Windows x64')
    if platform.machine().lower() not in (('arm64',) if MAC else ('amd64', 'x86_64')):
        parser.error('Unexpected runner architecture')
    os.chdir(ROOT)
    print(f'Python: {os.sys.executable} ({platform.python_version()})', flush=True)
    WORK.mkdir(parents=True, exist_ok=True)
    qt = args.qt_root.resolve()
    mysql = args.mysql_root.resolve() if args.mysql_root else prepare_mysql()
    source = args.qtbase_source.resolve() if args.qtbase_source else prepare_qtbase()
    if output(qt / ('bin/qmake' if MAC else 'bin/qmake.exe'), '-query', 'QT_VERSION').strip() != QT_VERSION:
        raise RuntimeError('Qt version mismatch')
    if f'"{MYSQL_VERSION}"' not in (mysql / 'include/mysql_version.h').read_text():
        raise RuntimeError('MySQL client version mismatch')
    os.environ['PATH'] = str(qt / 'bin') + os.pathsep + os.environ['PATH']
    cmake = shutil.which('cmake')
    if not cmake:
        raise RuntimeError('CMake must be available on PATH')
    arch = ['-DCMAKE_OSX_ARCHITECTURES=arm64', '-DCMAKE_OSX_DEPLOYMENT_TARGET=15.0'] if MAC else []
    if args.allow_command_line_tools:
        arch.append('-DQT_NO_XCODE_MIN_VERSION_CHECK=ON')
    library = mysql / ('lib/libmysqlclient.dylib' if MAC else 'lib/libmysql.lib')
    run(cmake, '-S', source / 'src/plugins/sqldrivers', '-B', WORK / 'driver', '-G', 'Ninja',
        f'-DCMAKE_PREFIX_PATH={qt}', '-DCMAKE_BUILD_TYPE=Release',
        f'-DMySQL_INCLUDE_DIR={mysql / "include"}', f'-DMySQL_LIBRARY={library}',
        '-DQT_FORCE_MACOS_ALL_ARCHES_QMYSQLDriverPlugin=ON', *arch)
    run(cmake, '--build', WORK / 'driver', '--target', 'QMYSQLDriverPlugin', '--parallel', '3')
    npm = shutil.which('npm.cmd' if os.name == 'nt' else 'npm')
    run(npm, '--prefix', 'frontend', 'ci', '--no-audit', '--no-fund')
    run(npm, '--prefix', 'frontend', 'run', 'build')
    run(npm, '--prefix', 'frontend', 'test')
    run(os.sys.executable, '-m', 'unittest', 'discover', '-s', 'tests', '-p', 'test_release.py')
    build = WORK / 'build'
    run(cmake, '-S', ROOT, '-B', build, '-G', 'Ninja', f'-DCMAKE_PREFIX_PATH={qt}',
        '-DCMAKE_BUILD_TYPE=Release', *arch)
    run(cmake, '--build', build, '--parallel', '3')
    try:
        run('ctest', '--test-dir', build, '--output-on-failure', '--timeout', '90')
    except subprocess.CalledProcessError:
        # CTest can suppress a native test's output when the process exits early;
        # rerun verbosely so CI contains the actual assertion or loader error.
        diagnose_tests(build)
        print('[DIAGNOSTIC] Re-running failed CTest suite verbosely', flush=True)
        run('ctest', '--test-dir', build, '--output-on-failure', '--verbose', '--timeout', '90')
        raise
    version = re.search(r'project\(SpeedSyncSQL VERSION ([0-9.]+)', (ROOT / 'CMakeLists.txt').read_text())[1]
    name = f'speed-sync-sql-{version}-' + ('macos-arm64' if MAC else 'windows-x64')
    stage = WORK / 'staging' / name
    if stage.exists():
        shutil.rmtree(stage)
    stage.mkdir(parents=True)
    app = build / ('speed-sync-sql.app' if MAC else 'speed-sync-sql.exe')
    with log_stage('Deploy runtime dependencies'):
        deployed = deploy_macos(qt, mysql, app, stage) if MAC else deploy_windows(qt, mysql, app, stage)
    clean_env = {k: v for k, v in os.environ.items() if not k.startswith(('QT_', 'QML', 'DYLD_', 'LD_LIBRARY_PATH', 'SPEED_SYNC_'))}
    clean_env['PATH'] = '/usr/bin:/bin' if MAC else str(Path(os.environ['SystemRoot']) / 'System32')
    check = run(deployed, '--deployment-check', env=clean_env, capture_output=True, text=True, timeout=30)
    report = json.loads(check.stdout)
    if not report.get('mysqlValid') or not report.get('sqliteValid'):
        raise RuntimeError(f'Deployed SQL driver load failed: {report}')
    shutil.copy2(ROOT / 'docs/development/desktop-release.md', stage / 'README.md')
    notices = stage / 'licenses'
    notices.mkdir()
    shutil.copytree(source / 'LICENSES', notices / 'QtBase')
    for item in mysql.glob('*'):
        if item.is_file() and item.name.upper().startswith(('LICENSE', 'COPYING', 'README')):
            shutil.copy2(item, notices / item.name)
    (stage / 'build-info.json').write_text(json.dumps({
        'version': version, 'qt': QT_VERSION, 'mysql': MYSQL_VERSION,
        'system': platform.platform(), 'architecture': platform.machine(),
        'commit': os.environ.get('GITHUB_SHA', output('git', 'rev-parse', 'HEAD').strip()),
        'deploymentCheck': report, 'minimumOSValidation': 'NOT RUN',
        'cleanMachineValidation': 'NOT RUN', 'signedRelease': False,
    }, indent=2) + '\n')
    artifacts = WORK / 'artifacts'
    if artifacts.exists():
        shutil.rmtree(artifacts)
    artifacts.mkdir()
    with log_stage('Package distribution and SHA-256'):
        if MAC:
            archive = artifacts / f'{name}.tar.gz'
            with tarfile.open(archive, 'w:gz') as tar:
                tar.add(stage, arcname=name)
        else:
            archive = Path(shutil.make_archive(str(artifacts / name), 'zip', stage.parent, name))
        digest = sha256(archive)
        (artifacts / (archive.name + '.sha256')).write_text(f'{digest}  {archive.name}\n')
    print(f'Distribution: {archive}', flush=True)


if __name__ == '__main__':
    main()
