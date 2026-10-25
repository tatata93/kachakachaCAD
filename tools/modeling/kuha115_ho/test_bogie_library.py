"""Reject stale native/component evidence without repeating geometry workloads."""
from pathlib import Path
import argparse,copy,json,tempfile,zipfile
import bogie_library as library
from revised import Revised

def main():
    ap=argparse.ArgumentParser();ap.add_argument('workspace',type=Path);ap.add_argument('--run',default='03');a=ap.parse_args();w=a.workspace;n=a.run
    source=w/('bogie'+n)/'tr62-native.kcd2';prepared=w/('bogie-native'+n)/'prepared.bin'
    validation=w/('bogie-native'+n)/'validation.json';report=w/('bogie-exports'+n)/'print-parts.json';design=w/('bogie'+n)/'design.json'
    results=[]
    def rejected(name,callback):
        try:callback()
        except AssertionError:results.append(name)
        else:raise AssertionError('Accepted invalid evidence: '+name)
    with tempfile.TemporaryDirectory(prefix='component-negative-',dir=w) as directory:
        temporary=Path(directory)
        def changed(path,field,value):
            data=json.loads(path.read_text(encoding='utf-8'));data[field]=value
            out=temporary/(field+'.json');out.write_text(json.dumps(data),encoding='utf-8');return out
        rejected('changed source digest',lambda:library.package(source,prepared,
            changed(validation,'modelSha','0'*64),report,design,temporary/'stale-source'))
        rejected('different native validation token',lambda:library.package(source,prepared,validation,
            changed(report,'preparedToken','different-job'),design,temporary/'stale-job'))
        rejected('missing component part',lambda:library.package(source,prepared,validation,
            changed(report,'visibleParts',7),design,temporary/'missing-part'))
        original=w/('component'+n)/'tr62-component.kcd2'
        with zipfile.ZipFile(original) as z:doc=json.loads(z.read('document.json'))
        next(f for f in doc['features'] if f['type']=='freeze_derived')['definition']['frozenBrep']='changed shape'
        forged=temporary/'tr62-component.kcd2'
        with zipfile.ZipFile(forged,'w') as z:z.writestr('document.json',json.dumps(doc))
        provenance=json.loads(original.with_suffix('.json').read_text(encoding='utf-8'))
        provenance['componentSha256']=library.sha(forged)
        forged.with_suffix('.json').write_text(json.dumps(provenance),encoding='utf-8')
        def place():
            m=Revised(True);root=m.group('negative test');library.place(m,root,forged)
        rejected('changed BRep despite matching archive checksum',place)
    result=dict(passed=True,rejected=results,sourceSha256=library.sha(source),componentSha256=library.sha(original))
    (w/'component-negative-tests.json').write_text(json.dumps(result,indent=2),encoding='utf-8',newline='\n')
    print(json.dumps(result))

if __name__=='__main__':main()
