"""Exercise the real executable with isolated configuration and no network modules."""
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
BINARY = ROOT / 'cupidfetch'


class CliTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix='cupidfetch-cli-')
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.env = os.environ.copy()
        self.env.update(HOME=str(self.root), XDG_CONFIG_HOME=str(self.root / 'config'),
                        USER='test-user')

    def configure(self, modules, home_fallback=False):
        directory = self.root / '.config' if home_fallback else self.root / 'config'
        directory = directory / 'cupidfetch'
        directory.mkdir(parents=True, exist_ok=True)
        (directory / 'cupidfetch.conf').write_text(f'modules = {modules}\n')

    def run_fetch(self, binary=BINARY, force=True):
        args = [str(binary), '--json']
        if force:
            args.extend(['--force-distro', 'Ubuntu'])
        result = subprocess.run(args, env=self.env, cwd=self.root, text=True,
                                capture_output=True, timeout=10)
        self.assertEqual(result.returncode, 0, result.stderr)
        return json.loads(result.stdout), result

    def test_environment_values_are_literal_strings(self):
        self.configure('username shell de wm display_server')
        value = 'literal-%s-%d-%%-"quoted"'
        self.env.update(USER=value, SHELL='/bin/' + value, XDG_CURRENT_DESKTOP=value,
                        WINDOWMANAGER='/bin/' + value, XDG_SESSION_TYPE=value)
        data, _ = self.run_fetch()
        for key in ('username', 'shell', 'de', 'wm', 'display_server'):
            with self.subTest(key=key):
                self.assertEqual(data[key], value)

    def test_redirected_text_output_exits(self):
        self.configure("username")
        result = subprocess.run([str(BINARY), "--force-distro", "Ubuntu"],
                                env=self.env, cwd=self.root, capture_output=True,
                                text=True, timeout=10)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("test-user", result.stdout)
        self.assertNotIn("\x1b[H\x1b[J", result.stdout)

    def test_performance_rejects_invalid_budgets(self):
        for budget in ("nan", "NaN", "inf", "-inf", "0", "-1", "garbage"):
            with self.subTest(budget=budget):
                env = dict(self.env, CUPIDFETCH_PERF_MAX_MEAN_MS=budget)
                result = subprocess.run([str(ROOT / "tests/bin/test_perf")],
                                        env=env, cwd=self.root, capture_output=True,
                                        text=True, timeout=10)
                self.assertNotEqual(result.returncode, 0)
                self.assertIn("Invalid CUPIDFETCH_PERF_MAX_MEAN_MS", result.stderr)

    def test_empty_xdg_uses_home(self):
        self.configure('username', home_fallback=True)
        self.env['XDG_CONFIG_HOME'] = ''
        data, _ = self.run_fetch()
        self.assertEqual(set(data), {'user_host', 'username'})

    def test_distro_discovery_keeps_stdout_json(self):
        self.configure('distro')
        install = self.root / 'installed'
        (install / 'data').mkdir(parents=True)
        (install / 'data' / 'distros.def').write_text('/* auto added */\n')
        binary = install / 'cupidfetch'
        shutil.copy2(BINARY, binary)
        data, result = self.run_fetch(binary, force=False)
        self.assertIn('distro', data)
        self.assertIn('Auto-updated', result.stderr)
        self.assertNotIn('Auto-updated', result.stdout)


if __name__ == '__main__':
    unittest.main()
