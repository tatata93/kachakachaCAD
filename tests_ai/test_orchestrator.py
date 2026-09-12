import copy
import importlib.util
import json
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path
from unittest import mock


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


def init_test_repo(root: Path) -> None:
    subprocess.run(["git", "init", "-q", str(root)], check=True)
    subprocess.run(["git", "-C", str(root), "config", "user.email", "test@example.invalid"],
        check=True)
    subprocess.run(["git", "-C", str(root), "config", "user.name", "Orchestrator Test"],
        check=True)
    (root / "seed.txt").write_text("seed\n", encoding="utf-8")
    subprocess.run(["git", "-C", str(root), "add", "seed.txt"], check=True)
    subprocess.run(["git", "-C", str(root), "commit", "-q", "-m", "seed"], check=True)


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
        self.assertEqual(orchestrator.parse_review_decision("review complete\nPASS\nok"), "PASS")
        self.assertEqual(orchestrator.parse_review_decision(
            "Build result\nPASS\nFinal decision\nREVISE\nfix"), "REVISE")
        with self.assertRaises(orchestrator.OrchestratorError):
            orchestrator.parse_review_decision("APPROVE")

    def test_execute_is_opt_in(self):
        default = orchestrator.parse_arguments([])
        self.assertFalse(default.execute)
        forced_dry = orchestrator.parse_arguments(["--execute", "--dry-run"])
        self.assertTrue(forced_dry.execute)
        self.assertTrue(forced_dry.dry_run)

    def test_worker_is_noninteractive_and_accepts_file_edits(self):
        command = orchestrator.worker_command(
            Path("worktree"), Path("CURRENT_TASK.md"), None, executable="claude"
        )
        self.assertIn("acceptEdits", command.argv)
        self.assertIn("--permission-prompts", command.argv)
        self.assertIn("none", command.argv)

    def test_worker_must_change_the_task_worktree(self):
        orchestrator.require_worker_changes(" M src/example.cpp")
        with self.assertRaises(orchestrator.OrchestratorError):
            orchestrator.require_worker_changes("\n")

    def test_legacy_task_gets_batch_and_medium_defaults(self):
        policy = orchestrator.determine_policy(task())
        self.assertEqual(policy.execution_mode, "BATCH")
        self.assertEqual(policy.review_effort, "MEDIUM")

    def test_selection_snap_hover_change_is_staged(self):
        value = task()
        value["title"] = "Selection / Snap / Hoverの共通状態連携"
        value["files_hint"] = [
            "src/next/kachakacha/app/Selection.cpp",
            "src/apps/cad_next/V2Viewport.cpp",
        ]
        policy = orchestrator.determine_policy(value)
        self.assertEqual(policy.execution_mode, "STAGED")
        self.assertEqual(policy.review_effort, "HIGH")
        stages = orchestrator.stages_for_task(value, policy)
        self.assertEqual(len(stages), 2)
        self.assertTrue(stages[-1].review_after)

    def test_undo_foundation_is_guarded_and_extra_high(self):
        value = task()
        value["title"] = "Undo/Redo基盤とDocument architectureの再設計"
        value["files_hint"] = ["src/next/kachakacha/document/Document.cpp"]
        policy = orchestrator.determine_policy(value)
        self.assertEqual(policy.execution_mode, "GUARDED")
        self.assertEqual(policy.review_effort, "EXTRA_HIGH")

    def test_explicit_policy_is_rendered_in_current_task(self):
        value = task()
        value["execution_mode"] = "BATCH"
        value["review_effort"] = "HIGH"
        text = orchestrator.render_current_task(value)
        self.assertIn("# Execution Mode\n\nBATCH", text)
        self.assertIn("# Review Effort\n\nHIGH", text)

    def test_rejects_invalid_execution_policy(self):
        value = task()
        value["execution_mode"] = "FAST"
        with self.assertRaises(orchestrator.OrchestratorError):
            orchestrator.validate_tasks([value])

    def test_rejects_invalid_stage_acceptance_tests(self):
        value = task()
        value["execution_mode"] = "STAGED"
        value["stages"] = [
            {"title": "first", "acceptance_tests": "not-a-list"},
            {"title": "second"},
        ]
        with self.assertRaises(orchestrator.OrchestratorError):
            orchestrator.validate_tasks([value])

    def test_rejects_empty_stage_acceptance_tests(self):
        value = task()
        value["execution_mode"] = "STAGED"
        value["stages"] = [
            {"title": "first", "acceptance_tests": []},
            {"title": "second"},
        ]
        with self.assertRaises(orchestrator.OrchestratorError):
            orchestrator.validate_tasks([value])

    def test_tiny_source_typo_can_use_low_effort(self):
        value = task()
        value["title"] = "UI文言の誤字を1箇所修正"
        value["files_hint"] = ["src/apps/cad_next/PropertyPanel.cpp"]
        policy = orchestrator.determine_policy(value)
        self.assertEqual(policy.execution_mode, "BATCH")
        self.assertEqual(policy.review_effort, "LOW")

    def test_staged_middle_boundary_can_skip_independent_review(self):
        value = task()
        value["execution_mode"] = "STAGED"
        value["stages"] = [
            {"title": "foundation", "review_after": True},
            {"title": "mechanical", "review_after": False},
            {"title": "integration", "review_after": True},
        ]
        stages = orchestrator.stages_for_task(value, orchestrator.determine_policy(value))
        self.assertEqual([stage.review_after for stage in stages], [True, False, True])

    def test_review_profile_falls_back_to_nearest_supported_effort(self):
        config = {
            "supported_codex_reasoning_efforts": ["low", "medium", "high"],
            "review_profiles": {
                name: {"reasoning_effort": effort, "prompt_depth": name}
                for name, effort in {
                    "LOW": "low", "MEDIUM": "medium", "HIGH": "high",
                    "EXTRA_HIGH": "xhigh",
                }.items()
            },
        }
        actual, depth = orchestrator.resolve_review_profile("EXTRA_HIGH", config)
        self.assertEqual(actual, "high")
        self.assertEqual(depth, "EXTRA_HIGH")

    def test_review_profile_tie_prefers_higher_effort(self):
        config = {
            "supported_codex_reasoning_efforts": ["low", "high"],
            "review_profiles": {
                name: {"reasoning_effort": effort, "prompt_depth": name}
                for name, effort in {
                    "LOW": "low", "MEDIUM": "medium", "HIGH": "high",
                    "EXTRA_HIGH": "xhigh",
                }.items()
            },
        }
        actual, _ = orchestrator.resolve_review_profile("MEDIUM", config)
        self.assertEqual(actual, "high")

    def test_diff_can_escalate_mode_and_review_effort(self):
        paths = [f"src/apps/cad_next/Part{index}.cpp" for index in range(10)]
        numstat = "".join(f"100\t20\t{path}\n" for path in paths)
        metrics = orchestrator.diff_metrics(numstat, paths)
        mode, reason = orchestrator.promoted_mode("BATCH", metrics)
        effort, _ = orchestrator.effort_after_diff("MEDIUM", metrics)
        self.assertEqual(mode, "STAGED")
        self.assertIsNotNone(reason)
        self.assertIn(effort, {"HIGH", "EXTRA_HIGH"})

    def test_reviewer_command_uses_logical_profile_without_model_name(self):
        config = {
            "supported_codex_reasoning_efforts": ["low", "medium", "high"],
            "review_profiles": {
                name: {"reasoning_effort": effort, "prompt_depth": f"depth-{name}"}
                for name, effort in {
                    "LOW": "low", "MEDIUM": "medium", "HIGH": "high",
                    "EXTRA_HIGH": "xhigh",
                }.items()
            },
        }
        command = orchestrator.reviewer_command(Path("worktree"), Path("packet"),
            Path("final"), "codex", "HIGH", config)
        self.assertIn('model_reasoning_effort="high"', command.argv)
        self.assertIn("Review Effort is HIGH", command.argv[-1])
        self.assertNotIn("gpt-", " ".join(command.argv).lower())

    def test_review_config_file_is_valid(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "config.json"
            path.write_text(json.dumps({
                "supported_codex_reasoning_efforts": ["medium"],
                "review_profiles": {
                    name: {"reasoning_effort": "medium", "prompt_depth": name}
                    for name in ("LOW", "MEDIUM", "HIGH", "EXTRA_HIGH")
                },
            }), encoding="utf-8")
            loaded = orchestrator.load_review_config(path)
            self.assertEqual(loaded["supported_codex_reasoning_efforts"], ["medium"])

    def test_review_config_must_be_an_object(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "config.json"
            path.write_text("[]", encoding="utf-8")
            with self.assertRaises(ValueError):
                orchestrator.load_review_config(path)

    def test_shipped_review_config_is_valid(self):
        loaded = orchestrator.load_review_config(ROOT / ".ai" / "ORCHESTRATOR_CONFIG.json")
        self.assertEqual(set(loaded["review_profiles"]),
            {"LOW", "MEDIUM", "HIGH", "EXTRA_HIGH"})

    def test_usage_limit_is_the_only_provider_fallback_trigger(self):
        self.assertTrue(orchestrator._usage_limit("You've hit your usage limit"))
        self.assertFalse(orchestrator._usage_limit("compiler error"))
        self.assertFalse(orchestrator._usage_limit(
            "+ return any(token in value for token in ('usage limit', 'rate limit'))"))
        self.assertFalse(orchestrator._usage_limit("network rate limit"))

    def test_usage_limit_runs_read_only_claude_fallback(self):
        config = {
            "supported_codex_reasoning_efforts": ["medium"],
            "review_profiles": {
                name: {"reasoning_effort": "medium", "prompt_depth": name}
                for name in ("LOW", "MEDIUM", "HIGH", "EXTRA_HIGH")
            },
            "reviewer_fallback": "claude",
        }
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            config_path = root / "config.json"
            config_path.write_text(json.dumps(config), encoding="utf-8")
            final = root / "final.md"
            failure = orchestrator.CommandFailed("limit", "You've hit your usage limit")
            with mock.patch.object(orchestrator, "ORCHESTRATOR_CONFIG_PATH", config_path), \
                    mock.patch.object(orchestrator, "run_command",
                        side_effect=[failure, "PASS\nfallback review"]):
                review, provider = orchestrator.run_reviewer(root, root / "packet.md",
                    final, "MEDIUM", {"codex": "codex", "claude": "claude"},
                    30, root / "review.log")
            self.assertEqual(provider, "claude-fallback")
            self.assertTrue(review.startswith("PASS"))
            self.assertEqual(final.read_text(encoding="utf-8"), review)

    def test_zero_exit_usage_message_without_final_output_uses_fallback(self):
        config = {
            "supported_codex_reasoning_efforts": ["medium"],
            "review_profiles": {
                name: {"reasoning_effort": "medium", "prompt_depth": name}
                for name in ("LOW", "MEDIUM", "HIGH", "EXTRA_HIGH")
            },
            "reviewer_fallback": "claude",
        }
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            config_path = root / "config.json"
            config_path.write_text(json.dumps(config), encoding="utf-8")
            final = root / "final.md"
            with mock.patch.object(orchestrator, "ORCHESTRATOR_CONFIG_PATH", config_path), \
                    mock.patch.object(orchestrator, "run_command",
                        side_effect=["You've hit your usage limit", "PASS\nfallback review"]):
                review, provider = orchestrator.run_reviewer(root, root / "packet.md",
                    final, "MEDIUM", {"codex": "codex", "claude": "claude"},
                    30, root / "review.log")
            self.assertEqual(provider, "claude-fallback")
            self.assertTrue(review.startswith("PASS"))

    def test_non_usage_failure_does_not_fallback(self):
        failure = orchestrator.CommandFailed("review failed", "compiler error")
        with self.assertRaises(orchestrator.CommandFailed):
            orchestrator._maybe_run_claude_reviewer(failure, Path("worktree"),
                Path("packet"), Path("final"), "MEDIUM",
                {"codex": "codex", "claude": "claude"}, 30, Path("log"),
                {"reviewer_fallback": "claude"})

    def test_claude_fallback_command_is_read_only(self):
        config = orchestrator.load_review_config(ROOT / ".ai" / "ORCHESTRATOR_CONFIG.json")
        command = orchestrator.claude_reviewer_command(Path("worktree"), Path("packet"),
            "HIGH", config, "claude")
        self.assertEqual(command.label, "Claude fallback reviewer")
        self.assertIn("plan", command.argv)
        self.assertIn("Review Effort is HIGH", command.argv[-1])

    def test_unsupported_effort_retries_codex_without_effort_flag(self):
        config = {
            "supported_codex_reasoning_efforts": ["high"],
            "review_profiles": {
                name: {"reasoning_effort": "high", "prompt_depth": name}
                for name in ("LOW", "MEDIUM", "HIGH", "EXTRA_HIGH")
            },
        }
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            config_path = root / "config.json"
            config_path.write_text(json.dumps(config), encoding="utf-8")
            final = root / "final.md"
            commands = []

            def run(command, *_args):
                commands.append(command)
                if len(commands) == 1:
                    raise orchestrator.CommandFailed("unsupported",
                        "unknown model_reasoning_effort")
                final.write_text("PASS\nretry worked", encoding="utf-8")
                return ""

            with mock.patch.object(orchestrator, "ORCHESTRATOR_CONFIG_PATH", config_path), \
                    mock.patch.object(orchestrator, "run_command", side_effect=run):
                review, provider = orchestrator.run_reviewer(root, root / "packet.md",
                    final, "HIGH", {"codex": "codex", "claude": "claude"},
                    30, root / "review.log")
            self.assertEqual(provider, "codex")
            self.assertTrue(review.startswith("PASS"))
            self.assertIn("model_reasoning_effort", " ".join(commands[0].argv))
            self.assertNotIn("model_reasoning_effort", " ".join(commands[1].argv))

    def test_guarded_promotion_keeps_explicit_stages_valid_without_reason_growth(self):
        value = task()
        value["execution_mode"] = "STAGED"
        value["review_effort"] = "HIGH"
        value["execution_reason"] = "initial execution"
        value["review_reason"] = "initial review"
        value["stages"] = [{"title": "first"}, {"title": "second"}]
        metrics = orchestrator.diff_metrics("10\t5\tsrc/next/kachakacha/document/Document.cpp\n",
            ["src/next/kachakacha/document/Document.cpp"])
        with mock.patch.object(orchestrator, "save_tasks"):
            policy = orchestrator._escalate_from_diff([value], value,
                orchestrator.determine_policy(value), metrics)
            first_reason = value["review_reason"]
            orchestrator._escalate_from_diff([value], value, policy, metrics)
        orchestrator.validate_tasks([value])
        self.assertEqual(value["execution_mode"], "GUARDED")
        self.assertEqual(value["review_reason"], first_reason)

    def test_claude_self_review_is_read_only(self):
        command = orchestrator.claude_self_review_command(Path("worktree"),
            Path("packet"), "BATCH", "MEDIUM", "claude")
        self.assertIn("plan", command.argv)
        self.assertIn("self-review", command.argv[-1])

    def test_stage_without_boundary_runs_self_review_but_not_independent_review(self):
        value = task()
        policy = orchestrator.determine_policy(value)
        stage = orchestrator.StagePlan("middle", "middle", "objective", (), ("works",), False)
        metrics = orchestrator.DiffMetrics(1, 1, ("src/example.cpp",), 1)
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            runtime = root / "runtime"
            runtime.mkdir()
            runtime_task = runtime / "CURRENT_TASK.md"
            with mock.patch.object(orchestrator, "_write_task_files", return_value="task"), \
                    mock.patch.object(orchestrator, "worktree_fingerprint",
                        side_effect=["before", "after"]), \
                    mock.patch.object(orchestrator, "run_command",
                        side_effect=["worker report", "preamble\nPASS\nself review"]), \
                    mock.patch.object(orchestrator, "_run_gates", return_value=[]), \
                    mock.patch.object(orchestrator, "collect_diff_metrics", return_value=metrics), \
                    mock.patch.object(orchestrator, "_escalate_from_diff", return_value=policy), \
                    mock.patch.object(orchestrator, "collect_git_evidence",
                        return_value=(" M file", "stat", "diff")), \
                    mock.patch.object(orchestrator, "make_review_packet"), \
                    mock.patch.object(orchestrator, "write_public_report"), \
                    mock.patch.object(orchestrator, "set_task_status"), \
                    mock.patch.object(orchestrator, "write_state"), \
                    mock.patch.object(orchestrator, "run_reviewer") as reviewer:
                outcome = orchestrator._run_stage([value], value, policy, stage, 2, 3,
                    root, runtime, runtime_task, {"claude": "claude", "codex": "codex"},
                    30, 1, "codex/control")
            reviewer.assert_not_called()
            self.assertIsNone(outcome.reviewer)

    def test_untracked_content_changes_stage_fingerprint_and_metrics(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            init_test_repo(root)
            source = root / "new-source.cpp"
            source.write_text("first\n", encoding="utf-8")
            before = orchestrator.worktree_fingerprint(root)
            source.write_text("first\nsecond\n", encoding="utf-8")
            after = orchestrator.worktree_fingerprint(root)
            metrics = orchestrator.collect_diff_metrics(root)
            self.assertNotEqual(before, after)
            self.assertIn("new-source.cpp", metrics.paths)
            self.assertEqual(metrics.changed_lines, 2)

    def test_untracked_text_is_included_in_review_evidence(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            runtime = root / "runtime"
            runtime.mkdir()
            init_test_repo(root)
            (root / "new-source.cpp").write_text("review this line\n", encoding="utf-8")
            orchestrator.collect_git_evidence(root, runtime)
            evidence = (runtime / "git-evidence.md").read_text(encoding="utf-8")
            self.assertIn("new-source.cpp", evidence)
            self.assertIn("review this line", evidence)


if __name__ == "__main__":
    unittest.main()
