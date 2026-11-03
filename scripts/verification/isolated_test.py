"""Give each CTest process its own temporary files, cache and working directory."""
import argparse
import json
import os
import subprocess
import tempfile
import time
from pathlib import Path
from process_scope import contain_current_process
from ui_watchdog import run


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--name', required=True)
    parser.add_argument('--ui-suite', choices=('direct', 'package'))
    parser.add_argument('--ui-timeout', type=float, default=180)
    parser.add_argument('command', nargs=argparse.REMAINDER)
    args = parser.parse_args()
    command = args.command[1:] if args.command[:1] == ['--'] else args.command
    if not command:
        parser.error('Missing test command')
    if args.ui_timeout <= 0:
        parser.error('UI progress timeout must be positive')
    parent = Path(os.environ.get('KACHACAD_VERIFICATION_RUN_DIR', tempfile.gettempdir())) / 'isolated'
    parent.mkdir(parents=True, exist_ok=True)
    work = Path(tempfile.mkdtemp(prefix=args.name + '-', dir=parent))
    env = {k.upper(): v for k, v in os.environ.items()}
    env.update({'TEMP': str(work), 'TMP': str(work), 'TMPDIR': str(work),
                'KACHACAD_DOCUMENT_CACHE_DIR': str(work / 'document-cache'),
                'KACHACAD_SELF_TEST_FIXTURE_DIR': str(Path(__file__).resolve().parents[2] / 'examples')})
    if args.ui_suite and not env.get('KACHACAD_VERIFICATION_RUN_DIR'):
        run_root = work
        if args.ui_suite == 'package':
            # The packager only permits output below repo/out, including plain
            # CTest invocations that do not go through verify.py.
            output_parent = Path(__file__).resolve().parents[2] / 'out/verification'
            output_parent.mkdir(parents=True, exist_ok=True)
            run_root = Path(tempfile.mkdtemp(prefix='ctest-package-', dir=output_parent))
        env['KACHACAD_VERIFICATION_RUN_DIR'] = str(run_root)
    job = contain_current_process()
    start = time.perf_counter()
    exit_code = run(command, work, env, args.ui_suite, args.ui_timeout)
    (work / 'process.json').write_text(json.dumps({'schema': 1, 'name': args.name,
        'seconds': time.perf_counter() - start, 'exit_code': exit_code}), encoding='utf-8')
    return exit_code


if __name__ == '__main__':
    raise SystemExit(main())
