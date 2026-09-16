"""Failure-path checks for distribution assembly; no Qt install or database needed."""
import importlib.util
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
import zipfile
from unittest.mock import patch

spec = importlib.util.spec_from_file_location('release', Path(__file__).resolve().parents[1] / 'scripts/release.py')
release = importlib.util.module_from_spec(spec)
spec.loader.exec_module(release)


class ReleaseTests(unittest.TestCase):
    def test_extract_zip_uses_python_zipfile(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            archive = root / 'sample.zip'
            with zipfile.ZipFile(archive, 'w') as zf:
                zf.writestr('qtbase/file.txt', 'ok')
            with patch.object(release, 'WORK', root / 'work'), patch.object(release, 'run') as run:
                release.extract(archive)
            run.assert_called_once_with(sys.executable, '-m', 'zipfile', '-e', archive, root / 'work', timeout=release.EXTRACT_TIMEOUT)

    def test_log_stage_reports_failure(self):
        with patch('builtins.print') as output:
            with self.assertRaisesRegex(RuntimeError, 'boom'):
                with release.log_stage('fixture'):
                    raise RuntimeError('boom')
        lines = [call.args[0] for call in output.call_args_list]
        self.assertTrue(lines[0].startswith('[START] fixture'))
        self.assertTrue(lines[1].startswith('[FAIL] fixture'))

    def test_failed_command_is_fatal(self):
        with self.assertRaises(subprocess.CalledProcessError):
            release.run(sys.executable, '-c', 'raise SystemExit(7)')

    def check_mac(self, dependency, with_library=False, rpath='@loader_path/../Frameworks'):
        with tempfile.TemporaryDirectory() as directory:
            app = Path(directory) / 'Demo.app'
            binary = app / 'Contents/MacOS/demo'
            binary.parent.mkdir(parents=True)
            binary.touch()
            if with_library:
                library = app / 'Contents/Frameworks/libfixture.dylib'
                library.parent.mkdir()
                library.touch()
            def inspect(*args):
                if args[0] == 'file':
                    return 'Mach-O 64-bit executable arm64'
                if args[1] == '-l':
                    return f'cmd LC_RPATH\n cmdsize 40\n path {rpath} (offset 12)\n' if rpath else ''
                if args[1] == '-D':
                    return str(args[2]) + ':\n'
                return str(args[2]) + ':\n\t' + dependency + ' (compatibility version 1.0.0)\n'
            with patch.object(release, 'output', side_effect=inspect):
                return release.check_macos_dependencies(app)

    def test_missing_packaged_library_is_fatal(self):
        with self.assertRaisesRegex(RuntimeError, 'Unresolved library'):
            self.check_mac('@rpath/libfixture.dylib')

    def test_development_library_is_fatal_even_if_installed(self):
        with self.assertRaisesRegex(RuntimeError, 'External library'):
            self.check_mac('/opt/homebrew/lib/libfixture.dylib')

    def test_system_library_is_allowed(self):
        self.assertTrue(self.check_mac('/usr/lib/libSystem.B.dylib'))

    def test_packaged_rpath_library_is_allowed(self):
        self.assertTrue(self.check_mac('@rpath/libfixture.dylib', with_library=True))

    def test_present_library_without_reachable_rpath_is_fatal(self):
        with self.assertRaisesRegex(RuntimeError, 'Unresolved library'):
            self.check_mac('@rpath/libfixture.dylib', with_library=True, rpath='')

    def test_development_rpath_is_fatal(self):
        with self.assertRaisesRegex(RuntimeError, 'External rpath'):
            self.check_mac('@rpath/libfixture.dylib', with_library=True, rpath='/opt/qt/lib')

    def test_windows_missing_transitive_library_is_fatal(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            stage = root / 'stage'; stage.mkdir()
            app = root / 'demo.exe'; app.touch()
            mysql = root / 'mysql'; (mysql / 'lib').mkdir(parents=True)
            (mysql / 'lib/libmysql.dll').touch()
            plugin = root / 'driver/plugins/sqldrivers/qsqlmysql.dll'
            plugin.parent.mkdir(parents=True); plugin.touch()
            def inspect(*args):
                name = Path(args[-1]).name
                return '\n    libmysql.dll\n' if name == 'qsqlmysql.dll' else '\n    missing-ssl.dll\n' if name == 'libmysql.dll' else ''
            with patch.object(release, 'WORK', root), patch.object(release, 'run'), patch.object(release, 'output', side_effect=inspect), patch.dict(release.os.environ, {'SystemRoot': str(root / 'system'), 'VCToolsRedistDir': ''}):
                with self.assertRaisesRegex(RuntimeError, 'missing-ssl.dll'):
                    release.deploy_windows(root / 'qt', mysql, app, stage)


if __name__ == '__main__':
    unittest.main()
