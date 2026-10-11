"""Wrap a previously validated BRep in an independent native test fixture.

This is test preparation, not a claim that the GUI imported or merged a BRep.
The subsequent GUI run must reopen and export it. Keep editable source history.
"""
import argparse,hashlib,json,uuid,zipfile
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('--template',required=True);p.add_argument('--brep',required=True)
p.add_argument('--output',required=True);p.add_argument('--description',required=True);a=p.parse_args()
out=Path(a.output)
if out.exists():raise SystemExit('Refusing to overwrite fixture')
with zipfile.ZipFile(a.template) as z:doc=json.loads(z.read('document.json'))
assert len(doc['entities'])==len(doc['features'])==1
assert doc['features'][0]['type']=='freeze_derived' and not doc['features'][0]['inputEntityIds']
doc['documentId']=str(uuid.uuid4());doc['features'][0]['definition']['frozenBrep']=Path(a.brep).read_text(encoding='utf-8')
doc['entities'][0]['displayName']=out.stem;doc['metadata']['title']=out.stem;doc['metadata']['description']=a.description
out.parent.mkdir(parents=True,exist_ok=True)
with zipfile.ZipFile(out,'w',zipfile.ZIP_STORED) as z:z.writestr('document.json',json.dumps(doc,ensure_ascii=False))
report={'description':a.description,'scope':'Independent native test fixture preparation. Not GUI BRep import/merge coverage.',
    'sources':{str(Path(s).resolve()):hashlib.sha256(Path(s).read_bytes()).hexdigest() for s in (a.template,a.brep)},
    'output_sha256':hashlib.sha256(out.read_bytes()).hexdigest()}
out.with_suffix('.provenance.json').write_text(json.dumps(report,indent=2),encoding='utf-8');print(out)
