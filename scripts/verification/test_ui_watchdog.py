"""Real-process regressions for progress timeouts and Windows job cleanup."""
import ctypes
import json
import os
import subprocess
import sys
import tempfile
import time
import unittest
from pathlib import Path

from verification.ui_watchdog import ProgressWatchdog


class UiWatchdogTests(unittest.TestCase):
    def test_progress_does_not_turn_total_duration_into_a_case_timeout(self):
        with tempfile.TemporaryDirectory() as work:
            p = Path(work) / 'progress'
            p.write_bytes(b'old\n')
            watch = ProgressWatchdog(p, 10, 0)
            self.assertFalse(watch.stalled(9))
            with p.open('ab') as out:
                out.write(b'next\n')
            self.assertFalse(watch.stalled(9))
            self.assertFalse(watch.stalled(18))
            self.assertTrue(watch.stalled(19))

    def test_partial_or_unchanged_progress_does_not_reset_the_deadline(self):
        with tempfile.TemporaryDirectory() as work:
            p = Path(work) / 'progress'
            watch = ProgressWatchdog(p, 10, 0)
            p.write_bytes(b'partial')
            self.assertFalse(watch.stalled(9))
            p.touch()
            self.assertTrue(watch.stalled(10))
            with self.assertRaises(ValueError):
                ProgressWatchdog(p, 0, 0)

    def launch(self, root, source, timeout=.6, suite='direct', use_env_root=True):
        child = root / 'fixture.py'
        child.write_text(source, encoding='utf-8')
        env = dict(os.environ, KACHACAD_VERIFICATION_RUN_DIR=str(root))
        if not use_env_root:
            env.pop('KACHACAD_VERIFICATION_RUN_DIR', None)
        return subprocess.Popen([sys.executable, str(Path(__file__).with_name('isolated_test.py')),
            '--name', 'watchdog-fixture', '--ui-suite', suite, '--ui-timeout', str(timeout),
            '--', sys.executable, str(child), str(root / 'pids.json')], env=env,
            stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)

    def test_real_progress_survives_and_nonzero_exit_is_not_hidden(self):
        with tempfile.TemporaryDirectory() as work:
            root = Path(work)
            p = self.launch(root, 'import os,time\nfrom pathlib import Path\n'
                'p=Path(os.environ["KACHACAD_VERIFICATION_RUN_DIR"])/f"selftest-{os.getpid()}.json.progress.jsonl"\n'
                'for i in range(12):\n p.open("ab").write(b"completed\\n");time.sleep(.1)\n')
            output, _ = p.communicate(timeout=10)
            self.assertEqual(p.returncode, 0, output)
            p = self.launch(root, 'raise SystemExit(7)\n')
            output, _ = p.communicate(timeout=10)
            self.assertEqual(p.returncode, 7, output)

    def test_stalled_child_is_failure_and_is_terminated(self):
        with tempfile.TemporaryDirectory() as work:
            root = Path(work)
            p = self.launch(root, 'import time\ntime.sleep(60)\n', .4)
            output, _ = p.communicate(timeout=10)
            self.assertEqual(p.returncode, 124, output)
            record = json.loads(next(root.glob('isolated/*/process.json')).read_text())
            self.assertEqual(record['exit_code'], 124)
            self.assertIn('progress stalled', output)

    def test_invalid_timeout_does_not_launch_the_child(self):
        with tempfile.TemporaryDirectory() as work:
            root = Path(work)
            p = self.launch(root, 'import sys\nfrom pathlib import Path\nPath(sys.argv[1]).write_text("launched")\n', 0)
            output, _ = p.communicate(timeout=10)
            self.assertEqual(p.returncode, 2, output)
            self.assertFalse((root / 'pids.json').exists())

    def test_plain_ctest_package_uses_a_unique_repo_output_root(self):
        with tempfile.TemporaryDirectory() as work:
            root = Path(work)
            roots = []
            for _ in range(2):
                p = self.launch(root, 'import os,sys\nfrom pathlib import Path\n'
                    'Path(sys.argv[1]).write_text(os.environ["KACHACAD_VERIFICATION_RUN_DIR"])\n',
                    suite='package', use_env_root=False)
                output, _ = p.communicate(timeout=10)
                self.assertEqual(p.returncode, 0, output)
                chosen = Path((root / 'pids.json').read_text()).resolve()
                self.assertTrue(chosen.is_relative_to(Path(__file__).resolve().parents[2] / 'out'))
                self.assertTrue(chosen.name.startswith('ctest-package-'))
                roots.append(chosen)
            self.assertNotEqual(*roots)

    @unittest.skipUnless(os.name == 'nt', 'Windows Job Object contract')
    def test_killing_wrapper_also_kills_child_and_grandchild(self):
        from ctypes import wintypes as w
        kernel = ctypes.WinDLL('kernel32', use_last_error=True)
        kernel.OpenProcess.argtypes = [w.DWORD, w.BOOL, w.DWORD]
        kernel.OpenProcess.restype = w.HANDLE
        kernel.WaitForSingleObject.argtypes = [w.HANDLE, w.DWORD]
        kernel.WaitForSingleObject.restype = w.DWORD
        kernel.CloseHandle.argtypes = [w.HANDLE]

        def dead(pid):
            handle = kernel.OpenProcess(0x100000, False, pid)
            if not handle:
                return True
            try:
                return kernel.WaitForSingleObject(handle, 0) == 0
            finally:
                kernel.CloseHandle(handle)

        with tempfile.TemporaryDirectory() as work:
            root = Path(work)
            p = self.launch(root, 'import json,os,subprocess,sys,time\nfrom pathlib import Path\n'
                'child=subprocess.Popen([sys.executable,"-c","import time;time.sleep(60)"])\n'
                'Path(sys.argv[1]).write_text(json.dumps([os.getpid(),child.pid]))\ntime.sleep(60)\n', 20)
            ids = []
            try:
                deadline = time.monotonic() + 10
                while not (root / 'pids.json').exists() and time.monotonic() < deadline:
                    time.sleep(.02)
                ids = json.loads((root / 'pids.json').read_text())
                self.assertTrue(all(not dead(pid) for pid in ids))
                p.kill()
                p.communicate(timeout=10)
                deadline = time.monotonic() + 5
                while not all(dead(pid) for pid in ids) and time.monotonic() < deadline:
                    time.sleep(.02)
                self.assertTrue(all(dead(pid) for pid in ids), 'Wrapper left a verification descendant')
            finally:
                if p.poll() is None:
                    p.kill(); p.communicate(timeout=10)
                for pid in ids:
                    if not dead(pid):
                        os.kill(pid, 15)
