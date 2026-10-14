import argparse
import datetime
import json
import os
import re
import shutil
import subprocess
import sys
import time
import uuid
from pathlib import Path

from .evidence import (artifact_identity, build_environment, digest, environment_identity, source_identity,
                       validate_ctest, validate_proof, validate_ui)
from .selection import PREFLIGHT, select_plan


ROOT = Path(__file__).resolve().parents[2]


def now():
    return datetime.datetime.now(datetime.timezone.utc).isoformat()


def child_environment(parallel, directory):
    env = {k.upper(): v for k, v in os.environ.items()}
    for key in list(env):
        if key.startswith(('KACHACAD_SELF_TEST_', 'KACHACAD_THICKEN_TEST_', 'KACHACAD_SURFACE_TEST_',
                           'KACHACAD_HEAVY_', 'KACHACAD_PRACTICAL_', 'KACHACAD_APPROX_UI_')) or ('KACHACAD_' in key and '_OWNER_' in key) or key in ('KACHACAD_OWNER_OPEN_FILE', 'KACHACAD_JOB_REPORT_DIR', 'KACHACAD_PROJECTION_SCREENSHOT'):
            env.pop(key)
    if os.name == 'nt':
        env['CL'] = '/MP' + str(parallel)
        env['MSBUILDDISABLENODEREUSE'] = '1'
    env.update({'QT_QPA_PLATFORM': 'offscreen', 'KACHACAD_VERIFICATION_RUN_DIR': str(directory),
                'KACHACAD_DOCUMENT_CACHE_DIR': str(directory / 'document-cache')})
    temp = directory / 'tmp'
    temp.mkdir(parents=True, exist_ok=True)
    env.update({'TEMP': str(temp), 'TMP': str(temp), 'TMPDIR': str(temp)})
    return env


class Run:
    def __init__(self, args, directory):
        self.args, self.directory = args, directory
        self.env = child_environment(args.parallel, directory)
        self.steps = []

    def command(self, name, arguments):
        start, tick = now(), time.perf_counter()
        log = self.directory / (name + '.log')
        with log.open('w', encoding='utf-8') as out:
            result = subprocess.run([str(a) for a in arguments], cwd=ROOT, env=self.env,
                                    stdout=out, stderr=subprocess.STDOUT)
        step = {'name': name, 'command': list(map(str, arguments)), 'start': start, 'end': now(),
                'seconds': time.perf_counter() - tick, 'exit_code': result.returncode, 'log': log.name}
        self.steps.append(step)
        print(name, f'{step["seconds"]:.2f}s', 'exit', result.returncode, flush=True)
        if result.returncode:
            raise RuntimeError(name + ' failed; inspect ' + str(log))
        return log

    def inventory(self, build, config):
        log = self.command('inventory', ['ctest', '--test-dir', build, '-C', config, '--show-only=json-v1'])
        return json.loads(log.read_text(encoding='utf-8'))['tests']

    def build(self, targets=()):
        args = ['cmake', '--build', '--preset', self.args.preset, '--config', self.args.config, '--parallel', str(self.args.parallel)]
        if targets:
            args += ['--target', *sorted(set(targets))]
        return self.command('build-' + str(len(self.steps)), args)

    def ctest(self, build, config, names, label):
        if not names:
            raise ValueError('Refusing zero-test execution')
        xml = self.directory / (label + '.xml')
        regex = '^(' + '|'.join(re.escape(n) for n in names) + ')$'
        self.command(label, ['ctest', '--test-dir', build, '-C', config, '--output-on-failure',
            '--no-tests=error', '--parallel', str(self.args.parallel), '--output-junit', xml, '-R', regex])
        return validate_ctest(xml, names)


def configuration(args):
    presets = json.loads((ROOT / 'CMakePresets.json').read_text(encoding='utf-8'))
    preset = next(p for p in presets['configurePresets'] if p['name'] == args.preset)
    binary = preset.get('binaryDir')
    if not binary and preset.get('inherits'):
        binary = next(p['binaryDir'] for p in presets['configurePresets'] if p['name'] == preset['inherits'])
    build = Path(binary.replace('${sourceDir}', str(ROOT)))
    config = args.config
    return build, config


def changed_inputs(base):
    requested = []
    if base:
        paths = subprocess.check_output(['git', 'diff', '--name-only', '-z', base, '--'], cwd=ROOT).decode('utf-8').split('\0')
        paths += subprocess.check_output(['git', 'ls-files', '--others', '--exclude-standard', '-z'], cwd=ROOT).decode('utf-8').split('\0')
        inputs = source_identity(ROOT)['files']
        requested = [p for p in paths if p in inputs]
    latest = ROOT / 'out/verification/latest-full.json'
    if not latest.is_file():
        return ['<no verified source baseline>']
    proof = json.loads(Path(json.loads(latest.read_text())['evidence']).read_text(encoding='utf-8'))
    if proof.get('status') != 'passed' or proof.get('mode') != 'full':
        return ['<invalid verified source baseline>']
    previous, current = proof['source']['files'], source_identity(ROOT)['files']
    return sorted(set(requested) | {p for p in set(previous) | set(current) if previous.get(p) != current.get(p)})


def build_targets(inventory, names):
    targets = []
    for test in inventory:
        if test['name'] not in names:
            continue
        for word in test.get('command', []):
            name = Path(word).stem
            if name.startswith('kachakacha_'):
                targets.append(name)
    return targets


def ui_cases(run, exe):
    path = run.directory / 'ui-inventory.json'
    run.command('ui-inventory', [exe, '--self-test-list', path])
    inventory = json.loads(path.read_text(encoding='utf-8'))['inventory']
    names = [c['name'] for c in inventory]
    if not names or len(names) != len(set(names)):
        raise ValueError('UI inventory empty or duplicate')
    return inventory


def baseline_matches(baseline, environment, dependencies, settings):
    return (baseline.get('environment') == environment
            and baseline.get('build_environment') == dependencies
            and baseline.get('settings') == settings)


def require_application_coverage(preset, inventory, exe=None, legacy=None):
    if preset.endswith('-core'):
        return
    required = {'v2_cad_next_smoke'}
    if os.name == 'nt':
        required.add('v2_package_zip')
    missing = required - {test['name'] for test in inventory}
    if missing:
        raise ValueError('GUI preset requires application/package tests: ' + ', '.join(sorted(missing)))
    if exe is not None and not exe.is_file():
        raise ValueError('GUI preset requires application executable')
    if os.name == 'nt' and legacy is not None and not legacy.is_file():
        raise ValueError('GUI preset requires legacy executable')


def verify(args, directory):
    run = Run(args, directory)
    proof = {'schema': 1, 'mode': args.mode, 'status': 'running', 'start': now(), 'steps': run.steps}
    result_path = directory / 'result.json'
    lock = None
    try:
        build, config = configuration(args)
        build.mkdir(parents=True, exist_ok=True)
        lock = build / '.verification-lock'
        lock.mkdir()  # One scheduler per build tree. Never break another run's lock automatically.
        (lock / 'owner.json').write_text(json.dumps({'pid': os.getpid(), 'run': str(directory), 'start': now()}), encoding='utf-8')
        proof['source'] = source_identity(ROOT)
        proof['environment'] = environment_identity(run.env)
        if args.mode == 'full' and os.name == 'nt':
            run.command('doctor', ['powershell', '-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', ROOT / 'scripts/doctor.ps1'])
        if args.mode != 'plan':
            run.command('configure', ['cmake', '--preset', args.preset])
        inventory = run.inventory(build, config)
        if args.mode in ('full', 'focused'):
            require_application_coverage(args.preset, inventory)
        rules = json.loads((ROOT / 'scripts/verification/scopes.json').read_text(encoding='utf-8'))
        plan = select_plan(changed_inputs(args.base), inventory, rules)
        latest = ROOT / 'out/verification/latest-full.json'
        settings = {'preset': args.preset, 'config': config, 'parallel': args.parallel,
                    'cache_sha256': digest(build / 'CMakeCache.txt')}
        if args.mode == 'focused' and latest.is_file():
            baseline = json.loads(Path(json.loads(latest.read_text())['evidence']).read_text(encoding='utf-8'))
            if not baseline_matches(baseline, proof['environment'], build_environment(build), settings):
                plan.update({'effective_full': True, 'reason': 'Verification configuration/environment changed'})
        if args.mode == 'full':
            plan['effective_full'] = True
            plan['ctest'] = [t['name'] for t in inventory]
        if args.mode == 'preflight':
            plan.update({'effective_full': False, 'ctest': list(PREFLIGHT), 'ui_suites': []})
        proof['plan'] = plan
        proof['settings'] = settings
        result_path.write_text(json.dumps(proof, ensure_ascii=False, indent=2), encoding='utf-8')
        if args.mode == 'plan':
            proof['status'] = 'planned'
            return 0
        run.build(build_targets(inventory, PREFLIGHT))
        proof['preflight'] = run.ctest(build, config, list(PREFLIGHT), 'preflight')
        if args.mode == 'preflight':
            if proof['source']['sha256'] != source_identity(ROOT)['sha256']:
                raise ValueError('Source changed during preflight')
            proof['status'] = 'passed'
            return 0
        full = plan['effective_full']
        if full:
            print('Full coverage:', plan['reason'], flush=True)
            proof['mode'] = 'full'
            if os.name == 'nt' and not any(step['name'] == 'doctor' for step in run.steps):
                run.command('doctor', ['powershell', '-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', ROOT / 'scripts/doctor.ps1'])
            run.build()
        else:
            run.build(build_targets(inventory, plan['ctest']) + (['kachakacha_cad_next'] if plan['ui_suites'] else []))
        exe = build / config / 'kachakacha_cad_next.exe' if os.name == 'nt' else build / 'kachakacha_cad_next'
        legacy = build / config / 'kachakacha_cad.exe' if os.name == 'nt' else build / 'kachakacha_cad'
        if full:
            require_application_coverage(args.preset, inventory, exe, legacy)
        ui = ui_cases(run, exe) if exe.is_file() and (full or plan['ui_suites']) else []
        names = [t['name'] for t in inventory] if full else plan['ctest']
        proof['expected_ctest'] = names
        proof['ui_inventory'] = ui
        proof['build_environment'] = build_environment(build)
        proof['ctest'] = run.ctest(build, config, names, 'tests')
        if not full and plan['ui_suites']:
            unknown = set(plan['ui_suites']) - {c['suite'] for c in ui}
            if unknown:
                raise ValueError('UI suite missing: ' + ', '.join(sorted(unknown)))
            selected = [c['name'] for c in ui if c['suite'] in plan['ui_suites']]
            path = directory / 'selected-ui.json'
            path.write_text(json.dumps(selected, ensure_ascii=False), encoding='utf-8')
            report_path = directory / 'focused-ui.json'
            run.command('focused-ui', [exe, '--self-test', '--self-test-cases', path, '--self-test-report', report_path])
            proof['ui_cases'] = validate_ui(json.loads(report_path.read_text(encoding='utf-8')), selected)
        if full and ui:
            reports = list(directory.glob('selftest-*.json'))
            package_report = directory / 'package/ui-results.json'
            if os.name == 'nt':
                reports.append(package_report)
            if len(reports) != (2 if os.name == 'nt' else 1):
                raise ValueError('Required application/package UI reports missing')
            proof['ui_cases'] = [validate_ui(json.loads(p.read_text(encoding='utf-8')), [c['name'] for c in ui]) for p in reports]
        if full and ui and os.name == 'nt' and not legacy.is_file():
            raise ValueError('Mandatory legacy self-test executable missing')
        if full and legacy.is_file():
            run.command('legacy-self-test', [sys.executable, ROOT / 'scripts/verification/isolated_test.py',
                '--name', 'legacy-final', '--', legacy, '--self-test'])
        if proof['source']['sha256'] != source_identity(ROOT)['sha256']:
            raise ValueError('Source changed during verification')
        if proof['build_environment'] != build_environment(build) or proof['settings']['cache_sha256'] != digest(build / 'CMakeCache.txt'):
            raise ValueError('Build configuration/dependencies changed during verification')
        if full and os.name == 'nt' and ui:
            package = directory / 'package'
            if digest(exe) != digest(package / exe.name):
                raise ValueError('Built and deployed application differ')
            proof['artifacts'] = artifact_identity(package)
            proof['zip_sha256'] = digest(directory / 'package.zip')
        proof['status'] = 'passed'
        if full:
            (ROOT / 'out/verification/latest-full.json').write_text(json.dumps({'evidence': str(result_path)}), encoding='utf-8')
        return 0
    except Exception as error:
        proof.update({'status': 'failed', 'error': str(error)})
        print(str(error), file=sys.stderr)
        return 1
    finally:
        proof['end'] = now()
        result_path.write_text(json.dumps(proof, ensure_ascii=False, indent=2), encoding='utf-8')
        if lock and (lock / 'owner.json').is_file():
            owner = json.loads((lock / 'owner.json').read_text())
            if owner.get('pid') == os.getpid():
                (lock / 'owner.json').unlink()
                lock.rmdir()
        print('Evidence:', result_path, flush=True)


def publish(args):
    path = Path(args.evidence).resolve()
    proof = json.loads(path.read_text(encoding='utf-8'))
    build, _ = configuration(args)
    settings = {'preset': args.preset, 'config': args.config, 'parallel': args.parallel,
                'cache_sha256': digest(build / 'CMakeCache.txt')}
    package = path.parent / 'package'
    validate_proof(proof, source_identity(ROOT), settings, environment_identity(child_environment(args.parallel, path.parent)), artifact_identity(package))
    if proof['build_environment'] != build_environment(build):
        raise ValueError('Compiler/dependency identity changed')
    validate_ctest(path.parent / 'preflight.xml', list(PREFLIGHT))
    validate_ctest(path.parent / 'tests.xml', proof['expected_ctest'])
    reports = list(path.parent.glob('selftest-*.json')) + [package / 'ui-results.json']
    if len(reports) != 2:
        raise ValueError('Application/package UI proof missing')
    for report in reports:
        validate_ui(json.loads(report.read_text(encoding='utf-8')), [c['name'] for c in proof['ui_inventory']])
    if digest(path.parent / 'package.zip') != proof['zip_sha256']:
        raise ValueError('Verified ZIP changed')
    destination = ROOT / 'out/releases' / path.parent.name
    destination.mkdir(parents=True, exist_ok=False)
    shutil.copytree(package, destination / 'package')
    shutil.copy2(path.parent / 'package.zip', destination / 'package.zip')
    shutil.copy2(path, destination / 'verification.json')
    if artifact_identity(destination / 'package') != proof['artifacts'] or digest(destination / 'package.zip') != proof['zip_sha256']:
        raise ValueError('Published copy differs from verified bytes')
    if source_identity(ROOT)['sha256'] != proof['source']['sha256']:
        raise ValueError('Source changed during publication')
    print('Verified release:', destination)
    return 0


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('mode', choices=('plan', 'preflight', 'focused', 'full', 'publish'))
    parser.add_argument('--preset', default='windows-msvc' if os.name == 'nt' else 'linux-core')
    parser.add_argument('--config', default='Release')
    parser.add_argument('--parallel', type=int, default=2)
    parser.add_argument('--base')
    parser.add_argument('--run-id')
    parser.add_argument('--evidence')
    parser.add_argument('--push', action='store_true', help='Push already committed tracked inputs after successful full verification')
    args = parser.parse_args()
    if args.parallel < 1:
        parser.error('parallel must be positive')
    if args.mode == 'publish':
        if not args.evidence:
            parser.error('publish needs --evidence')
        try:
            return publish(args)
        except Exception as error:
            print('Publication refused:', error, file=sys.stderr)
            return 1
    run_id = args.run_id or datetime.datetime.now(datetime.timezone.utc).strftime('%Y%m%dT%H%M%SZ-') + uuid.uuid4().hex[:8]
    if not re.fullmatch(r'[A-Za-z0-9_-]+', run_id):
        parser.error('Unsafe run ID')
    directory = ROOT / 'out/verification' / run_id
    directory.mkdir(parents=True, exist_ok=False)
    code = verify(args, directory)
    if code == 0 and args.push:
        proof = json.loads((directory / 'result.json').read_text(encoding='utf-8'))
        if proof.get('mode') != 'full' or proof.get('status') != 'passed':
            print('Git push requires full verification', file=sys.stderr)
            return 1
        tracked = set(subprocess.check_output(['git', 'ls-files', '-z'], cwd=ROOT).decode('utf-8').split('\0'))
        if subprocess.run(['git', 'diff', '--quiet', 'HEAD'], cwd=ROOT).returncode or any(
                p not in tracked and p != 'local.cmake' for p in proof['source']['files']):
            print('Commit owned changes first; automatic staging/committing is prohibited', file=sys.stderr)
            return 1
        if source_identity(ROOT)['sha256'] != proof['source']['sha256']:
            print('Source changed before Git push', file=sys.stderr)
            return 1
        return subprocess.run(['git', 'push'], cwd=ROOT).returncode
    return code
