#!/usr/bin/env python3
"""Small, worktree-first Claude/Codex task orchestrator for kachakachaCAD.

Dry-run is the default. The script intentionally uses fixed argument arrays,
never a shell command string, and never performs destructive Git operations.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import platform
import shutil
import subprocess
import sys
from dataclasses import dataclass
from pathlib import Path
from typing import Any, Iterable, Sequence

try:
    from orchestrator_policy import (
        DiffMetrics,
        StagePlan,
        TaskPolicy,
        determine_policy,
        diff_metrics,
        effort_after_diff,
        load_review_config,
        promoted_mode,
        resolve_review_profile,
        stages_for_task,
        validate_policy_fields,
    )
except ModuleNotFoundError:
    from scripts.orchestrator_policy import (
        DiffMetrics,
        StagePlan,
        TaskPolicy,
        determine_policy,
        diff_metrics,
        effort_after_diff,
        load_review_config,
        promoted_mode,
        resolve_review_profile,
        stages_for_task,
        validate_policy_fields,
    )


REPO_ROOT = Path(__file__).resolve().parents[1]
AI_DIR = REPO_ROOT / ".ai"
TASKS_PATH = AI_DIR / "TASKS.json"
CURRENT_TASK_PATH = AI_DIR / "CURRENT_TASK.md"
STATE_PATH = AI_DIR / "STATE.md"
ORCHESTRATOR_CONFIG_PATH = AI_DIR / "ORCHESTRATOR_CONFIG.json"
ALLOWED_STATUSES = {
    "pending",
    "ready",
    "in_progress",
    "review",
    "revision",
    "passed",
    "blocked",
}
REQUIRED_TASK_FIELDS = {
    "id",
    "title",
    "phase",
    "status",
    "priority",
    "assigned_to",
    "dependencies",
    "spec_refs",
    "files_hint",
    "acceptance_tests",
    "last_review",
}
DANGEROUS_BRANCHES = {"main", "master"}
DEFAULT_TIMEOUT_SECONDS = 1800
MAX_ALLOWED_REVISIONS = 3


class OrchestratorError(RuntimeError):
    """Expected stop condition with a user-facing explanation."""


class CommandFailed(OrchestratorError):
    """External command failure with captured output for bounded fallback decisions."""

    def __init__(self, message: str, output: str) -> None:
        super().__init__(message)
        self.output = output


def configure_console_encoding() -> None:
    """Keep Japanese task/report text readable in captured Windows output."""
    for stream in (sys.stdout, sys.stderr):
        reconfigure = getattr(stream, "reconfigure", None)
        if reconfigure is not None:
            reconfigure(encoding="utf-8", errors="replace")


@dataclass(frozen=True)
class PlannedCommand:
    label: str
    argv: tuple[str, ...]
    cwd: Path


@dataclass(frozen=True)
class StageOutcome:
    worker_report: str
    command_results: tuple[tuple[PlannedCommand, str], ...]
    stat: str
    policy: TaskPolicy
    reviewer: str | None


def load_tasks(path: Path = TASKS_PATH) -> list[dict[str, Any]]:
    try:
        data = json.loads(path.read_text(encoding="utf-8"))
    except FileNotFoundError as exc:
        raise OrchestratorError(f"Task ledger not found: {path}") from exc
    except json.JSONDecodeError as exc:
        raise OrchestratorError(f"Invalid task ledger JSON: {exc}") from exc
    if not isinstance(data, list):
        raise OrchestratorError("TASKS.json must contain one JSON array")
    return data


def validate_tasks(tasks: list[dict[str, Any]]) -> None:
    ids: list[str] = []
    for index, task in enumerate(tasks):
        if not isinstance(task, dict):
            raise OrchestratorError(f"Task #{index + 1} must be an object")
        missing = REQUIRED_TASK_FIELDS - set(task)
        if missing:
            raise OrchestratorError(
                f"Task #{index + 1} is missing: {', '.join(sorted(missing))}"
            )
        task_id = task["id"]
        if not isinstance(task_id, str) or not task_id.strip():
            raise OrchestratorError(f"Task #{index + 1} has an invalid id")
        if task_id in ids:
            raise OrchestratorError(f"Duplicate task id: {task_id}")
        ids.append(task_id)
        if task["status"] not in ALLOWED_STATUSES:
            raise OrchestratorError(
                f"{task_id} has unsupported status: {task['status']}"
            )
        _validate_string_list(task_id, task, "dependencies")
        _validate_string_list(task_id, task, "spec_refs", require_values=True)
        _validate_string_list(task_id, task, "files_hint", require_values=True)
        _validate_string_list(task_id, task, "acceptance_tests", require_values=True)
        if len(task["files_hint"]) > 15:
            raise OrchestratorError(f"{task_id} exceeds the fifteen-file task limit")
        try:
            validate_policy_fields(task)
        except ValueError as exc:
            raise OrchestratorError(f"{task_id} has invalid execution policy: {exc}") from exc
        revisions = task.get("revision_count", 0)
        if not isinstance(revisions, int) or revisions < 0:
            raise OrchestratorError(f"{task_id} has an invalid revision_count")
    _validate_dependencies(tasks, set(ids))


def _validate_string_list(
    task_id: str,
    task: dict[str, Any],
    field: str,
    require_values: bool = False,
) -> None:
    value = task[field]
    if not isinstance(value, list) or not all(isinstance(item, str) for item in value):
        raise OrchestratorError(f"{task_id}.{field} must be a string array")
    if require_values and not value:
        raise OrchestratorError(f"{task_id}.{field} must not be empty")


def _validate_dependencies(tasks: list[dict[str, Any]], known_ids: set[str]) -> None:
    graph = {task["id"]: task["dependencies"] for task in tasks}
    for task_id, dependencies in graph.items():
        for dependency in dependencies:
            if dependency not in known_ids:
                raise OrchestratorError(f"{task_id} has unknown dependency: {dependency}")
            if dependency == task_id:
                raise OrchestratorError(f"{task_id} depends on itself")
    visiting: set[str] = set()
    visited: set[str] = set()

    def visit(task_id: str) -> None:
        if task_id in visiting:
            raise OrchestratorError(f"Task dependency cycle includes {task_id}")
        if task_id in visited:
            return
        visiting.add(task_id)
        for dependency in graph[task_id]:
            visit(dependency)
        visiting.remove(task_id)
        visited.add(task_id)

    for task_id in graph:
        visit(task_id)


def resolve_task(tasks: list[dict[str, Any]], requested_id: str | None) -> dict[str, Any]:
    by_id = {task["id"]: task for task in tasks}
    if requested_id:
        task = by_id.get(requested_id)
        if task is None:
            raise OrchestratorError(f"Unknown task: {requested_id}")
        if task["status"] not in {"ready", "revision"}:
            raise OrchestratorError(
                f"{requested_id} is {task['status']}, expected ready or revision"
            )
    else:
        candidates = [task for task in tasks if task["status"] == "ready"]
        if not candidates:
            raise OrchestratorError("No ready task exists")
        priorities = {"high": 0, "medium": 1, "low": 2}
        candidates.sort(
            key=lambda item: (priorities.get(item["priority"], 9), item["phase"], item["id"])
        )
        task = candidates[0]
    for dependency in task["dependencies"]:
        if by_id[dependency]["status"] != "passed":
            raise OrchestratorError(
                f"{task['id']} dependency is not passed: {dependency}"
            )
    return task


def render_current_task(
    task: dict[str, Any],
    policy: TaskPolicy | None = None,
    stage: StagePlan | None = None,
    stage_number: int = 1,
    stage_count: int = 1,
) -> str:
    policy = policy or determine_policy(task)
    objective = task.get("objective", task["title"])
    background = task.get("background", "See the referenced specifications and current code.")
    forbidden = task.get(
        "forbidden_changes",
        ["Do not change files unrelated to this task", "Do not change V1"],
    )
    work_steps = task.get(
        "work_steps",
        ["Inspect the relevant code", "Implement the smallest change", "Add tests"],
    )
    requirements = task.get("requirements")
    requirement_text = (
        _markdown_list(requirements)
        if requirements
        else _markdown_list(task["spec_refs"], code=True)
    )
    review_reason = policy.review_reason
    if task.get("review_diff_reason"):
        review_reason += f"\n\nLatest diff assessment: {task['review_diff_reason']}"
    sections = [
        ("TASK ID", task["id"]),
        ("Execution Mode", policy.execution_mode),
        ("Review Effort", policy.review_effort),
        ("Execution Reason", policy.execution_reason),
        ("Review Reason", review_reason),
        ("目的", objective),
        ("背景", background),
        ("変更対象候補", _markdown_list(task["files_hint"], code=True)),
        ("変更禁止範囲", _markdown_list(forbidden)),
        ("必須仕様", requirement_text),
        ("作業手順", _numbered_list(work_steps)),
        ("Acceptance Tests", _markdown_list(
            stage.acceptance_tests if stage is not None else task["acceptance_tests"])),
        (
            "完了条件",
            "全Acceptance Testsとローカル検証が成功し、無関係な変更がないこと。",
        ),
        (
            "報告形式",
            "1. 変更ファイル一覧\n2. 実装内容\n3. Build結果\n4. Test結果\n"
            "5. git diff要約\n6. 残存問題",
        ),
    ]
    if stage is not None:
        stage_body = (
            f"{stage_number}/{stage_count}: {stage.stage_id} - {stage.title}\n\n"
            f"{stage.objective}\n\n{_numbered_list(stage.work_steps)}"
        )
        sections.insert(5, ("Current Stage", stage_body))
    return "\n\n".join(f"# {title}\n\n{body}" for title, body in sections) + "\n"


def _markdown_list(items: Iterable[str], code: bool = False) -> str:
    if code:
        return "\n".join(f"- `{item}`" for item in items)
    return "\n".join(f"- {item}" for item in items)


def _numbered_list(items: Iterable[str]) -> str:
    return "\n".join(f"{index}. {item}" for index, item in enumerate(items, 1))


def command_environment() -> dict[str, str]:
    """Collapse Windows Path/PATH duplicates before spawning CMake."""
    if os.name != "nt":
        return dict(os.environ)
    result: dict[str, str] = {}
    path_value: str | None = None
    for key, value in os.environ.items():
        if key.lower() == "path":
            if key == "Path" or path_value is None:
                path_value = value
            continue
        result[key] = value
    if path_value is not None:
        result["Path"] = path_value
    return result


def detect_tools() -> dict[str, str | None]:
    names = ["claude", "codex", "git", "cmake", "ctest"]
    if os.name == "nt":
        names.append("pwsh")
    tools = {name: shutil.which(name) for name in names}
    if not tools["claude"]:
        tools["claude"] = _find_installed_claude()
    return tools


def _find_installed_claude() -> str | None:
    candidates: list[Path] = []
    local_app_data = os.environ.get("LOCALAPPDATA")
    if local_app_data:
        candidates.append(
            Path(local_app_data) / "Microsoft" / "WinGet" / "Links" / "claude.exe"
        )
    candidates.extend(
        [
            Path.home() / ".local" / "bin" / "claude.exe",
            Path.home() / ".local" / "bin" / "claude",
        ]
    )
    # WinGet uses a symlink whose target can be unreadable in a planning sandbox.
    # lexists checks the link itself; execute mode still fails safely if it cannot run.
    return next((str(path) for path in candidates if os.path.lexists(path)), None)


def require_tools(tools: dict[str, str | None]) -> None:
    if not tools.get("claude"):
        raise OrchestratorError("Claude Code not found")
    missing = [name for name, path in tools.items() if name != "claude" and not path]
    if missing:
        raise OrchestratorError(f"Required command not found: {', '.join(missing)}")


def run_command(
    command: PlannedCommand,
    timeout_seconds: int,
    log_path: Path | None = None,
) -> str:
    try:
        completed = subprocess.run(
            list(command.argv),
            cwd=command.cwd,
            env=command_environment(),
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            text=True,
            encoding="utf-8",
            errors="replace",
            timeout=timeout_seconds,
            check=False,
        )
    except subprocess.TimeoutExpired as exc:
        raise OrchestratorError(
            f"{command.label} timed out after {timeout_seconds} seconds"
        ) from exc
    except OSError as exc:
        raise OrchestratorError(
            f"{command.label} could not start: {exc.__class__.__name__}: {exc}"
        ) from exc
    output = completed.stdout or ""
    if log_path is not None:
        log_path.parent.mkdir(parents=True, exist_ok=True)
        log_path.write_text(output, encoding="utf-8")
    if completed.returncode != 0:
        raise CommandFailed(
            f"{command.label} failed with exit code {completed.returncode}", output
        )
    return output


def git_output(args: Sequence[str], cwd: Path = REPO_ROOT) -> str:
    command = PlannedCommand("git", tuple(["git", *args]), cwd)
    return run_command(command, 120).strip()


def current_branch() -> str:
    branch = git_output(["branch", "--show-current"])
    if not branch:
        raise OrchestratorError("Detached HEAD is not supported")
    return branch


def ensure_control_checkout_safe() -> str:
    branch = current_branch()
    tracked = git_output(["status", "--porcelain", "--untracked-files=no"])
    if tracked:
        raise OrchestratorError(
            "Tracked changes exist in the control checkout; commit or resolve them first"
        )
    return branch


def task_branch(task_id: str) -> str:
    return f"codex/ai-{task_id.lower()}"


def worktree_path(task_id: str) -> Path:
    return REPO_ROOT.parent / f"{REPO_ROOT.name}-worktrees" / task_id.lower()


def prepare_worktree(task_id: str, base_ref: str) -> Path:
    path = worktree_path(task_id)
    branch = task_branch(task_id)
    if path.exists():
        actual = git_output(["-C", str(path), "branch", "--show-current"])
        if actual != branch:
            raise OrchestratorError(
                f"Existing worktree uses {actual}, expected {branch}: {path}"
            )
        return path
    path.parent.mkdir(parents=True, exist_ok=True)
    branch_exists = subprocess.run(
        ["git", "show-ref", "--verify", "--quiet", f"refs/heads/{branch}"],
        cwd=REPO_ROOT,
        env=command_environment(),
        check=False,
    ).returncode == 0
    args = ["worktree", "add"]
    if branch_exists:
        args.extend([str(path), branch])
    else:
        args.extend(["-b", branch, str(path), base_ref])
    git_output(args)
    return path


def build_and_test_plan(worktree: Path) -> list[PlannedCommand]:
    if os.name == "nt":
        executable = worktree / "build-msvc2022-x64" / "Release" / "kachakacha_cad_next.exe"
        return [
            PlannedCommand("configure", ("cmake", "--preset", "windows-msvc"), worktree),
            PlannedCommand(
                "build",
                ("cmake", "--build", "--preset", "windows-msvc", "--parallel"),
                worktree,
            ),
            PlannedCommand("CTest", ("ctest", "--preset", "windows-msvc"), worktree),
            PlannedCommand("application self-test", (str(executable), "--self-test"), worktree),
        ]
    return [
        PlannedCommand("configure", ("cmake", "--preset", "linux-core"), worktree),
        PlannedCommand(
            "build",
            ("cmake", "--build", "--preset", "linux-core", "--parallel"),
            worktree,
        ),
        PlannedCommand("CTest", ("ctest", "--preset", "linux-core"), worktree),
    ]


def worker_command(
    worktree: Path,
    task_file: Path,
    revision_file: Path | None,
    execution_mode: str = "BATCH",
    stage: StagePlan | None = None,
    executable: str = "claude",
) -> PlannedCommand:
    prompt = (
        "Read .ai/prompts/CLAUDE_WORKER.md and AGENTS.md. "
        f"The active assignment is {task_file}. Implement only that task in this worktree. "
        f"Execution Mode is {execution_mode}. "
        "Do not commit or push. Finish with the required worker report and self-review checklist."
    )
    if stage is not None:
        prompt += f" Implement only Current Stage {stage.stage_id} ({stage.title}) in this run."
    if revision_file is not None:
        prompt += f" This is a revision run; also read {revision_file}."
    return PlannedCommand(
        "Claude worker",
        (
            executable,
            "-p",
            "--permission-mode",
            "acceptEdits",
            "--permission-prompts",
            "none",
            prompt,
        ),
        worktree,
    )


def reviewer_command(
    worktree: Path,
    packet_path: Path,
    final_output_path: Path,
    executable: str = "codex",
    review_effort: str = "MEDIUM",
    review_config: dict[str, Any] | None = None,
    use_effort_flag: bool = True,
) -> PlannedCommand:
    config = review_config or read_review_config()
    actual_effort, depth = resolve_review_profile(review_effort, config)
    prompt = (
        "Read .ai/prompts/CODEX_REVIEWER.md and the review packet at "
        f"{packet_path}. Review only; do not edit. Review Effort is {review_effort}. "
        f"Depth instruction: {depth} "
        "The first non-empty line must be PASS or REVISE."
    )
    effort_arguments = (
        ("-c", f'model_reasoning_effort="{actual_effort}"')
        if use_effort_flag else ()
    )
    return PlannedCommand(
        "Codex reviewer",
        (
            executable,
            "exec",
            *effort_arguments,
            "--ephemeral",
            "--sandbox",
            "read-only",
            "--output-last-message",
            str(final_output_path),
            "-C",
            str(worktree),
            prompt,
        ),
        worktree,
    )


def claude_reviewer_command(
    worktree: Path,
    packet_path: Path,
    review_effort: str,
    review_config: dict[str, Any],
    executable: str,
) -> PlannedCommand:
    _, depth = resolve_review_profile(review_effort, review_config)
    prompt = (
        "Read .ai/prompts/CODEX_REVIEWER.md and the review packet at "
        f"{packet_path}. Review only; do not edit. You are the fallback independent reviewer. "
        f"Review Effort is {review_effort}. Depth instruction: {depth} "
        "The first non-empty line must be PASS or REVISE."
    )
    return PlannedCommand(
        "Claude fallback reviewer",
        (executable, "-p", "--permission-mode", "plan", "--permission-prompts", "none", prompt),
        worktree,
    )


def claude_self_review_command(
    worktree: Path,
    packet_path: Path,
    execution_mode: str,
    review_effort: str,
    executable: str,
) -> PlannedCommand:
    prompt = (
        "Read .ai/prompts/CLAUDE_WORKER.md and the completed-work packet at "
        f"{packet_path}. Perform the mandatory worker self-review for {execution_mode} "
        f"with review depth {review_effort}. Do not edit in this review pass. Compare CURRENT_TASK, "
        "master specs, complete diff, diff stat, build/tests, TODO/stubs, unrelated changes, "
        "error handling, and regressions. The first non-empty line must be PASS or REVISE. "
        "Use REVISE only with concrete corrections for the next worker run."
    )
    return PlannedCommand(
        "Claude self-review",
        (executable, "-p", "--permission-mode", "plan", "--permission-prompts", "none", prompt),
        worktree,
    )


def read_review_config() -> dict[str, Any]:
    try:
        return load_review_config(ORCHESTRATOR_CONFIG_PATH)
    except (OSError, ValueError) as exc:
        raise OrchestratorError(f"Invalid orchestrator review config: {exc}") from exc


def run_reviewer(
    worktree: Path,
    packet_path: Path,
    final_output_path: Path,
    review_effort: str,
    tools: dict[str, str | None],
    timeout_seconds: int,
    log_path: Path,
) -> tuple[str, str]:
    config = read_review_config()
    final_output_path.unlink(missing_ok=True)
    command = reviewer_command(worktree, packet_path, final_output_path,
        tools["codex"] or "codex", review_effort, config)
    command_output = ""
    try:
        command_output = run_command(command, timeout_seconds, log_path)
    except CommandFailed as exc:
        if _unsupported_effort(exc.output):
            command = reviewer_command(worktree, packet_path, final_output_path,
                tools["codex"] or "codex", review_effort, config, False)
            try:
                command_output = run_command(command, timeout_seconds, log_path)
            except CommandFailed as retry_exc:
                return _maybe_run_claude_reviewer(retry_exc, worktree, packet_path,
                    final_output_path, review_effort, tools, timeout_seconds, log_path, config)
        else:
            return _maybe_run_claude_reviewer(exc, worktree, packet_path,
                final_output_path, review_effort, tools, timeout_seconds, log_path, config)
    if not final_output_path.exists():
        missing = CommandFailed("Codex reviewer did not write a final decision", command_output)
        if _usage_limit(command_output):
            return _maybe_run_claude_reviewer(missing, worktree, packet_path,
                final_output_path, review_effort, tools, timeout_seconds, log_path, config)
        raise missing
    return final_output_path.read_text(encoding="utf-8"), "codex"


def _maybe_run_claude_reviewer(
    failure: CommandFailed,
    worktree: Path,
    packet_path: Path,
    final_output_path: Path,
    review_effort: str,
    tools: dict[str, str | None],
    timeout_seconds: int,
    log_path: Path,
    config: dict[str, Any],
) -> tuple[str, str]:
    if not _usage_limit(failure.output) or config.get("reviewer_fallback") != "claude":
        raise failure
    fallback = claude_reviewer_command(worktree, packet_path, review_effort, config,
        tools["claude"] or "claude")
    output = run_command(fallback, timeout_seconds, log_path.with_name(log_path.stem + "-claude.log"))
    final_output_path.write_text(output, encoding="utf-8")
    return output, "claude-fallback"


def _usage_limit(output: str) -> bool:
    known_prefixes = (
        "you've hit your usage limit",
        "you have hit your usage limit",
        "usage limit reached",
        "error: usage limit",
        "error: you've hit your usage limit",
    )
    lines = (line.strip().lower() for line in output.splitlines())
    return any(line.startswith(known_prefixes) for line in lines)


def _unsupported_effort(output: str) -> bool:
    value = output.lower()
    effort_words = ("reasoning effort", "model_reasoning_effort")
    failure_words = ("unsupported", "unknown", "invalid")
    return any(word in value for word in effort_words) and any(word in value for word in failure_words)


def format_command(command: PlannedCommand) -> str:
    def quote(value: str) -> str:
        return f'"{value}"' if any(character.isspace() for character in value) else value

    return " ".join(quote(value) for value in command.argv)


def dry_run(task: dict[str, Any], base_ref: str, tools: dict[str, str | None]) -> None:
    branch = ensure_control_checkout_safe()
    policy = determine_policy(task)
    stages = stages_for_task(task, policy)
    review_config = read_review_config()
    actual_effort, _ = resolve_review_profile(policy.review_effort, review_config)
    worktree = worktree_path(task["id"])
    runtime = worktree / ".ai" / "runtime" / task["id"]
    task_file = runtime / "CURRENT_TASK.md"
    print("DRY RUN: no files, branches, worktrees, or agents will be changed")
    print(f"Task: {task['id']} - {task['title']}")
    print(f"Control branch: {branch}")
    print(f"Task branch: {task_branch(task['id'])}")
    print(f"Worktree: {worktree}")
    print(f"Execution Mode: {policy.execution_mode}")
    print(f"Execution Reason: {policy.execution_reason}")
    print(f"Review Effort: {policy.review_effort} (Codex: {actual_effort})")
    print(f"Review Reason: {policy.review_reason}")
    print("Stages: " + ", ".join(
        f"{stage.stage_id}{' [review]' if stage.review_after else ''}" for stage in stages))
    print(f"Current task bytes: {len(render_current_task(task, policy, stages[0], 1, len(stages)).encode('utf-8'))}")
    print(f"PLAN git worktree add -b {task_branch(task['id'])} {worktree} {base_ref}")
    worker = worker_command(worktree, task_file, None, policy.execution_mode, stages[0],
        tools.get("claude") or "claude")
    print(f"PLAN [{worker.label}] {format_command(worker)}")
    for command in build_and_test_plan(worktree):
        print(f"PLAN [{command.label}] {format_command(command)}")
    review = reviewer_command(
        worktree,
        runtime / "review-packet.md",
        runtime / "review-final.md",
        tools.get("codex") or "codex",
        policy.review_effort,
        review_config,
    )
    print(f"PLAN [{review.label}] {format_command(review)}")
    for name, path in tools.items():
        state = path if path else "NOT FOUND (execute mode will stop)"
        print(f"TOOL {name}: {state}")


def save_tasks(tasks: list[dict[str, Any]]) -> None:
    TASKS_PATH.write_text(
        json.dumps(tasks, ensure_ascii=False, indent=2) + "\n",
        encoding="utf-8",
    )


def set_task_status(
    tasks: list[dict[str, Any]],
    task: dict[str, Any],
    status: str,
    blocked_reason: str | None = None,
) -> None:
    if status not in ALLOWED_STATUSES:
        raise OrchestratorError(f"Internal unsupported status: {status}")
    task["status"] = status
    task["assigned_to"] = "claude" if status in {"in_progress", "revision"} else task.get("assigned_to")
    if blocked_reason:
        task["blocked_reason"] = blocked_reason
    else:
        task.pop("blocked_reason", None)
    save_tasks(tasks)


def write_state(task: dict[str, Any], branch: str, note: str) -> None:
    last_passed = "none"
    for item in load_tasks():
        if item["status"] == "passed":
            last_passed = item["id"]
    text = (
        "# AI orchestration state\n\n"
        f"- Current branch: `{branch}`\n"
        f"- Current phase: `{task['phase']}`\n"
        f"- Current task ID: `{task['id']}`\n"
        f"- Execution Mode: `{task.get('execution_mode', 'legacy')}`\n"
        f"- Review Effort: `{task.get('review_effort', 'legacy')}`\n"
        f"- Current Stage: `{task.get('current_stage', 'none')}`\n"
        f"- Last PASS task: `{last_passed}`\n"
        f"- Current status: `{task['status']}`\n"
        f"- Current note: {note}\n\n"
        "## Important design decisions\n\n"
        "- `docs/v2/ui-ux-integrated-spec.md` is the master UI specification.\n"
        "- `.ai/TASKS.json` is the only AI task ledger.\n"
        "- One task uses one sibling worktree and one task branch.\n"
        "- Automatic commit and push are off unless explicitly requested.\n"
        "- V1 remains available as a behavioral reference.\n\n"
        "## Next action\n\n"
        "Inspect the current report, review, task worktree, and ledger status before continuing.\n"
    )
    STATE_PATH.write_text(text, encoding="utf-8")


def collect_git_evidence(worktree: Path, runtime: Path) -> tuple[str, str, str]:
    status = git_output(["-C", str(worktree), "status", "--short"])
    stat = git_output(["-C", str(worktree), "diff", "--stat", "HEAD", "--"])
    diff = git_output(
        ["-C", str(worktree), "diff", "--no-ext-diff", "HEAD", "--"]
    )
    untracked_paths = collect_untracked_paths(worktree)
    untracked = "\n".join(untracked_paths)
    untracked_evidence = render_untracked_evidence(worktree, untracked_paths)
    if untracked_paths:
        stat = stat + ("\n" if stat else "") + "\n".join(
            f" {path} | new untracked file" for path in untracked_paths)
    evidence = f"# Status\n\n```text\n{status}\n```\n\n# Diff stat\n\n```text\n{stat}\n```\n\n"
    evidence += f"# Diff\n\n```diff\n{diff}\n```\n\n# Untracked files\n\n```text\n{untracked}\n```\n"
    evidence += untracked_evidence
    (runtime / "git-evidence.md").write_text(evidence, encoding="utf-8")
    return status, stat, diff


def require_worker_changes(status: str) -> None:
    if not status.strip():
        raise OrchestratorError(
            "Claude worker completed without changing the task worktree"
        )


def worktree_fingerprint(worktree: Path) -> str:
    status = git_output(["-C", str(worktree), "status", "--short"])
    tracked = git_output(["-C", str(worktree), "diff", "--name-only", "HEAD", "--"])
    paths = {path for path in tracked.splitlines() if path}
    paths.update(collect_untracked_paths(worktree))
    hashes = "\n".join(f"{path}\t{hash_file(worktree / path)}" for path in sorted(paths))
    return status + "\n" + hashes


def require_new_worker_changes(before: str, after: str) -> None:
    require_worker_changes(after)
    if before == after:
        raise OrchestratorError("Claude worker completed without a new stage change")


def collect_diff_metrics(worktree: Path) -> DiffMetrics:
    numstat = git_output(["-C", str(worktree), "diff", "--numstat", "HEAD", "--"])
    names = git_output(["-C", str(worktree), "diff", "--name-only", "HEAD", "--"])
    paths = [line for line in names.splitlines() if line.strip()]
    untracked = collect_untracked_paths(worktree)
    paths.extend(path for path in untracked if path not in paths)
    extra_numstat = untracked_numstat(worktree, untracked)
    if extra_numstat:
        numstat = numstat + ("\n" if numstat else "") + extra_numstat
    return diff_metrics(numstat, paths)


def collect_untracked_paths(worktree: Path) -> tuple[str, ...]:
    output = git_output(
        ["-C", str(worktree), "ls-files", "-z", "--others", "--exclude-standard"]
    )
    return tuple(path for path in output.split("\0") if path)


def hash_file(path: Path) -> str:
    digest = hashlib.sha256()
    try:
        with path.open("rb") as stream:
            for chunk in iter(lambda: stream.read(1024 * 1024), b""):
                digest.update(chunk)
    except OSError as exc:
        return f"unreadable:{exc.__class__.__name__}"
    return digest.hexdigest()


def untracked_numstat(worktree: Path, paths: Sequence[str]) -> str:
    rows = []
    for relative in paths:
        try:
            data = (worktree / relative).read_bytes()
        except OSError:
            rows.append(f"-\t-\t{relative}")
            continue
        if b"\0" in data:
            rows.append(f"-\t-\t{relative}")
            continue
        line_count = data.count(b"\n") + (1 if data and not data.endswith(b"\n") else 0)
        rows.append(f"{line_count}\t0\t{relative}")
    return "\n".join(rows)


def render_untracked_evidence(worktree: Path, paths: Sequence[str]) -> str:
    sections = []
    limit = 256 * 1024
    for relative in paths:
        path = worktree / relative
        try:
            data = path.read_bytes()
        except OSError as exc:
            sections.append(f"\n## `{relative}`\n\nUnreadable: {exc.__class__.__name__}\n")
            continue
        digest = hashlib.sha256(data).hexdigest()
        header = f"\n## `{relative}`\n\nSize: {len(data)} bytes; SHA-256: `{digest}`\n"
        if len(data) > limit or b"\0" in data:
            sections.append(header + "\nContent omitted because the file is binary or over 256 KiB.\n")
        else:
            sections.append(header + f"\n```text\n{data.decode('utf-8', errors='replace')}\n```\n")
    return "\n# Untracked file evidence\n" + "".join(sections) if sections else ""


def persist_policy(task: dict[str, Any], policy: TaskPolicy) -> None:
    task["execution_mode"] = policy.execution_mode
    task["review_effort"] = policy.review_effort
    task["execution_reason"] = policy.execution_reason
    task["review_reason"] = policy.review_reason


def make_review_packet(
    task_text: str,
    worker_report: str,
    command_results: list[tuple[PlannedCommand, str]],
    evidence_path: Path,
    packet_path: Path,
) -> None:
    verification = "\n".join(
        f"- PASS `{format_command(command)}`" for command, _ in command_results
    )
    packet = (
        "# Current task\n\n"
        f"{task_text}\n"
        "# Worker report\n\n"
        f"{worker_report}\n\n"
        "# Verification\n\n"
        f"{verification}\n\n"
        "# Git evidence\n\n"
        f"Read the complete evidence at `{evidence_path}` and inspect any listed untracked file.\n"
    )
    packet_path.write_text(packet, encoding="utf-8")


def parse_review_decision(review: str) -> str:
    verdicts = [line.strip() for line in review.splitlines()
        if line.strip() in {"PASS", "REVISE"}]
    if "REVISE" in verdicts:
        return "REVISE"
    if "PASS" in verdicts:
        return "PASS"
    raise OrchestratorError("Reviewer output must contain a standalone PASS or REVISE line")


def write_public_report(
    task: dict[str, Any],
    worker_report: str,
    commands: list[tuple[PlannedCommand, str]],
    stat: str,
) -> None:
    lines = [f"# {task['id']} worker report", "", worker_report.strip(), "", "## Local gates", ""]
    lines.extend(f"- PASS `{format_command(command)}`" for command, _ in commands)
    lines.extend(["", "## Diff stat", "", "```text", stat, "```", ""])
    path = AI_DIR / "reports" / f"{task['id']}.md"
    path.write_text("\n".join(lines), encoding="utf-8")


def promote_ready_tasks(tasks: list[dict[str, Any]]) -> None:
    by_id = {task["id"]: task for task in tasks}
    for task in tasks:
        if task["status"] != "pending":
            continue
        if all(by_id[dependency]["status"] == "passed" for dependency in task["dependencies"]):
            task["status"] = "ready"
    save_tasks(tasks)


def commit_passed_worktree(task: dict[str, Any], worktree: Path) -> None:
    branch = git_output(["-C", str(worktree), "branch", "--show-current"])
    if branch in DANGEROUS_BRANCHES or branch != task_branch(task["id"]):
        raise OrchestratorError(f"Refusing automatic commit on branch: {branch}")
    git_output(["-C", str(worktree), "add", "--all"])
    git_output(
        ["-C", str(worktree), "commit", "-m", f"{task['id']} {task['title']}"]
    )


def _run_gates(
    worktree: Path,
    runtime: Path,
    stage_number: int,
    attempt: int,
    timeout_seconds: int,
) -> list[tuple[PlannedCommand, str]]:
    results: list[tuple[PlannedCommand, str]] = []
    for index, command in enumerate(build_and_test_plan(worktree)):
        log = runtime / f"stage-{stage_number}-attempt-{attempt}-gate-{index}-{command.label}.log"
        results.append((command, run_command(command, timeout_seconds, log)))
    return results


def _escalate_from_diff(
    tasks: list[dict[str, Any]],
    task: dict[str, Any],
    policy: TaskPolicy,
    metrics: DiffMetrics,
) -> TaskPolicy:
    mode, mode_reason = promoted_mode(policy.execution_mode, metrics)
    effort, effort_reason = effort_after_diff(policy.review_effort, metrics)
    if mode == "GUARDED" and effort in {"LOW", "MEDIUM"}:
        effort = "HIGH"
        effort_reason += "; GUARDEDのためHIGHを下限とする"
    if mode_reason:
        task["execution_reason"] = policy.execution_reason + "; 自動昇格: " + mode_reason
    task["execution_mode"] = mode
    task["review_effort"] = effort
    if effort != policy.review_effort:
        task["review_reason"] = policy.review_reason + "; 自動昇格: " + effort_reason
    else:
        task["review_reason"] = policy.review_reason
    task["review_diff_reason"] = effort_reason
    save_tasks(tasks)
    return determine_policy(task)


def _write_task_files(
    task: dict[str, Any],
    policy: TaskPolicy,
    stage: StagePlan,
    stage_number: int,
    stage_count: int,
    runtime_task: Path,
) -> str:
    task_text = render_current_task(task, policy, stage, stage_number, stage_count)
    runtime_task.write_text(task_text, encoding="utf-8")
    CURRENT_TASK_PATH.write_text(task_text, encoding="utf-8")
    return task_text


def _review_path(task_id: str, stage: StagePlan, final_stage: bool) -> Path:
    suffix = "" if final_stage else f"-{stage.stage_id}"
    return AI_DIR / "reviews" / f"{task_id}{suffix}.md"


def _run_stage(
    tasks: list[dict[str, Any]], task: dict[str, Any], policy: TaskPolicy,
    stage: StagePlan, stage_number: int, stage_count: int, worktree: Path,
    runtime: Path, runtime_task: Path, tools: dict[str, str | None],
    timeout_seconds: int, max_revisions: int, control_branch: str,
    resume_existing: bool = False,
) -> StageOutcome:
    revision_file: Path | None = None
    for attempt in range(max_revisions + 1):
        set_task_status(tasks, task, "in_progress")
        write_state(task, control_branch,
            f"Claude worker runs stage {stage.stage_id}, attempt {attempt + 1}")
        task_text = _write_task_files(task, policy, stage, stage_number, stage_count,
            runtime_task)
        if resume_existing and attempt == 0:
            metrics = collect_diff_metrics(worktree)
            if metrics.changed_files == 0:
                raise OrchestratorError(
                    "--resume-gates requires existing task-worktree changes"
                )
            worker_report = (
                "# Resumed existing repair\n\n"
                "The task worktree already contains a repair made after a failed gate. "
                "The worker step was intentionally skipped; gates and both reviews must "
                "validate the complete current diff."
            )
        else:
            before = worktree_fingerprint(worktree)
            worker = worker_command(worktree, runtime_task, revision_file,
                policy.execution_mode, stage, tools["claude"] or "claude")
            worker_log = runtime / f"stage-{stage_number}-claude-{attempt}.log"
            worker_report = run_command(worker, timeout_seconds, worker_log)
            require_new_worker_changes(before, worktree_fingerprint(worktree))
        commands = _run_gates(worktree, runtime, stage_number, attempt, timeout_seconds)
        policy = _escalate_from_diff(tasks, task, policy, collect_diff_metrics(worktree))
        task_text = _write_task_files(task, policy, stage, stage_number, stage_count,
            runtime_task)
        _, stat, _ = collect_git_evidence(worktree, runtime)
        self_packet = runtime / f"self-review-packet-stage-{stage_number}.md"
        make_review_packet(task_text, worker_report, commands,
            runtime / "git-evidence.md", self_packet)
        self_command = claude_self_review_command(worktree, self_packet,
            policy.execution_mode, policy.review_effort, tools["claude"] or "claude")
        self_review = run_command(self_command, timeout_seconds,
            runtime / f"self-review-stage-{stage_number}-{attempt}.log")
        self_decision = parse_review_decision(self_review)
        combined_report = worker_report + "\n\n# Claude self-review\n\n" + self_review
        write_public_report(task, combined_report, commands, stat)
        if self_decision == "REVISE":
            task["revision_count"] = int(task.get("revision_count", 0)) + 1
            if attempt >= max_revisions:
                raise OrchestratorError("Maximum worker self-review revisions reached")
            set_task_status(tasks, task, "revision")
            revision_file = runtime / f"self-revision-stage-{stage_number}-{attempt + 1}.md"
            revision_file.write_text(self_review, encoding="utf-8")
            write_state(task, control_branch,
                f"Stage {stage.stage_id} self-review revision {attempt + 1} requested")
            continue
        if not stage.review_after:
            return StageOutcome(combined_report, tuple(commands), stat, policy, None)
        set_task_status(tasks, task, "review")
        packet = runtime / f"review-packet-stage-{stage_number}.md"
        make_review_packet(task_text, combined_report, commands,
            runtime / "git-evidence.md", packet)
        final_review = runtime / f"review-final-stage-{stage_number}-{attempt}.md"
        review, provider = run_reviewer(worktree, packet, final_review,
            policy.review_effort, tools, timeout_seconds,
            runtime / f"review-stage-{stage_number}-{attempt}.log")
        _review_path(task["id"], stage, stage_number == stage_count).write_text(
            review, encoding="utf-8")
        decision = parse_review_decision(review)
        task["last_review"] = {"decision": decision, "attempt": attempt + 1,
            "stage": stage.stage_id, "reviewer": provider,
            "review_effort": policy.review_effort}
        if provider == "claude-fallback":
            task["last_review"]["reason"] = (
                "Codex CLI account usage limit; configured Claude fallback used")
        save_tasks(tasks)
        if decision == "PASS":
            return StageOutcome(combined_report, tuple(commands), stat, policy, provider)
        task["revision_count"] = int(task.get("revision_count", 0)) + 1
        if attempt >= max_revisions:
            raise OrchestratorError("Maximum reviewer revisions reached")
        set_task_status(tasks, task, "revision")
        revision_file = runtime / f"revision-stage-{stage_number}-{attempt + 1}.md"
        revision_file.write_text(review, encoding="utf-8")
        write_state(task, control_branch,
            f"Stage {stage.stage_id} revision {attempt + 1} requested")
    raise OrchestratorError("Stage loop ended unexpectedly")


def run_one_task(
    tasks: list[dict[str, Any]], task: dict[str, Any], base_ref: str,
    timeout_seconds: int, max_revisions: int, auto_commit: bool,
    resume_gates: bool = False,
) -> None:
    tools = detect_tools()
    require_tools(tools)
    control_branch = ensure_control_checkout_safe()
    if control_branch in DANGEROUS_BRANCHES:
        raise OrchestratorError("Execute mode requires a non-main control branch")
    if resume_gates and task["status"] != "revision":
        raise OrchestratorError("--resume-gates requires a revision task")
    policy = determine_policy(task)
    persist_policy(task, policy)
    stages = stages_for_task(task, policy)
    worktree = prepare_worktree(task["id"], base_ref)
    runtime = worktree / ".ai" / "runtime" / task["id"]
    runtime.mkdir(parents=True, exist_ok=True)
    runtime_task = runtime / "CURRENT_TASK.md"
    set_task_status(tasks, task, "in_progress")
    write_state(task, control_branch, f"Claude worker owns {worktree}")
    try:
        for index, stage in enumerate(stages, 1):
            task["current_stage"] = f"{index}/{len(stages)} {stage.stage_id}"
            set_task_status(tasks, task, "in_progress")
            write_state(task, control_branch, f"Running stage {stage.stage_id}")
            outcome = _run_stage(tasks, task, policy, stage, index, len(stages), worktree,
                runtime, runtime_task, tools, timeout_seconds, max_revisions, control_branch,
                resume_existing=resume_gates and index == 1)
            policy = outcome.policy
        task["status"] = "passed"
        task["assigned_to"] = "claude"
        task["current_stage"] = f"{len(stages)}/{len(stages)} complete"
        save_tasks(tasks)
        promote_ready_tasks(tasks)
        write_state(task, control_branch, "PASS; inspect and commit the task worktree")
        if auto_commit:
            commit_passed_worktree(task, worktree)
        print(f"PASS {task['id']} [{policy.execution_mode}/{policy.review_effort}] at {worktree}")
    except OrchestratorError as exc:
        set_task_status(tasks, task, "blocked", str(exc))
        write_state(task, control_branch, f"Blocked: {exc}")
        raise
    except Exception as exc:
        error = OrchestratorError(
            f"Unexpected orchestration failure: {exc.__class__.__name__}: {exc}")
        set_task_status(tasks, task, "blocked", str(error))
        write_state(task, control_branch, f"Blocked: {error}")
        raise error from exc


def print_doctor() -> int:
    tools = detect_tools()
    print(f"Repository: {REPO_ROOT}")
    print(f"Platform: {platform.platform()}")
    for name, path in tools.items():
        print(f"{name}: {path or 'NOT FOUND'}")
    return 0 if all(tools.values()) else 2


def parse_arguments(argv: Sequence[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "command",
        nargs="?",
        choices=("run", "doctor", "validate"),
        default="run",
    )
    parser.add_argument("--task", help="ready or revision task ID")
    parser.add_argument("--base-ref", default="HEAD", help="worktree start ref")
    parser.add_argument("--timeout", type=int, default=DEFAULT_TIMEOUT_SECONDS)
    parser.add_argument("--max-revisions", type=int, default=MAX_ALLOWED_REVISIONS)
    parser.add_argument("--execute", action="store_true", help="perform the planned run")
    parser.add_argument("--dry-run", action="store_true", help="force read-only planning")
    parser.add_argument("--auto-commit", action="store_true", help="commit PASS on task branch")
    parser.add_argument("--resume-gates", action="store_true",
        help="for a revision task, keep an existing repair and resume at gates/reviews")
    return parser.parse_args(argv)


def main(argv: Sequence[str] | None = None) -> int:
    args = parse_arguments(argv if argv is not None else sys.argv[1:])
    try:
        if args.timeout < 1:
            raise OrchestratorError("--timeout must be positive")
        if not 1 <= args.max_revisions <= MAX_ALLOWED_REVISIONS:
            raise OrchestratorError(
                f"--max-revisions must be between 1 and {MAX_ALLOWED_REVISIONS}"
            )
        if args.command == "doctor":
            return print_doctor()
        tasks = load_tasks()
        validate_tasks(tasks)
        if args.command == "validate":
            print(f"TASKS.json OK: {len(tasks)} tasks")
            return 0
        task = resolve_task(tasks, args.task)
        if args.dry_run or not args.execute:
            dry_run(task, args.base_ref, detect_tools())
            return 0
        run_one_task(
            tasks,
            task,
            args.base_ref,
            args.timeout,
            args.max_revisions,
            args.auto_commit,
            args.resume_gates,
        )
        return 0
    except KeyboardInterrupt:
        print("Stopped by user; no cleanup was performed", file=sys.stderr)
        return 130
    except OrchestratorError as exc:
        print(f"ERROR: {exc}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    configure_console_encoding()
    raise SystemExit(main())
