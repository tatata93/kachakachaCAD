"""Negative-path acceptance tests for the development verification infrastructure."""
import copy
import json
import os
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch
from types import SimpleNamespace
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from verification.evidence import source_identity, validate_ctest, validate_proof, validate_ui
from verification.selection import PREFLIGHT, select_plan
from verification.runner import Run, child_environment


class VerificationTests(unittest.TestCase):
    def test_configuration_and_fixture_environment_are_not_silently_reused(self):
        with tempfile.TemporaryDirectory() as work:
            with patch.dict(os.environ, {'KACHACAD_SELF_TEST_FILTER': 'nothing',
                'KACHACAD_THICKEN_TEST_FILE': 'external.kcd2', 'KACHACAD_SURFACE_TEST_OUTPUT': 'external',
                'KACHACAD_HEAVY_MODEL': 'external.kcd2', 'KACHACAD_PRACTICAL_FILE': 'external.kcd2'}):
                env = child_environment(2, Path(work))
                for key in ('KACHACAD_SELF_TEST_FILTER', 'KACHACAD_THICKEN_TEST_FILE',
                            'KACHACAD_SURFACE_TEST_OUTPUT', 'KACHACAD_HEAVY_MODEL', 'KACHACAD_PRACTICAL_FILE'):
                    self.assertNotIn(key, env)
            run = Run(SimpleNamespace(parallel=2, config='Debug', preset='windows-msvc'), Path(work))
            with patch.object(run, 'command') as command:
                run.build(['kachakacha_cad_next'])
                args = command.call_args.args[1]
                self.assertEqual(args[args.index('--config') + 1], 'Debug')

    def test_ctest_inventory_failure_and_skips(self):
        with tempfile.TemporaryDirectory() as work:
            path = Path(work) / 'tests.xml'
            for xml, expected, accepted in [
                ('<testsuite/>', [], False),
                ('<testsuite><testcase name="a"/></testsuite>', ['a', 'b'], False),
                ('<testsuite><testcase name="a"/><testcase name="a"/></testsuite>', ['a'], False),
                ('<testsuite><testcase name="a"><failure/></testcase></testsuite>', ['a'], False),
                ('<testsuite><testcase name="a"><skipped/></testcase></testsuite>', ['a'], False),
                ('<testsuite><testcase name="a" status="notrun"/></testsuite>', ['a'], False),
                ('<testsuite><testcase name="a"/></testsuite>', ['a'], True),
            ]:
                path.write_text(xml, encoding='utf-8')
                if accepted:
                    self.assertEqual(validate_ctest(path, expected)['total'], 1)
                else:
                    with self.assertRaises(ValueError): validate_ctest(path, expected)
            path.write_text('<testsuite><testcase name="a"><skipped/></testcase></testsuite>')
            self.assertEqual(validate_ctest(path, ['a'], ['a'])['skipped'], ['a'])

    def test_ui_exact_coverage_and_timing(self):
        report = {'total': 2, 'failed': 0, 'cases': [
            {'name': 'a', 'status': 'passed', 'duration_ms': 1},
            {'name': 'b', 'status': 'passed', 'duration_ms': 2}]}
        self.assertEqual(validate_ui(report, ['a', 'b']), 2)
        for key, value in [('total', 3), ('failed', 1)]:
            bad = copy.deepcopy(report); bad[key] = value
            with self.assertRaises(ValueError): validate_ui(bad, ['a', 'b'])
        for expected in ([], ['b', 'a'], ['a'], ['a', 'a']):
            with self.assertRaises(ValueError): validate_ui(report, expected)
        for value in (-1, float('nan'), float('inf'), '1'):
            bad = copy.deepcopy(report); bad['cases'][0]['duration_ms'] = value
            with self.assertRaises(ValueError): validate_ui(bad, ['a', 'b'])

    def test_proof_rejects_every_stale_identity_and_partial_results(self):
        source, settings, environment, artifacts = {'sha256': 'source'}, {'config': 'Release'}, {'compiler': 'v1'}, {'app': 'hash'}
        proof = {'status': 'passed', 'mode': 'full', 'source': source, 'settings': settings,
                 'environment': environment, 'artifacts': artifacts, 'preflight': {'total': 1},
                 'ctest': {'total': 1, 'skipped': []}, 'expected_ctest': ['a'], 'ui_cases': [1, 1],
                 'steps': [{'name': name, 'exit_code': 0} for name in ('configure','inventory','ui-inventory','preflight','tests','legacy-self-test','build-1')]}
        validate_proof(proof, source, settings, environment, artifacts)
        for field, replacement in [('status', 'failed'), ('mode', 'focused'), ('source', {'sha256': 'other'}),
                                   ('settings', {'config': 'Debug'}), ('environment', {'compiler': 'v2'}),
                                   ('artifacts', {'app': 'different'}), ('preflight', {}),
                                   ('ctest', {'total': 0}), ('ui_cases', []), ('steps', [{'exit_code': 1}]),
                                   ('steps', [{'name': 'tests', 'exit_code': 0}])]:
            bad = copy.deepcopy(proof); bad[field] = replacement
            with self.assertRaises(ValueError): validate_proof(bad, source, settings, environment, artifacts)

    def test_source_identity_uses_bytes_not_only_commit(self):
        with tempfile.TemporaryDirectory() as work:
            root = Path(work)
            subprocess.run(['git', 'init', '-q', root], check=True)
            (root / 'src').mkdir(); source = root / 'src/a.cpp'; source.write_text('int x=1;')
            subprocess.run(['git', '-C', root, 'add', 'src/a.cpp'], check=True)
            subprocess.run(['git', '-C', root, '-c', 'user.name=Verification', '-c', 'user.email=verification@example.invalid',
                            'commit', '-qm', 'fixture'], check=True)
            before = source_identity(root)
            source.write_text('int x=2;')
            after = source_identity(root)
            self.assertEqual(before['commit'], after['commit'])
            self.assertNotEqual(before['sha256'], after['sha256'])
            (root / 'src/new.cpp').write_text('int y=3;')
            self.assertNotEqual(after['sha256'], source_identity(root)['sha256'])
            (root / 'local.cmake').write_text('set(TEST_OPTION ON)')
            self.assertIn('local.cmake', source_identity(root)['files'])
            (root / 'tools').mkdir(); (root / 'tools/sample.cpp').write_text('int sample;')
            self.assertIn('tools/sample.cpp', source_identity(root)['files'])
            (root / 'legal').mkdir(); (root / 'legal/notice.txt').write_text('notice')
            self.assertIn('legal/notice.txt', source_identity(root)['files'])

    def test_scope_selection_expands_shared_or_unknown_changes(self):
        names = list(PREFLIGHT) + ['v2_thicken_input_state_tests', 'v2_kernel_thicken_quality_tests',
            'v2_kernel_warped_thicken_tests', 'v2_session_tests', 'v2_shape_rebuild_tests', 'v2_feature_reevaluation_tests']
        inventory = [{'name': n} for n in names]
        rules = json.loads((Path(__file__).parent / 'scopes.json').read_text())
        plan = select_plan(['src/apps/cad_next/V2ThickenCommands.cpp'], inventory, rules)
        self.assertFalse(plan['effective_full'])
        self.assertIn('v2_session_tests', plan['ctest'])
        self.assertIn('Regression', plan['ui_suites'])
        with self.assertRaises(ValueError): select_plan(['src/apps/cad_next/V2ThickenCommands.cpp'], inventory[:-1], rules)
        for path in ['src/apps/cad_next/V2MainWindow.h', 'src/next/kachakacha/document/Document.cpp', 'scripts/new.py']:
            self.assertTrue(select_plan([path], inventory, rules)['effective_full'])
        with self.assertRaises(ValueError): select_plan([], inventory[:-len(names)], rules)

    def test_concurrent_processes_do_not_share_temp_cache_or_files(self):
        with tempfile.TemporaryDirectory() as work:
            root = Path(work)
            child = root / 'child.py'
            child.write_text('import os,tempfile,json,time\nfrom pathlib import Path\n'
                'Path("fixed-name.txt").write_text(str(os.getpid()))\n'
                'Path("child.json").write_text(json.dumps({"cwd":str(Path.cwd()),"temp":tempfile.gettempdir(),'
                '"cache":os.environ["KACHACAD_DOCUMENT_CACHE_DIR"]}))\ntime.sleep(0.1)\n')
            env = {k.upper(): v for k, v in os.environ.items()}
            env['KACHACAD_VERIFICATION_RUN_DIR'] = str(root)
            command = [sys.executable, str(Path(__file__).parent / 'isolated_test.py'), '--name', 'same', '--', sys.executable, str(child)]
            processes = [subprocess.Popen(command, env=env) for _ in range(2)]
            self.assertEqual([p.wait(timeout=20) for p in processes], [0, 0])
            results = [json.loads(p.read_text()) for p in root.glob('isolated/*/child.json')]
            self.assertEqual(len(results), 2)
            for key in ('cwd', 'temp', 'cache'): self.assertEqual(len({r[key] for r in results}), 2)
            for row in results: self.assertEqual(row['cwd'], row['temp'])


if __name__ == '__main__':
    unittest.main(verbosity=2)
