"""Prepare a native assembly fixture from independent GUI-exported native parts.

This test preparation preserves the exact serialized B-Reps and their positions.
The following GUI run must validate/reopen it and perform the actual union/export.
It does not replace the editable source or count as GUI import/merge coverage.
"""
import argparse, copy, hashlib, json, uuid
from pathlib import Path
from zipfile import ZipFile, ZIP_STORED

p = argparse.ArgumentParser()
p.add_argument('--input', required=True)
p.add_argument('--pattern', required=True)
p.add_argument('--output', required=True)
a = p.parse_args()
paths = sorted(Path(a.input).glob(a.pattern))
if not paths:
    raise SystemExit('No exported parts')
output = Path(a.output)
if output.exists():
    raise SystemExit('Refusing to overwrite existing fixture')
documents, records = [], []
for path in paths:
    data = path.read_bytes()
    with ZipFile(path) as archive:
        document = json.loads(archive.read('document.json'))
    assert len(document['entities']) == len(document['features']) == 1
    feature = document['features'][0]
    assert feature['type'] == 'freeze_derived' and feature['definition']['frozenBrep']
    assert not feature['inputEntityIds']
    documents.append(document)
    records.append(dict(path=str(path.resolve()), sha256=hashlib.sha256(data).hexdigest(),
                        brep_sha256=hashlib.sha256(feature['definition']['frozenBrep'].encode()).hexdigest()))
result = copy.deepcopy(documents[0])
result['documentId'] = str(uuid.uuid4())
for key in ('entities', 'features', 'rootOrder'):
    result[key] = [item for document in documents for item in document[key]]
ids = [item['id'] for key in ('entities', 'features') for item in result[key]]
assert len(ids) == len(set(ids)), 'Independent exports unexpectedly repeat IDs'
result['metadata']['title'] = output.stem
result['metadata']['description'] = 'Exact independent GUI-exported parts; native assembly validation fixture.'
output.parent.mkdir(parents=True, exist_ok=True)
with ZipFile(output, 'w', ZIP_STORED) as archive:
    archive.writestr('document.json', json.dumps(result, ensure_ascii=False))
output.with_suffix('.sources.json').write_text(json.dumps(records, indent=2), encoding='utf-8')
print(f'{len(paths)} unchanged part B-Reps: {output}')
