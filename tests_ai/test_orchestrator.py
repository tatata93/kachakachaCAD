import copy
import importlib.util
import sys
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location(
    "kachakacha_orchestrator", ROOT / "scripts" / "orchestrator.py"
)
assert SPEC is not None and SPEC.loader is not None
orchestrator = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = orchestrator
SPEC.loader.exec_module(orchestrator)


def task(task_id="UI-P1-001", status="ready", dependencies=None):
    return {
        "id": task_id,
        "title": "test task",
        "phase": 1,
        "status": status,
        "priority": "high",
        "assigned_to": None,
        "dependencies": dependencies or [],
        "spec_refs": ["docs/v2/ui-ux-integrated-spec.md#4"],
        "files_hint": ["src/example.cpp"],
        "acceptance_tests": ["works"],
        "last_review": None,
        "revision_count": 0,
    }


class OrchestratorTests(unittest.TestCase):
    def test_validates_small_dependency_graph(self):
        tasks = [task(status="passed"), task("UI-P1-002", dependencies=["UI-P1-001"])]
        orchestrator.validate_tasks(tasks)

    def test_rejects_unknown_status(self):
        tasks = [task(status="done")]
        with self.assertRaises(orchestrator.OrchestratorError):
            orchestrator.validate_tasks(tasks)

    def test_rejects_dependency_cycle(self):
        first = task("UI-P1-001", dependencies=["UI-P1-002"])
        second = task("UI-P1-002", dependencies=["UI-P1-001"])
        with self.assertRaises(orchestrator.OrchestratorError):
            orchestrator.validate_tasks([first, second])

    def test_resolves_ready_task_only_after_passed_dependency(self):
        first = task(status="passed")
        second = task("UI-P1-002", dependencies=["UI-P1-001"])
        self.assertEqual(
            orchestrator.resolve_task([first, second], None)["id"], "UI-P1-002"
        )
        blocked = copy.deepcopy(first)
        blocked["status"] = "revision"
        with self.assertRaises(orchestrator.OrchestratorError):
            orchestrator.resolve_task([blocked, second], "UI-P1-002")

    def test_current_task_has_required_sections(self):
        text = orchestrator.render_current_task(task())
        for heading in (
            "# TASK ID",
            "# 目的",
            "# 背景",
            "# 変更対象候補",
            "# 変更禁止範囲",
            "# 必須仕様",
            "# 作業手順",
            "# Acceptance Tests",
            "# 完了条件",
            "# 報告形式",
        ):
            self.assertIn(heading, text)

    def test_review_decision_must_be_exact(self):
        self.assertEqual(orchestrator.parse_review_decision("\nPASS\nok"), "PASS")
        self.assertEqual(orchestrator.parse_review_decision("REVISE\nfix"), "REVISE")
        with self.assertRaises(orchestrator.OrchestratorError):
            orchestrator.parse_review_decision("APPROVE")

    def test_execute_is_opt_in(self):
        default = orchestrator.parse_arguments([])
        self.assertFalse(default.execute)
        forced_dry = orchestrator.parse_arguments(["--execute", "--dry-run"])
        self.assertTrue(forced_dry.execute)
        self.assertTrue(forced_dry.dry_run)


if __name__ == "__main__":
    unittest.main()
