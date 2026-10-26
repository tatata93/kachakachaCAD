"""Negative checks against completed evidence; do not rerun geometry workloads."""
from pathlib import Path
import argparse, copy, json, sys
from unittest.mock import patch
import review_revised as review


def main():
    ap=argparse.ArgumentParser();ap.add_argument('workspace',type=Path);ap.add_argument('--run',default='04');a=ap.parse_args()
    original_load=review.load;original_extract=review.extract_roundtrip;results=[]
    def reject(label,loader=original_load,extract=original_extract):
        with patch.object(sys,'argv',['review_revised',str(a.workspace),'--run',a.run]), \
             patch.object(review,'load',side_effect=loader),patch.object(review,'extract_roundtrip',side_effect=extract):
            try:review.main()
            except AssertionError:print('PASS rejected '+label);results.append(label)
            else:raise AssertionError('Incorrectly accepted '+label)
    # Remove one pair consistently from both candidate list and report. Merely
    # comparing those two would pass; current BRep AABBs must reveal the omission.
    def stale(path):
        d=copy.deepcopy(original_load(path))
        if path.name=='design-all-pairs.json':
            d['assemblyChecks'].pop();d['broadPhase']['candidates']-=1
        if path.parent.name=='all-pairs'+a.run and path.name=='print-parts.json':d['assemblyChecks'].pop()
        return d
    reject('a consistently stale candidate pair list',loader=stale)
    def group_changed(prepared,destination):
        token,d=original_extract(prepared,destination);d=copy.deepcopy(d)
        key=next(k for k in d['groups'][0] if k!='id' and isinstance(d['groups'][0][k],str))
        d['groups'][0][key]='changed during save';return token,d
    reject('a changed saved group',extract=group_changed)
    def hidden(prepared,destination):
        token,d=original_extract(prepared,destination);d=copy.deepcopy(d)
        next(e for e in d['entities'] if e['kind']=='part' and e['visibility']=='visible')['visibility']='hidden'
        return token,d
    reject('a changed saved part visibility',extract=hidden)
    def zero(path):
        d=copy.deepcopy(original_load(path))
        if path.parent.name=='exports'+a.run and path.name=='print-parts.json':d['parts']=[];d['visibleParts']=0
        return d
    reject('zero visible parts',loader=zero)
    for kind in ['missing','failed','stale']:
        def bad_contacts(path,kind=kind):
            d=copy.deepcopy(original_load(path))
            if path.parent.name=='exports'+a.run and path.name=='print-parts.json':
                if kind=='missing':d['attachmentChecks']=[]
                elif kind=='failed':d['attachmentChecks'][0]['passed']=False
                else:d['attachmentChecks'][0]['first']='stale entity'
            return d
        if original_load(a.workspace/('candidate'+a.run)/'design.json').get('attachmentRevision'):
            reject(kind+' attachment evidence',loader=bad_contacts)
    (a.workspace/'review-negative-tests.json').write_text(json.dumps(dict(passed=True,rejected=results,
        reviewResultSha256=review.sha(a.workspace/'review-result.json')),indent=2),encoding='utf-8',newline='\n')


if __name__=='__main__':main()
