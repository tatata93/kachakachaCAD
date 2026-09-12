"""Execution mode and review-effort policy for the existing orchestrator."""

from __future__ import annotations

import json
from dataclasses import dataclass
from pathlib import Path
from typing import Any


EXECUTION_MODES = ("BATCH", "STAGED", "GUARDED")
REVIEW_EFFORTS = ("LOW", "MEDIUM", "HIGH", "EXTRA_HIGH")


@dataclass(frozen=True)
class TaskPolicy:
    execution_mode: str
    review_effort: str
    execution_reason: str
    review_reason: str
    execution_score: int


@dataclass(frozen=True)
class StagePlan:
    stage_id: str
    title: str
    objective: str
    work_steps: tuple[str, ...]
    acceptance_tests: tuple[str, ...]
    review_after: bool


@dataclass(frozen=True)
class DiffMetrics:
    changed_lines: int
    changed_files: int
    paths: tuple[str, ...]
    changed_subsystems: int


def validate_policy_fields(task: dict[str, Any]) -> None:
    mode = task.get("execution_mode")
    effort = task.get("review_effort")
    if mode is not None and mode not in EXECUTION_MODES:
        raise ValueError(f"unsupported execution_mode: {mode}")
    if effort is not None and effort not in REVIEW_EFFORTS:
        raise ValueError(f"unsupported review_effort: {effort}")
    stages = task.get("stages")
    if stages is None:
        return
    if mode == "BATCH":
        raise ValueError("stages may only be used with STAGED or GUARDED execution_mode")
    if not isinstance(stages, list) or not 2 <= len(stages) <= 4:
        raise ValueError("stages must contain two to four stage objects")
    for index, stage in enumerate(stages, 1):
        if not isinstance(stage, dict):
            raise ValueError(f"stage #{index} must be an object")
        if not isinstance(stage.get("title"), str) or not stage["title"].strip():
            raise ValueError(f"stage #{index} needs a title")
        steps = stage.get("work_steps", [])
        if not isinstance(steps, list) or not all(isinstance(item, str) for item in steps):
            raise ValueError(f"stage #{index}.work_steps must be a string array")
        if "acceptance_tests" in stage:
            acceptance = stage["acceptance_tests"]
            if not isinstance(acceptance, list) or not acceptance or not all(
                    isinstance(item, str) and item.strip() for item in acceptance):
                raise ValueError(
                    f"stage #{index}.acceptance_tests must be a non-empty string array")
        if "review_after" in stage and not isinstance(stage["review_after"], bool):
            raise ValueError(f"stage #{index}.review_after must be boolean")


def determine_policy(task: dict[str, Any]) -> TaskPolicy:
    score, inferred_mode, execution_reasons = _execution_assessment(task)
    mode = task.get("execution_mode", "STAGED" if task.get("stages") else inferred_mode)
    if task.get("execution_mode"):
        execution_reasons.insert(0, "TASKS.jsonで明示されたモードを使用")
    effort, review_reasons = _initial_review_effort(task, mode)
    if task.get("review_effort"):
        effort = task["review_effort"]
        review_reasons.insert(0, "TASKS.jsonで明示された推論量を使用")
    return TaskPolicy(
        execution_mode=mode,
        review_effort=effort,
        execution_reason=task.get("execution_reason") or "; ".join(execution_reasons),
        review_reason=task.get("review_reason") or "; ".join(review_reasons),
        execution_score=score,
    )


def stages_for_task(task: dict[str, Any], policy: TaskPolicy) -> tuple[StagePlan, ...]:
    if policy.execution_mode == "BATCH":
        return (StagePlan("full", "一括実装", str(task.get("objective", task["title"])),
            tuple(task.get("work_steps", ())), tuple(task["acceptance_tests"]), True),)
    explicit = task.get("stages")
    if explicit:
        stages = []
        for index, item in enumerate(explicit, 1):
            stages.append(StagePlan(
                str(item.get("id", f"stage-{index}")), item["title"],
                str(item.get("objective", item["title"])),
                tuple(item.get("work_steps", ())),
                tuple(item.get("acceptance_tests", (f"{item['title']}が完了してビルドできる",))),
                bool(item.get("review_after", index == 1 or index == len(explicit))),
            ))
        last = stages[-1]
        stages[-1] = StagePlan(last.stage_id, last.title, last.objective,
            last.work_steps, tuple(task["acceptance_tests"]), True)
        return tuple(stages)
    steps = tuple(task.get("work_steps", ()))
    if len(steps) < 2:
        steps = ("関連する基盤と状態遷移を実装する。", "UI連携と回帰試験を完成させる。")
    split = max(1, len(steps) // 2)
    return (
        StagePlan("foundation", "基盤", "状態と共通境界を先に完成させる。",
            steps[:split], ("基盤Stageの作業が完了し、既存ビルドと試験が成功する。",), True),
        StagePlan("integration", "連携と回帰", "UI連携と回帰確認まで完成させる。",
            steps[split:] or ("UI連携と回帰試験を完成させる。",),
            tuple(task["acceptance_tests"]), True),
    )


def diff_metrics(numstat: str, paths: list[str]) -> DiffMetrics:
    changed_lines = 0
    for line in numstat.splitlines():
        columns = line.split("\t")
        if len(columns) < 3:
            continue
        for value in columns[:2]:
            if value.isdigit():
                changed_lines += int(value)
    normalized = tuple(path.replace("\\", "/") for path in paths)
    subsystems = {_subsystem_for(path) for path in normalized}
    subsystems.discard("tests")
    subsystems.discard("docs")
    return DiffMetrics(changed_lines, len(normalized), normalized, len(subsystems))


def effort_after_diff(current: str, metrics: DiffMetrics) -> tuple[str, str]:
    score = _review_diff_score(metrics)
    source_change = any(path.startswith(("src/", "scripts/")) for path in metrics.paths)
    if score <= 1:
        inferred = "MEDIUM" if source_change else "LOW"
    elif score <= 4:
        inferred = "MEDIUM"
    elif score <= 8:
        inferred = "HIGH"
    else:
        inferred = "EXTRA_HIGH"
    chosen = _max_effort(current, inferred)
    reason = (f"実差分 {metrics.changed_files}ファイル/{metrics.changed_lines}行、"
        f"{metrics.changed_subsystems}サブシステム、差分スコア{score}; {chosen}を使用")
    return chosen, reason


def promoted_mode(current: str, metrics: DiffMetrics) -> tuple[str, str | None]:
    hard = any(_is_guarded_path(path) for path in metrics.paths)
    if hard and current != "GUARDED":
        return "GUARDED", "実差分がUndo・保存・Document・所有権等の中核へ波及"
    broad = (metrics.changed_files >= 10 or metrics.changed_subsystems >= 3
        or metrics.changed_lines >= 2000)
    if current == "BATCH" and broad:
        return "STAGED", "実差分が10ファイル以上、3サブシステム以上、または2000行以上"
    return current, None


def load_review_config(path: Path) -> dict[str, Any]:
    data = json.loads(path.read_text(encoding="utf-8"))
    if not isinstance(data, dict):
        raise ValueError("review profile config must be a JSON object")
    profiles = data.get("review_profiles")
    supported = data.get("supported_codex_reasoning_efforts")
    if not isinstance(profiles, dict) or not isinstance(supported, list) or not supported:
        raise ValueError("review profile config is incomplete")
    known_efforts = {"none", "minimal", "low", "medium", "high", "xhigh", "max", "ultra"}
    if not all(isinstance(item, str) and item in known_efforts for item in supported):
        raise ValueError("supported Codex reasoning efforts contain an unknown value")
    for effort in REVIEW_EFFORTS:
        profile = profiles.get(effort)
        if not isinstance(profile, dict) or not isinstance(profile.get("reasoning_effort"), str):
            raise ValueError(f"review profile {effort} is incomplete")
        if not isinstance(profile.get("prompt_depth"), str):
            raise ValueError(f"review profile {effort} needs prompt_depth")
    return data


def resolve_review_profile(effort: str, config: dict[str, Any]) -> tuple[str, str]:
    requested = config["review_profiles"][effort]["reasoning_effort"]
    supported = config["supported_codex_reasoning_efforts"]
    order = ["none", "minimal", "low", "medium", "high", "xhigh", "max", "ultra"]
    available = [item for item in supported if item in order]
    if requested in available:
        actual = requested
    else:
        target = order.index(requested) if requested in order else order.index("medium")
        actual = min(available,
            key=lambda item: (abs(order.index(item) - target), -order.index(item)))
    return actual, config["review_profiles"][effort]["prompt_depth"]


def _execution_assessment(task: dict[str, Any]) -> tuple[int, str, list[str]]:
    text = _task_text(task)
    files = task.get("files_hint", [])
    score = 0
    reasons: list[str] = []
    score += 0 if len(files) <= 5 else 1 if len(files) <= 10 else 2
    subsystems = {_subsystem_for(path) for path in files}
    subsystems.discard("tests")
    subsystems.discard("docs")
    score += 0 if len(subsystems) <= 1 else 1 if len(subsystems) == 2 else 2
    linked = sum(token in text for token in ("selection", "選択", "snap", "スナップ",
        "hover", "toolsession", "property", "プロパティ"))
    if linked >= 3:
        score += 2
        reasons.append("複数の操作状態が連携")
    if any(token in text for token in ("状態管理", "state management", "data structure",
            "selection model", "schema")):
        score += 2
        reasons.append("局所的な状態またはデータ構造変更")
    guarded = any(token in text for token in _GUARDED_TERMS)
    if guarded:
        score += 4
        reasons.append("中核基盤の変更を含むため強制GUARDED")
    if len(task.get("acceptance_tests", [])) >= 7:
        score += 1
        reasons.append("回帰確認範囲が中程度")
    if guarded:
        mode = "GUARDED"
    else:
        mode = "BATCH" if score <= 2 else "STAGED" if score <= 6 else "GUARDED"
    reasons.insert(0, f"事前リスクスコア{score}")
    return score, mode, reasons


def _initial_review_effort(task: dict[str, Any], mode: str) -> tuple[str, list[str]]:
    text = _task_text(task)
    effort = "MEDIUM" if mode == "BATCH" else "HIGH"
    reasons = [f"{mode}の標準レビュー深度"]
    if mode == "GUARDED" and any(token in text for token in _EXTRA_HIGH_TERMS):
        effort = "EXTRA_HIGH"
        reasons.append("破損・所有権・履歴に関わる中核変更")
    elif mode == "BATCH" and _is_tiny_nonlogic_task(task, text):
        effort = "LOW"
        reasons.append("コードロジックを変えない極小変更")
    return effort, reasons


def _review_diff_score(metrics: DiffMetrics) -> int:
    score = 0 if metrics.changed_lines <= 200 else 1 if metrics.changed_lines <= 800 \
        else 2 if metrics.changed_lines <= 2000 else 3
    score += 0 if metrics.changed_files <= 3 else 1 if metrics.changed_files <= 8 else 2
    joined = " ".join(metrics.paths).lower()
    if any(token in joined for token in ("selection", "snap", "toolsession", "state")):
        score += 2
    if any(token in joined for token in ("geometry", "modeling", "kernel", "occt")):
        score += 1
    if any(_is_guarded_path(path) for path in metrics.paths):
        score += 4
    if metrics.changed_subsystems >= 3:
        score += 3
    elif metrics.changed_subsystems == 2:
        score += 1
    if metrics.changed_files >= 9:
        score += 1  # 広い差分は少なくとも中程度の回帰範囲を持つ。
    return score


def _subsystem_for(path: str) -> str:
    value = path.replace("\\", "/").lower()
    if value.startswith("tests") or "/test" in value:
        return "tests"
    if value.startswith("docs/") or value.startswith(".ai/"):
        return "docs"
    if value.startswith("scripts/"):
        return "orchestration"
    if "src/apps/" in value:
        return "ui"
    for name in ("document", "domain", "geometry", "modeling", "app", "io"):
        if f"/kachakacha/{name}/" in value:
            return name
    return value.split("/", 1)[0]


def _is_guarded_path(path: str) -> bool:
    value = path.lower().replace("\\", "/")
    document_core = value.endswith(("/document.cpp", "/document.h"))
    return document_core or any(token in value for token in ("undo", "project-script",
        "fileformat", "serialization", "ownership", "thread", "asyncgeometry"))


def _task_text(task: dict[str, Any]) -> str:
    return json.dumps(task, ensure_ascii=False).lower()


def _max_effort(first: str, second: str) -> str:
    return REVIEW_EFFORTS[max(REVIEW_EFFORTS.index(first), REVIEW_EFFORTS.index(second))]


def _is_tiny_nonlogic_task(task: dict[str, Any], text: str) -> bool:
    tiny_terms = ("文言", "コメント", "icon", "アイコン", "typo", "誤字", "定数",
        "ui配置", "機械的変更")
    risk_terms = _GUARDED_TERMS + ("ロジック", "状態", "geometry", "occt", "api", "snap",
        "selection", "保存", "undo", "thread")
    return len(task.get("files_hint", [])) <= 2 \
        and len(task.get("acceptance_tests", [])) <= 2 \
        and any(term in text for term in tiny_terms) \
        and not any(term in text for term in risk_terms)


_GUARDED_TERMS = (
    "undo / redo基盤", "undo/redo基盤", "保存形式", "file format", "document architecture",
    "project/document", "feature履歴", "shape ownership", "ownership", "lifetime",
    "threading", "非同期geometry", "toolsession基盤", "selection基盤全体", "選択基盤全面",
)

_EXTRA_HIGH_TERMS = _GUARDED_TERMS + (
    "データ破損", "data corruption", "parametric recompute", "原因不明", "再現困難",
)
