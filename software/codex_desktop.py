"""Conservative desktop picker adapter; disk defaults are not GUI confirmation."""

from __future__ import annotations

import json
import os
import subprocess
from pathlib import Path
from typing import Any


def saved_host_hint(state_path: Path | None = None) -> str:
    """A persisted preference is a hint, never evidence of a task's runtime."""
    path = state_path or Path(os.environ.get("CODEX_HOME", str(Path.home() / ".codex"))) / ".codex-global-state.json"
    try:
        host = json.loads(path.read_text(encoding="utf-8")).get("selected-remote-host-id")
    except (OSError, ValueError, AttributeError):
        return "unknown"
    if not host or host == "local":
        return "local-or-unset"
    return "remote-control" if str(host).startswith("remote-control:") else "remote"


def inspect_picker(model: str | None, effort: str | None, *, sync: bool = False) -> dict[str, Any]:
    result: dict[str, Any] = {
        "status": "unsupported",
        "reason": "non_windows",
        "confirmed_effort": None,
        "confirmed_model": None,
        "saved_host_hint": saved_host_hint(),
        "runtime_verified": False,
    }
    if os.name != "nt":
        return result
    script = Path(__file__).with_name("codex_desktop_probe.ps1")
    if not script.is_file():
        result["reason"] = "probe_script_missing"
        return result
    command = [
        "powershell.exe", "-NoProfile", "-NonInteractive", "-ExecutionPolicy", "Bypass",
        "-File", str(script), "-Mode", "Sync" if sync else "Probe",
    ]
    if model:
        command.extend(["-Model", model])
    if effort:
        command.extend(["-Effort", effort])
    try:
        completed = subprocess.run(
            command, capture_output=True, text=True, encoding="utf-8", errors="replace",
            timeout=8.0, creationflags=subprocess.CREATE_NO_WINDOW,
        )
        if completed.returncode != 0:
            result["reason"] = "probe_process_failed"
            return result
        response = json.loads(completed.stdout.lstrip("\ufeff").strip())
        if not isinstance(response, dict):
            raise ValueError("invalid probe result")
        for key in ("status", "reason", "confirmed_effort", "confirmed_model"):
            result[key] = response.get(key)
        if result["status"] == "confirmed" and (
            result["confirmed_effort"] != effort or result["confirmed_model"] != model
        ):
            result.update(status="unverified", reason="invalid_picker_confirmation", confirmed_effort=None)
    except (OSError, ValueError, subprocess.TimeoutExpired):
        result.update(status="unsupported", reason="probe_unavailable", confirmed_effort=None)
    return result
