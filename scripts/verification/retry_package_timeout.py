"""Retry only a package timeout; retain exact-source passed coverage and raw attempts."""
import argparse, copy, datetime, json, shutil, sys, xml.etree.ElementTree as ET
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[2]))
from scripts.verification.runner import Run, ROOT, configuration, child_environment, now
from scripts.verification.selection import PREFLIGHT
from scripts.verification.evidence import (source_identity, environment_identity, build_environment,
    digest, validate_ctest, validate_ui, artifact_identity, validate_proof)

if sys.flags.optimize:
    raise RuntimeError('Evidence recovery requires Python assertions enabled; do not use -O')


def binary_snapshot(build,config,start):
    files=[p for p in (build/config).iterdir() if p.suffix.lower() in ('.exe','.dll')]
    assert files and all(p.stat().st_mtime<=start for p in files), 'A build executable/runtime changed after original tests began'
    return {p.name:digest(p) for p in files}


def audit_union(proof,directory):
    reused=proof['reused_from'];original=Path(reused['evidence'])
    assert digest(original)==reused['sha256']
    initial=directory/'tests-initial.xml';retry=directory/'retry-package.xml'
    assert digest(initial)==reused['ctestXmlSha256']==digest(original.parent/'tests.xml')
    source=json.loads(original.read_text(encoding='utf-8'))
    assert source['source']['sha256']==proof['source']['sha256'] and source['expected_ctest']==proof['expected_ctest']
    prior=checked_cases(initial,proof['expected_ctest'])
    replacement=checked_cases(retry,['v2_package_zip'],True)[0]
    actual=checked_cases(directory/'tests.xml',proof['expected_ctest'],True)
    def canonical(case):
        result=copy.deepcopy(case);result.tail=None;return ET.tostring(result)
    expected={c.get('name'):canonical(replacement if c.get('name')=='v2_package_zip' else c) for c in prior}
    assert expected=={c.get('name'):canonical(c) for c in actual}
    assert reused['passedCasesReused']==len(prior)-1


def checked_cases(path, expected, retry=False):
    cases=list(ET.parse(path).getroot().iter('testcase'))
    assert cases and len(cases)==len(expected) and {c.get('name') for c in cases}==set(expected)
    assert len({c.get('name') for c in cases})==len(cases)
    for c in cases:
        assert c.find('skipped') is None and c.find('error') is None
        failure=c.find('failure')
        if c.get('name')=='v2_package_zip' and not retry:
            assert failure is not None and failure.get('message')=='Timeout'
        else:assert failure is None and c.get('status','run')=='run'
    return cases


def merge(initial,retry,inventory,output):
    names=json.loads(inventory.read_text(encoding='utf-8'))
    original=checked_cases(initial,names);replacement=checked_cases(retry,['v2_package_zip'],True)[0]
    suite=ET.Element('testsuite',name='Exact-source coverage union',tests=str(len(names)),failures='0',errors='0',skipped='0')
    props=ET.SubElement(suite,'properties')
    for source in [initial,retry]:ET.SubElement(props,'property',name=str(source),value=digest(source))
    for case in original:suite.append(copy.deepcopy(replacement if case.get('name')=='v2_package_zip' else case))
    ET.ElementTree(suite).write(output,encoding='utf-8',xml_declaration=True)
    return validate_ctest(output,names)


def main():
    parser=argparse.ArgumentParser();parser.add_argument('evidence',type=Path)
    parser.add_argument('--run-id');parser.add_argument('--audit-only',action='store_true');args=parser.parse_args()
    original=args.evidence.resolve();proof=json.loads(original.read_text(encoding='utf-8'))
    if args.audit_only:
        audit_union(proof,original.parent)
        print('Original/retry XML and coverage union match:',original)
        return
    if not args.run_id:parser.error('--run-id is required for a retry')
    assert proof['mode']=='full' and proof['status']=='failed'
    assert proof['source']['sha256']==source_identity(ROOT)['sha256']
    settings=proof['settings'];options=argparse.Namespace(**settings)
    build,config=configuration(options);directory=ROOT/'out/verification'/args.run_id
    assert directory.parent==ROOT/'out/verification' and not directory.exists()
    assert proof['environment']==environment_identity(child_environment(options.parallel,original.parent))
    assert proof['build_environment']==build_environment(build) and settings['cache_sha256']==digest(build/'CMakeCache.txt')
    checked_cases(original.parent/'tests.xml',proof['expected_ctest'])
    validate_ctest(original.parent/'preflight.xml',list(PREFLIGHT))
    names=[c['name'] for c in proof['ui_inventory']]
    reports=list(original.parent.glob('selftest-*.json'));assert len(reports)==1
    validate_ui(json.loads(reports[0].read_text(encoding='utf-8')),names)
    validate_ui(json.loads((original.parent/'package/ui-results.json').read_text(encoding='utf-8')),names)
    exe=build/config/'kachakacha_cad_next.exe';old_sha=digest(exe)
    assert old_sha==digest(original.parent/'package'/exe.name)
    successful=[s for s in proof['steps'] if s['exit_code']==0]
    assert [s['name'] for s in proof['steps'] if s['exit_code']!=0]==['tests']
    test_start=next(s['start'] for s in proof['steps'] if s['name']=='tests')
    stamp=datetime.datetime.fromisoformat(test_start).timestamp()
    binaries=binary_snapshot(build,config,stamp)
    lock=build/'.verification-lock';lock.mkdir()
    try:
        directory.mkdir();run=Run(options,directory)
        shutil.copy2(Path(__file__),directory/'recovery-script.py')
        for step in successful:
            if step.get('log'):shutil.copy2(original.parent/step['log'],directory/step['log'])
        (directory/'expected.json').write_text(json.dumps(proof['expected_ctest']),encoding='utf-8')
        for name in ['preflight.xml','tests.xml',reports[0].name]:
            shutil.copy2(original.parent/name,directory/('tests-initial.xml' if name=='tests.xml' else name))
        run.ctest(build,config,['v2_package_zip'],'retry-package')
        ctest=merge(directory/'tests-initial.xml',directory/'retry-package.xml',directory/'expected.json',directory/'tests.xml')
        (directory/'tests.log').write_text(
            f"Coverage union: {len(proof['expected_ctest'])-1} original passes + 1 successful package retry. Raw reports preserved.\n",encoding='utf-8')
        aggregate=dict(name='tests',command=['coverage-union',str(original.parent/'tests.xml'),'retry-package.xml'],
                       start=now(),end=now(),seconds=0,exit_code=0,log='tests.log')
        run.command('legacy-self-test',[sys.executable,ROOT/'scripts/verification/isolated_test.py',
                    '--name','legacy-final','--',build/config/'kachakacha_cad.exe','--self-test'])
        assert proof['source']['sha256']==source_identity(ROOT)['sha256']
        assert proof['build_environment']==build_environment(build) and settings['cache_sha256']==digest(build/'CMakeCache.txt')
        assert digest(exe)==old_sha==digest(directory/'package'/exe.name)
        assert binaries==binary_snapshot(build,config,stamp)
        proof.update(status='passed',end=now(),ctest=ctest,steps=successful+run.steps+[aggregate],
            reused_from=dict(evidence=str(original),sha256=digest(original),originalStatus='failed',
                             reason='Only v2_package_zip timed out; exact source/environment/build and tested executable matched',
                             ctestXmlSha256=digest(original.parent/'tests.xml'),passedCasesReused=len(proof['expected_ctest'])-1),
            ui_cases=[validate_ui(json.loads(p.read_text(encoding='utf-8')),names) for p in
                      [directory/reports[0].name,directory/'package/ui-results.json']],
            artifacts=artifact_identity(directory/'package'),zip_sha256=digest(directory/'package.zip'))
        proof['reuse_binary_snapshot']=binaries
        audit_union(proof,directory)
        validate_proof(proof,source_identity(ROOT),settings,environment_identity(run.env),artifact_identity(directory/'package'))
        result=directory/'result.json';result.write_text(json.dumps(proof,ensure_ascii=False,indent=2),encoding='utf-8')
        (ROOT/'out/verification/latest-full.json').write_text(json.dumps({'evidence':str(result)}),encoding='utf-8')
        print('Passed exact-source full coverage with explicit retry:',result)
    finally:lock.rmdir()


if __name__=='__main__':main()
