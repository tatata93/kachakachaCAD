"""Conservative audited scope selection. Unknown changes require full coverage."""
import fnmatch
import json
import re


PREFLIGHT = ('v2_harness_tests', 'v2_architecture_tests', 'v2_scaffold_tests',
             'v2_command_catalog_tests', 'v2_command_availability_tests',
             'v2_core_ui_wiring_tests', 'v2_coverage_tests', 'v2_manual_tests',
             'verification_infrastructure_tests')


def select_plan(changed, inventory, rules):
    names = {t['name'] for t in inventory}
    missing = set(PREFLIGHT) - names
    if missing:
        raise ValueError('Required preflight tests missing: ' + ', '.join(sorted(missing)))
    tests = set(PREFLIGHT)
    suites = set()
    reasons = []
    for path in changed:
        matches = [r for r in rules['scopes'] if any(fnmatch.fnmatchcase(path, p) for p in r['paths'])]
        if not matches:
            return {'effective_full': True, 'reason': 'Unmapped/shared change: ' + path,
                    'ctest': sorted(names), 'ui_suites': []}
        for rule in matches:
            selected = set(rule['required_tests'])
            if not selected or selected - names:
                raise ValueError('Audited required tests missing: ' + rule['name'] + ' / ' + ', '.join(sorted(selected - names)))
            tests.update(selected)
            suites.update(rule.get('ui_suites', []))
            reasons.append(rule['name'])
    return {'effective_full': False, 'reason': sorted(set(reasons)) or ['No source change; preflight only'],
            'ctest': sorted(tests), 'ui_suites': sorted(suites)}
