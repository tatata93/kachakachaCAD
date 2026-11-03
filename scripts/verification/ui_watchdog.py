"""Bound each UI case by completed-case progress, independently of suite length."""
import os
import signal
import subprocess
import time
from pathlib import Path


class ProgressWatchdog:
    def __init__(self, path, timeout, now):
        if timeout <= 0:
            raise ValueError('Progress timeout must be positive')
        self.path, self.timeout, self.last_progress = Path(path), timeout, now
        self.completed = self.count()

    def count(self):
        try:
            return self.path.read_bytes().count(b'\n')
        except FileNotFoundError:
            return 0

    def stalled(self, now):
        completed = self.count()
        if completed > self.completed:
            self.completed, self.last_progress = completed, now
        return now - self.last_progress >= self.timeout


def run(command, work, env, ui_suite=None, timeout=180):
    process = subprocess.Popen(command, cwd=work, env=env, start_new_session=os.name != 'nt')
    if not ui_suite:
        return process.wait()
    root = Path(env['KACHACAD_VERIFICATION_RUN_DIR'])
    report = root / ('package/ui-results.json' if ui_suite == 'package' else f'selftest-{process.pid}.json')
    watchdog = ProgressWatchdog(str(report) + '.progress.jsonl', timeout, time.monotonic())
    try:
        while process.poll() is None:
            if watchdog.stalled(time.monotonic()):
                print(f'UI case progress stalled for {timeout:g}s: {watchdog.path}', flush=True)
                return 124
            time.sleep(.1)
        return process.returncode
    finally:
        if os.name != 'nt':
            try:
                os.killpg(process.pid, signal.SIGKILL)
            except ProcessLookupError:
                pass
        elif process.poll() is None:
            process.kill()
        process.wait()
