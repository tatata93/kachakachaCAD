"""Build the separately editable, native 1/80 TR62 source and inspection plan."""
from pathlib import Path
import argparse,json
import revised
from bogie_details import detail_checks,REVISION

def main():
    ap=argparse.ArgumentParser();ap.add_argument('output',type=Path);a=ap.parse_args();a.output.mkdir(parents=True,exist_ok=False)
    m=revised.Revised(True);g=m.group('TR62独立検証')
    m.finished['floor']=m.box('検査用床',100,0,0,1,1,1,g)
    previous=revised.PIVOTS
    try:revised.PIVOTS=[35.625];revised.bogies(m,g)
    finally:revised.PIVOTS=previous
    m.entities[m.finished['floor']]['visibility']='hidden';detail_checks(m,35.625)
    m.save(a.output/'tr62-native.kcd2')
    data=dict(bogieRevision=REVISION,entities=m.d['entities'],groups=m.d['groups'],colors=m.colors,
        pointChecks=m.points,assemblyChecks=m.checks,stlChordMm=.025,stlChordByEntity=m.mesh_chords,springChecks=m.spring_checks)
    (a.output/'design.json').write_text(json.dumps(data,ensure_ascii=False,indent=2),encoding='utf-8',newline='\n')
    print(json.dumps(dict(features=len(m.d['features']),pointChecks=len(m.points),maximumSpringPathErrorMm=max(x['maximumMm'] for x in m.spring_checks))))

if __name__=='__main__':main()
