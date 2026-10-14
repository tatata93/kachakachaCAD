"""Give each CTest process its own temporary files, cache and working directory."""
import argparse
import json
import os
import subprocess
import tempfile
import time
from pathlib import Path


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--name', required=True)
    parser.add_argument('command', nargs=argparse.REMAINDER)
    args = parser.parse_args()
    command = args.command[1:] if args.command[:1] == ['--'] else args.command
    if not command:
        parser.error('Missing test command')
    parent = Path(os.environ.get('KACHACAD_VERIFICATION_RUN_DIR', tempfile.gettempdir())) / 'isolated'
    parent.mkdir(parents=True, exist_ok=True)
    work = Path(tempfile.mkdtemp(prefix=args.name + '-', dir=parent))
    env = {k.upper(): v for k, v in os.environ.items()}
    env.update({'TEMP': str(work), 'TMP': str(work), 'TMPDIR': str(work),
                'KACHACAD_DOCUMENT_CACHE_DIR': str(work / 'document-cache')})
    start = time.perf_counter()
    result = subprocess.run(command, cwd=work, env=env)
    (work / 'process.json').write_text(json.dumps({'schema': 1, 'name': args.name,
        'seconds': time.perf_counter() - start, 'exit_code': result.returncode}), encoding='utf-8')
    return result.returncode


if __name__ == '__main__':
    raise SystemExit(main())
