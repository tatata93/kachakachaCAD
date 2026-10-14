"""Verification identities and strict result checks, independent of the runner."""
import hashlib
import json
import math
import os
import platform
import re
import shutil
import subprocess
import xml.etree.ElementTree as ET
from pathlib import Path


def digest(path):
    with Path(path).open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def source_identity(root):
    tracked = subprocess.check_output(['git', 'ls-files', '-z'], cwd=root).decode('utf-8').split('\0')
    # Untracked build inputs must also invalidate proofs. Ignore only normal generated assets.
    untracked = subprocess.check_output(['git', 'ls-files', '--others', '--exclude-standard', '-z'], cwd=root).decode('utf-8').split('\0')
    prefixes = ('src/', 'tests/', 'tests_v2/', 'scripts/', 'cmake/', 'tools/', 'samples/', 'examples/', 'legal/', 'assets/', 'resources/', 'docs/manual/', 'docs/v2/')
    exact = ('CMakeLists.txt', 'CMakePresets.json', 'AGENTS.md', 'docs/manual.html', 'local.cmake', 'LICENSE', 'COPYRIGHT')
    selected = sorted(set(p for p in tracked + [p for p in untracked if p.startswith(prefixes) or p in exact]
                          if p and not p.startswith('docs/development/') and p != 'docs/refactoring-plan.md'))
    hashes = {p: digest(root / p) if (root / p).is_file() else None for p in selected}
    # local.cmake is ignored by Git, but still controls compilation.
    if (root / 'local.cmake').is_file():
        hashes['local.cmake'] = digest(root / 'local.cmake')
    return {'commit': subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=root, text=True).strip(),
            'files': hashes, 'sha256': hashlib.sha256(json.dumps(hashes, sort_keys=True).encode()).hexdigest()}


def environment_identity(environment=None):
    environment = environment or os.environ
    keys = ('CL', 'CXXFLAGS', 'CFLAGS', 'KACHACAD_QT_ROOT', 'QT_ROOT_DIR', 'VCPKG_ROOT',
            'CMAKE_TOOLCHAIN_FILE', 'QT_SCALE_FACTOR', 'QT_QPA_PLATFORM')
    keys = sorted(set(keys) | {k for k in environment if k.startswith('QT_')})
    tools = {}
    for name in ('cmake', 'ctest'):
        tools[name] = {'version': subprocess.check_output([name, '--version'], text=True).splitlines()[0],
                       'executable_sha256': digest(shutil.which(name))}
    return {'platform': platform.platform(), 'python': platform.python_version(),
            'cpu_count': os.cpu_count(), 'tools': tools,
            'variables': {k: environment.get(k) for k in keys}}


def validate_ctest(path, expected, allowed_skips=()):
    cases = list(ET.parse(path).getroot().iter('testcase'))
    names = [c.attrib['name'] for c in cases]
    if not expected or len(names) != len(set(names)) or set(names) != set(expected):
        raise ValueError('CTest inventory mismatch or zero/duplicate execution')
    for case in cases:
        if case.attrib.get('status', 'run') != 'run' and case.find('skipped') is None:
            raise ValueError('CTest case did not run: ' + case.attrib['name'])
        if case.find('failure') is not None or case.find('error') is not None:
            raise ValueError('CTest failure: ' + case.attrib['name'])
        if case.find('skipped') is not None and case.attrib['name'] not in allowed_skips:
            raise ValueError('Unexpected CTest skip: ' + case.attrib['name'])
    return {'total': len(cases), 'skipped': [c.attrib['name'] for c in cases if c.find('skipped') is not None]}


def validate_ui(report, expected):
    names = [r['name'] for r in report['cases']]
    if not expected or names != list(expected) or len(names) != len(set(names)):
        raise ValueError('UI inventory mismatch or zero/duplicate execution')
    if report.get('failed') != 0 or report.get('total') != len(names):
        raise ValueError('UI summary mismatch or failure')
    for case in report['cases']:
        ms = case.get('duration_ms')
        if case.get('status') != 'passed' or not isinstance(ms, (int, float)) or not math.isfinite(ms) or ms < 0:
            raise ValueError('Invalid UI result/timing: ' + case['name'])
    return len(names)


def artifact_identity(directory):
    return {p.relative_to(directory).as_posix(): digest(p) for p in sorted(directory.rglob('*')) if p.is_file()}


def validate_proof(proof, source, settings, environment, artifacts):
    if proof.get('status') != 'passed' or proof.get('mode') != 'full':
        raise ValueError('Only successful full verification is publishable')
    if proof['source']['sha256'] != source['sha256']:
        raise ValueError('Source changed since verification')
    if proof['settings'] != settings or proof['environment'] != environment or proof['artifacts'] != artifacts:
        raise ValueError('Settings, environment or generated artifacts changed')
    if not proof.get('preflight', {}).get('total') or not proof.get('ctest', {}).get('total'):
        raise ValueError('Full verification results are missing')
    if proof['ctest']['total'] != len(proof.get('expected_ctest', [])) or proof['ctest'].get('skipped'):
        raise ValueError('Full verification inventory is incomplete')
    if not proof.get('ui_cases') or any(count <= 0 for count in proof['ui_cases']):
        raise ValueError('Application/package UI results missing')
    if not proof.get('steps') or any(step.get('exit_code') != 0 for step in proof['steps']):
        raise ValueError('Required verification step did not succeed')
    steps = {step.get('name') for step in proof['steps']}
    required = {'configure', 'inventory', 'ui-inventory', 'preflight', 'tests', 'legacy-self-test'}
    if not required <= steps or not any(name and name.startswith('build-') for name in steps):
        raise ValueError('Required verification steps are missing')


def build_environment(build):
    files = list((build / 'CMakeFiles').glob('*/CMakeCXXCompiler.cmake'))
    cache = (build / 'CMakeCache.txt').read_text(encoding='utf-8', errors='replace')
    compiler = re.search(r'^CMAKE_CXX_COMPILER:[^=]+=(.+)$', cache, re.MULTILINE)
    if compiler and Path(compiler[1]).is_file():
        files.append(Path(compiler[1]))
    for key in ('Qt6_DIR', 'OpenCASCADE_DIR'):
        value = re.search(r'^' + key + r':[^=]+=(.+)$', cache, re.MULTILINE)
        if value:
            files.extend(Path(value[1]).glob('*.cmake'))
    return {str(p.resolve()): digest(p) for p in sorted(set(files)) if p.is_file()}
