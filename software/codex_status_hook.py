#!/usr/bin/env python3
"""Forward Codex lifecycle hook events to the keyboard bridge over localhost UDP."""

from __future__ import annotations

import argparse
import json
import socket
import sys
import time
from typing import Any


DEFAULT_HOST = "127.0.0.1"
DEFAULT_PORT = 18765
SEND_ATTEMPTS = 3
SEND_RETRY_SECONDS = 0.015
FORWARDED_EVENTS = {
    "SessionStart",
    "SessionEnd",
    "UserPromptSubmit",
    "PermissionRequest",
    "PostToolUse",
    "Stop",
    "Interrupt",
}


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--host", default=DEFAULT_HOST)
    parser.add_argument("--port", type=int, default=DEFAULT_PORT)
    return parser.parse_args()


def load_hook_payload() -> dict[str, Any]:
    # Codex writes hook payloads as UTF-8 JSON. On Windows, Python may otherwise
    # decode redirected stdin with the active ANSI code page (for example GBK),
    # which breaks prompts containing Chinese text before JSON parsing begins.
    raw = sys.stdin.buffer.read().decode("utf-8-sig")
    if not raw.strip():
        return {}
    value = json.loads(raw)
    return value if isinstance(value, dict) else {}


def send_event(payload: dict[str, Any], host: str, port: int) -> None:
    event = payload.get("hook_event_name")
    if event not in FORWARDED_EVENTS:
        return
    message = {
        "version": 1,
        "hook_event_name": event,
        "session_id": payload.get("session_id"),
        "turn_id": payload.get("turn_id"),
        "cwd": payload.get("cwd"),
        "sent_at": time.time(),
    }
    encoded = json.dumps(message, ensure_ascii=False, separators=(",", ":")).encode("utf-8")
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as sender:
        for attempt in range(SEND_ATTEMPTS):
            sender.sendto(encoded, (host, port))
            if attempt + 1 < SEND_ATTEMPTS:
                time.sleep(SEND_RETRY_SECONDS)


def main() -> int:
    args = parse_args()
    payload: dict[str, Any] = {}
    try:
        payload = load_hook_payload()
        send_event(payload, args.host, args.port)
    except (OSError, ValueError, json.JSONDecodeError) as exc:
        # A status-indicator failure must never block the Codex turn.
        print(f"Codex keyboard status hook warning: {exc}", file=sys.stderr)

    # Stop requires valid JSON on stdout. This acknowledges the hook without
    # requesting that Codex continue or change the completed turn.
    if payload.get("hook_event_name") == "Stop":
        print(json.dumps({"continue": True}, separators=(",", ":")))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
