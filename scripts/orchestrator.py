#!/usr/bin/env python3
"""Small, worktree-first Claude/Codex task orchestrator for kachakachaCAD.

Dry-run is the default. The script intentionally uses fixed argument arrays,
never a shell command string, and never performs destructive Git operations.
"""

from __future__ import annotations

import argparse
import json
import os
import platform
import shutil
import subprocess
import sys
from dataclasses import dataclass
from pathlib import Path
from typing import Any, Iterable, Sequence


REPO_ROOT = Path(__file__).resolve().parents[1]
AI_DIR = REPO_ROOT / ".ai"
TASKS_PATH = AI_DIR / "TASKS.json"
CURRENT_TASK_PATH = AI_DIR / "CURRENT_TASK.md"
STATE_PATH = AI_DIR / "STATE.md"
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
        if len(task["files_hint"]) > 5:
            raise OrchestratorError(f"{task_id} exceeds the five-file task limit")
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


def render_current_task(task: dict[str, Any]) -> str:
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
    sections = [
        ("TASK ID", task["id"]),
        ("目的", objective),
        ("背景", background),
        ("変更対象候補", _markdown_list(task["files_hint"], code=True)),
        ("変更禁止範囲", _markdown_list(forbidden)),
        ("必須仕様", requirement_text),
        ("作業手順", _numbered_list(work_steps)),
        ("Acceptance Tests", _markdown_list(task["acceptance_tests"])),
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
    output = completed.stdout or ""
    if log_path is not None:
        log_path.parent.mkdir(parents=True, exist_ok=True)
        log_path.write_text(output, encoding="utf-8")
    if completed.returncode != 0:
        raise OrchestratorError(
            f"{command.label} failed with exit code {completed.returncode}"
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
    executable: str = "claude",
) -> PlannedCommand:
    prompt = (
        "Read .ai/prompts/CLAUDE_WORKER.md and AGENTS.md. "
        f"The active assignment is {task_file}. Implement only that task in this worktree. "
        "Do not commit or push. Finish with the required worker report."
    )
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
) -> PlannedCommand:
    prompt = (
        "Read .ai/prompts/CODEX_REVIEWER.md and the review packet at "
        f"{packet_path}. Review only; do not edit. The first non-empty line must be PASS or REVISE."
    )
    return PlannedCommand(
        "Codex reviewer",
        (
            executable,
            "exec",
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


def format_command(command: PlannedCommand) -> str:
    def quote(value: str) -> str:
        return f'"{value}"' if any(character.isspace() for character in value) else value

    return " ".join(quote(value) for value in command.argv)


def dry_run(task: dict[str, Any], base_ref: str, tools: dict[str, str | None]) -> None:
    branch = ensure_control_checkout_safe()
    worktree = worktree_path(task["id"])
    runtime = worktree / ".ai" / "runtime" / task["id"]
    task_file = runtime / "CURRENT_TASK.md"
    print("DRY RUN: no files, branches, worktrees, or agents will be changed")
    print(f"Task: {task['id']} - {task['title']}")
    print(f"Control branch: {branch}")
    print(f"Task branch: {task_branch(task['id'])}")
    print(f"Worktree: {worktree}")
    print(f"Current task bytes: {len(render_current_task(task).encode('utf-8'))}")
    print(f"PLAN git worktree add -b {task_branch(task['id'])} {worktree} {base_ref}")
    worker = worker_command(worktree, task_file, None, tools.get("claude") or "claude")
    print(f"PLAN [{worker.label}] {format_command(worker)}")
    for command in build_and_test_plan(worktree):
        print(f"PLAN [{command.label}] {format_command(command)}")
    review = reviewer_command(
        worktree,
        runtime / "review-packet.md",
        runtime / "review-final.md",
        tools.get("codex") or "codex",
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
    untracked = git_output(
        ["-C", str(worktree), "ls-files", "--others", "--exclude-standard"]
    )
    evidence = f"# Status\n\n```text\n{status}\n```\n\n# Diff stat\n\n```text\n{stat}\n```\n\n"
    evidence += f"# Diff\n\n```diff\n{diff}\n```\n\n# Untracked files\n\n```text\n{untracked}\n```\n"
    (runtime / "git-evidence.md").write_text(evidence, encoding="utf-8")
    return status, stat, diff


def require_worker_changes(status: str) -> None:
    if not status.strip():
        raise OrchestratorError(
            "Claude worker completed without changing the task worktree"
        )


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
    first = next((line.strip() for line in review.splitlines() if line.strip()), "")
    if first not in {"PASS", "REVISE"}:
        raise OrchestratorError("Reviewer output must begin with PASS or REVISE")
    return first


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


def run_one_task(
    tasks: list[dict[str, Any]],
    task: dict[str, Any],
    base_ref: str,
    timeout_seconds: int,
    max_revisions: int,
    auto_commit: bool,
) -> None:
    tools = detect_tools()
    require_tools(tools)
    control_branch = ensure_control_checkout_safe()
    if control_branch in DANGEROUS_BRANCHES:
        raise OrchestratorError("Execute mode requires a non-main control branch")
    worktree = prepare_worktree(task["id"], base_ref)
    runtime = worktree / ".ai" / "runtime" / task["id"]
    runtime.mkdir(parents=True, exist_ok=True)
    task_text = render_current_task(task)
    runtime_task = runtime / "CURRENT_TASK.md"
    runtime_task.write_text(task_text, encoding="utf-8")
    CURRENT_TASK_PATH.write_text(task_text, encoding="utf-8")
    set_task_status(tasks, task, "in_progress")
    write_state(task, control_branch, f"Claude worker owns {worktree}")
    revision_file: Path | None = None
    try:
        for attempt in range(max_revisions + 1):
            worker = worker_command(
                worktree, runtime_task, revision_file, tools["claude"] or "claude"
            )
            worker_report = run_command(
                worker, timeout_seconds, runtime / f"claude-{attempt}.log"
            )
            worker_status = git_output(["-C", str(worktree), "status", "--short"])
            require_worker_changes(worker_status)
            command_results: list[tuple[PlannedCommand, str]] = []
            for index, command in enumerate(build_and_test_plan(worktree)):
                output = run_command(
                    command, timeout_seconds, runtime / f"gate-{index}-{command.label}.log"
                )
                command_results.append((command, output))
            set_task_status(tasks, task, "review")
            _, stat, _ = collect_git_evidence(worktree, runtime)
            write_public_report(task, worker_report, command_results, stat)
            packet = runtime / "review-packet.md"
            make_review_packet(
                task_text,
                worker_report,
                command_results,
                runtime / "git-evidence.md",
                packet,
            )
            final_review = runtime / f"review-final-{attempt}.md"
            reviewer = reviewer_command(
                worktree, packet, final_review, tools["codex"] or "codex"
            )
            run_command(reviewer, timeout_seconds, runtime / f"review-{attempt}.log")
            if not final_review.exists():
                raise OrchestratorError("Codex reviewer did not write a final decision")
            review = final_review.read_text(encoding="utf-8")
            review_path = AI_DIR / "reviews" / f"{task['id']}.md"
            review_path.write_text(review, encoding="utf-8")
            decision = parse_review_decision(review)
            task["last_review"] = {"decision": decision, "attempt": attempt + 1}
            if decision == "PASS":
                task["status"] = "passed"
                task["assigned_to"] = "claude"
                save_tasks(tasks)
                promote_ready_tasks(tasks)
                write_state(task, control_branch, "PASS; inspect and commit the task worktree")
                if auto_commit:
                    commit_passed_worktree(task, worktree)
                print(f"PASS {task['id']} at {worktree}")
                return
            task["revision_count"] = int(task.get("revision_count", 0)) + 1
            if attempt >= max_revisions:
                raise OrchestratorError("Maximum reviewer revisions reached")
            set_task_status(tasks, task, "revision")
            revision_file = runtime / f"revision-{attempt + 1}.md"
            revision_file.write_text(review, encoding="utf-8")
            write_state(task, control_branch, f"Revision {attempt + 1} requested")
    except OrchestratorError as exc:
        set_task_status(tasks, task, "blocked", str(exc))
        write_state(task, control_branch, f"Blocked: {exc}")
        raise


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
