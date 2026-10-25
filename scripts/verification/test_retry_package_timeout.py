import copy, importlib.util, json, tempfile, unittest, xml.etree.ElementTree as ET
from pathlib import Path

candidate=Path(__file__).with_name('retry_package_timeout.py')
spec=importlib.util.spec_from_file_location('retry_timeout',candidate)
retry=importlib.util.module_from_spec(spec);spec.loader.exec_module(retry)


class RetryEvidenceTests(unittest.TestCase):
    def setUp(self):
        self.temp=tempfile.TemporaryDirectory()
        self.root=Path(self.temp.name).resolve()
        assert self.root.is_relative_to(Path(tempfile.gettempdir()).resolve())
        self.addCleanup(self.temp.cleanup)

    def write(self,name,text):
        p=self.root/name;p.write_text(text,encoding='utf-8');return p

    def test_reject_empty_duplicate_skip_failure_and_incomplete(self):
        for body,names in [('',[]),('<testcase name="x"/><testcase name="x"/>',['x']),
            ('<testcase name="x"><skipped/></testcase>',['x']),
            ('<testcase name="x"><failure message="assertion"/></testcase>',['x']),
            ('<testcase name="x"/>',['x','missing'])]:
            with self.subTest(body=body):
                p=self.write('invalid.xml','<testsuite>'+body+'</testsuite>')
                with self.assertRaises(AssertionError):retry.checked_cases(p,names,True)

    def inputs(self):
        initial=self.write('initial.xml','<testsuite><testcase name="x" status="run"/><testcase name="v2_package_zip" status="fail"><failure message="Timeout"/></testcase></testsuite>')
        repeated=self.write('retry.xml','<testsuite><testcase name="v2_package_zip" status="run"/></testsuite>')
        inventory=self.write('expected.json',json.dumps(['x','v2_package_zip']))
        return initial,repeated,inventory,self.root/'tests.xml'

    def test_union_requires_real_successful_retry(self):
        a,b,inv,out=self.inputs();self.assertEqual(retry.merge(a,b,inv,out)['total'],2)
        b.write_text('<testsuite><testcase name="v2_package_zip" status="fail"><failure message="Timeout"/></testcase></testsuite>',encoding='utf-8')
        with self.assertRaises(AssertionError):retry.merge(a,b,inv,out)

    def test_raw_evidence_and_union_are_rechecked(self):
        a,b,inv,out=self.inputs();retry.merge(a,b,inv,out)
        (self.root/'tests-initial.xml').write_bytes(a.read_bytes())
        (self.root/'retry-package.xml').write_bytes(b.read_bytes())
        old=self.root/'old';old.mkdir();(old/'tests.xml').write_bytes(a.read_bytes())
        original=self.write('old/result.json',json.dumps(dict(source={'sha256':'source'},expected_ctest=['x','v2_package_zip'])))
        proof=dict(source={'sha256':'source'},expected_ctest=['x','v2_package_zip'],
            reused_from=dict(evidence=str(original),sha256=retry.digest(original),
                             ctestXmlSha256=retry.digest(a),passedCasesReused=1))
        retry.audit_union(proof,self.root)
        changed=copy.deepcopy(proof);changed['source']['sha256']='other'
        with self.assertRaises(AssertionError):retry.audit_union(changed,self.root)
        tree=ET.parse(out);next(tree.getroot().iter('testcase')).set('time','99');tree.write(out)
        with self.assertRaises(AssertionError):retry.audit_union(proof,self.root)
        retry.merge(a,b,inv,out);(self.root/'tests-initial.xml').write_text('changed',encoding='utf-8')
        with self.assertRaises(AssertionError):retry.audit_union(proof,self.root)


if __name__=='__main__':unittest.main()
