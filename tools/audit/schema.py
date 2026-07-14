"""Shared schema for Cubatarium code audit findings."""

from __future__ import annotations

import json
from dataclasses import asdict, dataclass, field
from datetime import datetime, timezone
from pathlib import Path
from typing import Any


REPO_ROOT = Path(__file__).resolve().parent.parent.parent
AUDIT_DIR = REPO_ROOT / "audit"


@dataclass
class Finding:
    id: str
    category: str
    priority: str
    module: str
    title: str
    files: list[str] = field(default_factory=list)
    lines: list[int] = field(default_factory=list)
    evidence: str = ""
    action: str = ""
    risk: str = "low"
    blocked_by: list[str] = field(default_factory=list)
    tech_debt_ref: str | None = None
    auto_fixable: bool = False
    verification: list[str] = field(default_factory=list)
    implemented_in: str | None = None
    status: str = "open"

    def to_dict(self) -> dict[str, Any]:
        return asdict(self)


@dataclass
class FindingsDocument:
    status: str = "pending_review"
    commit: str = ""
    generated_at: str = ""
    summary: dict[str, int] = field(default_factory=dict)
    approved_ids: list[str] = field(default_factory=list)
    findings: list[Finding] = field(default_factory=list)

    def to_dict(self) -> dict[str, Any]:
        return {
            "status": self.status,
            "commit": self.commit,
            "generated_at": self.generated_at,
            "summary": self.summary,
            "approved_ids": self.approved_ids,
            "findings": [f.to_dict() for f in self.findings],
        }


def utc_now_iso() -> str:
    return datetime.now(timezone.utc).replace(microsecond=0).isoformat()


def ensure_audit_dir() -> Path:
    AUDIT_DIR.mkdir(parents=True, exist_ok=True)
    (AUDIT_DIR / "modules").mkdir(parents=True, exist_ok=True)
    return AUDIT_DIR


def write_json(path: Path, data: Any) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(
        json.dumps(data, indent=2, ensure_ascii=False) + "\n",
        encoding="utf-8",
    )


def read_json(path: Path) -> Any:
    return json.loads(path.read_text(encoding="utf-8"))


def summarize_priorities(findings: list[Finding | dict[str, Any]]) -> dict[str, int]:
    summary = {"P0": 0, "P1": 0, "P2": 0, "P3": 0}
    for item in findings:
        priority = item.priority if isinstance(item, Finding) else item.get("priority", "P3")
        summary[priority] = summary.get(priority, 0) + 1
    return summary
