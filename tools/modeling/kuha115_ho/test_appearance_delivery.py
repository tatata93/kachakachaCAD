"""Exercise the actual packager's refusal paths without altering model artifacts."""
from pathlib import Path
from unittest.mock import patch
import argparse,hashlib,json,sys
import assemble_appearance


def main():
    ap=argparse.ArgumentParser();ap.add_argument('workspace',type=Path);a=ap.parse_args();w=a.workspace
    original_read=Path.read_text;original_glob=Path.glob;rejected=[];target=w/'must-not-package'
    assert not target.exists()
    def run(label,message,reader=original_read,globber=original_glob):
        with patch.object(sys,'argv',['assemble_appearance',str(w),str(target),str(target)]),\
            patch.object(Path,'read_text',reader),patch.object(Path,'glob',globber):
            try:assemble_appearance.main()
            except AssertionError as exc:
                assert str(exc)==message,(label,str(exc));rejected.append(label)
            else:raise AssertionError('Packager accepted '+label)
        assert not target.exists()
    def stale_artifact(path,*args,**kwargs):
        text=original_read(path,*args,**kwargs)
        if path.name=='review-result.json':
            d=json.loads(text);key=next(iter(d['evidenceSha256']));d['evidenceSha256'][key]='0'*64;return json.dumps(d)
        return text
    run('changed verified CAD/export bytes','Changed verified artifact',reader=stale_artifact)
    def missing(path,pattern):
        values=list(original_glob(path,pattern))
        return iter([p for p in values if p.name!='overview.png']) if path.name=='previews05' else iter(values)
    run('missing required overview image','Missing or unexpected final previews',globber=missing)
    def stale_image(path,*args,**kwargs):
        text=original_read(path,*args,**kwargs)
        if path.name=='render-proof.json':
            d=json.loads(text);d['imagesSha256']['overview.png']='0'*64;return json.dumps(d)
        return text
    run('changed rendered image bytes','Changed preview bytes',reader=stale_image)
    result=dict(passed=True,rejected=rejected,reviewResultSha256=hashlib.sha256((w/'review-result.json').read_bytes()).hexdigest())
    (w/'delivery-negative-tests.json').write_text(json.dumps(result,indent=2),encoding='utf-8',newline='\n')
    print('PASS packaging rejects changed artifacts, missing and stale images')


if __name__=='__main__':main()
