"""Repeatable incremental build measurements; source bytes are never changed."""
import argparse
import datetime
import hashlib
import json
import os
import re
import subprocess
import sys
import time
from pathlib import Path


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--preset', default='windows-msvc')
    parser.add_argument('--target', default='kachakacha_cad_next')
    parser.add_argument('--parallel', type=int, default=2)
    parser.add_argument('--config', default='Release')
    parser.add_argument('--label', required=True)
    parser.add_argument('--cpp', default='src/apps/cad_next/V2ThickenCommands.cpp')
    parser.add_argument('--header', default='src/next/kachakacha/app/ThickenInputState.h')
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[2]
    dest = root / 'out' / 'verification' / ('benchmark-' + args.label)
    dest.mkdir(parents=True, exist_ok=False)
    env = {k.upper(): v for k, v in os.environ.items()}
    env['CL'] = '/MP' + str(args.parallel) if os.name == 'nt' else env.get('CL', '')
    env['MSBUILDDISABLENODEREUSE'] = '1'
    sys.path.insert(0, str(root / 'scripts'))
    from verification.runner import configuration
    build, _ = configuration(args)
    lock = build / '.verification-lock'
    lock.mkdir()
    (lock / 'owner.json').write_text(json.dumps({'pid': os.getpid(), 'run': str(dest)}), encoding='utf-8')
    records = []
    commit = subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=root, text=True).strip()
    for label, relative in [('warmup', None), ('unchanged', None), ('cpp', args.cpp), ('shared-header', args.header)]:
        path = root / relative if relative else None
        saved = path.stat() if path else None
        digest = hashlib.sha256(path.read_bytes()).hexdigest() if path else None
        start = datetime.datetime.now(datetime.timezone.utc).isoformat()
        tick = time.perf_counter()
        try:
            if path:
                os.utime(path, None)
            log = dest / (label + '.log')
            with log.open('w', encoding='utf-8') as out:
                result = subprocess.run(['cmake', '--build', '--preset', args.preset, '--target', args.target,
                                         '--config', args.config,
                                         '--parallel', str(args.parallel)], cwd=root, env=env, stdout=out, stderr=subprocess.STDOUT)
            elapsed = time.perf_counter() - tick
            text = log.read_text(encoding='utf-8', errors='replace')
            compiled = set(re.findall(r'^\s*([\w.-]+\.cpp)\s*$', text, re.MULTILINE))
            compiled.update(Path(p).name for p in re.findall(r'Building CXX object (\S+\.cpp)\.o', text))
            record = {'stage': label, 'source': relative, 'start': start,
                      'end': datetime.datetime.now(datetime.timezone.utc).isoformat(),
                      'seconds': elapsed, 'exit_code': result.returncode, 'compiled': sorted(compiled),
                      'compiled_count': len(compiled), 'v2_count': sum(p.startswith('V2') for p in compiled)}
            records.append(record)
            (dest / 'result.json').write_text(json.dumps({'schema': 1, 'commit': commit, 'preset': args.preset,
                'target': args.target, 'parallel': args.parallel, 'cache': 'warm incremental; warmup excluded',
                'records': records}, indent=2), encoding='utf-8')
            print(label, f'{elapsed:.3f}s', len(compiled), 'compiled', flush=True)
            if result.returncode:
                (lock / 'owner.json').unlink(); lock.rmdir()
                return result.returncode
        finally:
            if path:
                assert hashlib.sha256(path.read_bytes()).hexdigest() == digest, 'Source bytes changed during benchmark'
                os.utime(path, ns=(saved.st_atime_ns, saved.st_mtime_ns))
    (lock / 'owner.json').unlink(); lock.rmdir()
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
