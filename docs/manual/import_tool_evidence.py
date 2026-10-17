"""Import Windows self-test screenshots without presenting setup as committed geometry."""
from pathlib import Path
import argparse, hashlib, json, shutil
HERE=Path(__file__).resolve().parent

def main():
    parser=argparse.ArgumentParser();parser.add_argument('directory',type=Path);parser.add_argument('report',type=Path);parser.add_argument('executable',type=Path);args=parser.parse_args()
    report=json.loads(args.report.read_text(encoding='utf-8'))
    assert report['total']==len(report['selected'])==len(report['cases']) and report['failed']==0 and not report['error'],'Only complete passing runs may supply manual images'
    status={r['name']:r['status'] for r in report['cases']}
    calls=[json.loads(l) for l in (args.directory/'cases.jsonl').read_text(encoding='utf-8').splitlines()]
    assert all(c['passed'] for c in calls),'Evidence directory contains failed cases; use a fresh run directory'
    from build_manual import commands
    ids={r[0] for r in commands()};result={};coverage={}
    executable_hash=hashlib.sha256(args.executable.read_bytes()).hexdigest()
    for ident in sorted(ids):
        meta=json.loads((args.directory/'commands'/f'{ident}.json').read_text(encoding='utf-8'))
        assert status.get(meta['case'])=='passed',(ident,meta['case'])
        assert meta['phase']=='command returned; not proof of committed geometry'
        image=args.directory/'commands'/f'{ident}-after.png'
        assert image.is_file() and image.stat().st_size>100
        (HERE/'tool-screens').mkdir(exist_ok=True);shutil.copyfile(image,HERE/'tool-screens'/f'{ident}.png')
        result[ident]={'case':meta['case'],'case_result':'passed','at':meta['at'],'phase':meta['phase'],'executable_sha256':executable_hash}
        relevant=[c for c in calls if any(v['command']==ident for v in c['calls'])]
        populated=[c['case'] for c in relevant if any(v['command']==ident and v['selected']>0 for v in c['calls'])]
        coverage[ident]={'cases':sorted(set(c['case'] for c in relevant)), 'selected_input_cases':sorted(set(populated)),
            'interpretation':'A passing containing case and selected input do not by themselves prove a successful geometry result.'}
    (HERE/'tool-evidence.json').write_text(json.dumps(result,ensure_ascii=False,indent=2)+'\n',encoding='utf-8',newline='\n')
    audit={'schema':1,'total':len(ids),'test_cases':report['total'],'commands':coverage,
        'limits':'Catalogue reachability is distinct from functional cases. Unsupported UI methods are documented, not claimed to pass.'}
    (HERE/'tool-test-coverage.json').write_text(json.dumps(audit,ensure_ascii=False,indent=2)+'\n',encoding='utf-8',newline='\n')
    print(f'Imported {len(result)} command screenshots from {report["total"]} passing Windows UI cases')

if __name__=='__main__':main()
