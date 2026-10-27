"""Independent ray sections of exported native geometry, including the old failure."""
from pathlib import Path
import argparse,hashlib,json
import numpy as np
from audit_appearance_geometry import mesh,front_section


def sha(path):return hashlib.sha256(path.read_bytes()).hexdigest()


def sections(exports):
    report=json.loads((exports/'print-parts.json').read_text(encoding='utf-8'))
    bodies=[p for p in report['parts'] if '/01 車体' in p['groupPath']]
    roofs=[p for p in report['parts'] if '/02 屋根' in p['groupPath'] and '中空屋根' in p['name']]
    assert len(bodies)==len(roofs)==1,'Ambiguous or absent native shell'
    body,roof=bodies[0],roofs[0]
    bm,rm=mesh(exports/body['stl']),mesh(exports/roof['stl'])
    rows=[]
    for y in sorted(set(np.linspace(-18.,18.,161).tolist()+[-16.5,-14.,-10.,-5.3,0.,5.3,10.,14.,16.5])):
        bx,rx=front_section(bm,y,38.98),front_section(rm,y,39.02)
        rows.append(dict(yMm=float(y),bodyZMm=38.98,roofZMm=39.02,
            bodyFrontXMm=bx,roofFrontXMm=rx,stepMm=rx-bx))
    return dict(nativeReportSha256=sha(exports/'print-parts.json'),
        meshes={p['stl']:sha(exports/p['stl']) for p in [body,roof]},rows=rows,
        maximumAbsoluteStepMm=max(abs(r['stepMm']) for r in rows))


def main():
    ap=argparse.ArgumentParser();ap.add_argument('exports',type=Path);ap.add_argument('output',type=Path)
    ap.add_argument('--baseline',type=Path);a=ap.parse_args()
    current=sections(a.exports);baseline=sections(a.baseline) if a.baseline else None
    # Body chord <= .025, roof <= .006 mm; this is a mesh-section check,
    # not an assertion that the model reproduces a measured prototype radius.
    allowed=.04
    if baseline:assert baseline['maximumAbsoluteStepMm']>.1,'Old failure must be detected'
    result=dict(passed=current['maximumAbsoluteStepMm']<=allowed,allowedMaximumStepMm=allowed,
        scope='Actual STL X-ray sections on either side of the Z39 joint; exact BRep point probes are recorded separately.',
        candidate=current,baseline=baseline)
    a.output.parent.mkdir(parents=True,exist_ok=True)
    a.output.write_text(json.dumps(result,indent=2),encoding='utf-8',newline='\n')
    print(json.dumps(dict(passed=result['passed'],maximumStepMm=current['maximumAbsoluteStepMm'],
        previousMaximumStepMm=baseline['maximumAbsoluteStepMm'] if baseline else None)))
    raise SystemExit(0 if result['passed'] else 1)


if __name__=='__main__':main()
