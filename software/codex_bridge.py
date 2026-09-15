#!/usr/bin/env python3
"""Bridge FunModularKeyboard CDC commands to the local Codex app server."""

from __future__ import annotations

import argparse
import json
import os
import queue
import shutil
import socket
import subprocess
import sys
import threading
import time
from pathlib import Path
from typing import Any

try:
    import serial
    from serial.tools import list_ports
except ImportError:  # Keep --dry-run useful before dependencies are installed.
    serial = None
    list_ports = None


DEVICE_PREFIX = "CX>"
PING_INTERVAL_SECONDS = 2.0
STATUS_ACK_RETRY_SECONDS = 0.35
DONE_HOLD_SECONDS = 4.0
PAUSED_HOLD_SECONDS = 8.0
DEFAULT_HOOK_STALE_TIMEOUT_SECONDS = 600.0
DEFAULT_HOOK_HOST = "127.0.0.1"
DEFAULT_HOOK_PORT = 18765
MAX_HOOK_PACKET_BYTES = 8192
ESPRESSIF_USB_VID = 0x303A
CH340_USB_VID = 0x1A86
CH340_USB_PID = 0x7523
PORT_PROBE_TIMEOUT_SECONDS = 1.0
APPROVAL_METHODS = {
    "item/commandExecution/requestApproval",
    "item/fileChange/requestApproval",
    "item/permissions/requestApproval",
}
PERMISSIONS_APPROVAL_METHOD = "item/permissions/requestApproval"
KNOWN_REASONING_EFFORTS = ("minimal", "low", "medium", "high", "xhigh", "max", "ultra")
DEFAULT_REASONING_EFFORTS = ("low", "medium", "high", "xhigh")


class AppServerError(RuntimeError):
    pass


class AppServerClient:
    def __init__(self, executable: str, cwd: Path) -> None:
        creationflags = subprocess.CREATE_NO_WINDOW if os.name == "nt" else 0
        self.process = subprocess.Popen(
            [executable, "app-server", "--stdio"],
            cwd=str(cwd),
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
            encoding="utf-8",
            errors="replace",
            bufsize=1,
            creationflags=creationflags,
        )
        self._write_lock = threading.Lock()
        self._pending: dict[int, queue.Queue[dict[str, Any]]] = {}
        self._pending_lock = threading.Lock()
        self._next_id = 1
        self.events: queue.Queue[dict[str, Any]] = queue.Queue()
        threading.Thread(target=self._read_stdout, daemon=True).start()
        threading.Thread(target=self._read_stderr, daemon=True).start()

    def _send(self, message: dict[str, Any]) -> None:
        if self.process.stdin is None or self.process.poll() is not None:
            raise AppServerError("Codex app server is not running")
        line = json.dumps(message, ensure_ascii=False, separators=(",", ":"))
        with self._write_lock:
            self.process.stdin.write(line + "\n")
            self.process.stdin.flush()

    def request(self, method: str, params: dict[str, Any], timeout: float = 30.0) -> dict[str, Any]:
        with self._pending_lock:
            request_id = self._next_id
            self._next_id += 1
            response_queue: queue.Queue[dict[str, Any]] = queue.Queue(maxsize=1)
            self._pending[request_id] = response_queue
        try:
            self._send({"id": request_id, "method": method, "params": params})
            try:
                response = response_queue.get(timeout=timeout)
            except queue.Empty as exc:
                raise AppServerError(f"Timed out waiting for {method}") from exc
        finally:
            with self._pending_lock:
                self._pending.pop(request_id, None)

        if "error" in response:
            raise AppServerError(f"{method} failed: {response['error']}")
        return response.get("result", {})

    def notify(self, method: str, params: dict[str, Any] | None = None) -> None:
        message: dict[str, Any] = {"method": method}
        if params is not None:
            message["params"] = params
        self._send(message)

    def respond(self, request_id: int | str, result: dict[str, Any]) -> None:
        self._send({"id": request_id, "result": result})

    def _read_stdout(self) -> None:
        assert self.process.stdout is not None
        for line in self.process.stdout:
            try:
                message = json.loads(line)
            except json.JSONDecodeError:
                print(f"[app-server] ignored non-JSON output: {line.rstrip()}", file=sys.stderr)
                continue

            request_id = message.get("id")
            if request_id is not None and "method" not in message:
                with self._pending_lock:
                    response_queue = self._pending.get(request_id)
                if response_queue is not None:
                    response_queue.put(message)
                continue
            self.events.put(message)

        self.events.put({"method": "bridge/appServerExited", "params": {"code": self.process.poll()}})

    def _read_stderr(self) -> None:
        assert self.process.stderr is not None
        for line in self.process.stderr:
            print(f"[app-server] {line.rstrip()}", file=sys.stderr)

    def close(self) -> None:
        if self.process.poll() is None:
            self.process.terminate()
            try:
                self.process.wait(timeout=3)
            except subprocess.TimeoutExpired:
                self.process.kill()


class CodexKeyboardBridge:
    def __init__(self, args: argparse.Namespace, tasks: dict[str, str]) -> None:
        self.args = args
        self.tasks = tasks
        self.app: AppServerClient | None = None
        self.app_project: Path | None = None
        self.last_active_project: Path | None = getattr(args, "project", None)
        self.thread_id: str | None = None
        self.active_turn_id: str | None = None
        self.thread_has_task = False
        self.bridge_paused = False
        self.last_prompt: str | None = None
        self.supported_efforts: list[str] = []
        self.current_effort: str | None = None
        self.pending_approval: tuple[int | str, str, dict[str, Any]] | None = None
        self.pending_user_input_request_id: int | str | None = None
        self.serial_port: Any = None
        self.hook_socket: socket.socket | None = None
        # A Codex session can have only one foreground turn at a time. Keeping
        # one turn per session lets a later prompt replace an orphaned turn if
        # its Stop/Interrupt event was lost.
        self.hook_active_turns: dict[str, str] = {}
        self.hook_approval_turns: set[str] = set()
        self.hook_last_seen: dict[str, float] = {}
        # Keep recent terminal turns as tombstones. Hook commands from different
        # config layers run as separate processes, so UDP packets for an older
        # turn can arrive after its Stop or even after the next turn starts.
        self.hook_terminal_turns: dict[tuple[str, str], float] = {}
        self.paused_tasks: dict[str, float] = {}
        self.last_ping = 0.0
        self.ready_after = 0.0
        self.terminal_status: str | None = None
        self.terminal_status_count = 1
        self.current_status = "DISCONNECTED"
        self.current_status_count = 1
        self.status_acknowledged = False
        self.last_status_sent_at = 0.0

    def start_app_server(self, project: Path | None = None) -> None:
        project = (project or self.args.project).resolve()
        if not project.is_dir():
            raise AppServerError(f"Codex project directory does not exist: {project}")
        executable = shutil.which(self.args.codex_bin)
        if executable is None:
            raise AppServerError(f"Cannot find '{self.args.codex_bin}' in PATH")
        self.app = AppServerClient(executable, project)
        self.app_project = project
        self.app.request(
            "initialize",
            {"clientInfo": {"name": "fun_modular_keyboard", "title": "Fun Modular Keyboard", "version": "1.0.0"}},
        )
        self.app.notify("initialized")
        self.load_model_efforts()
        self.start_thread(project)

    def load_model_efforts(self) -> None:
        if self.app is None:
            return
        try:
            result = self.app.request("model/list", {"includeHidden": False})
        except AppServerError as exc:
            print(f"Unable to query model reasoning efforts: {exc}; using safe defaults", file=sys.stderr)
            self.supported_efforts = list(DEFAULT_REASONING_EFFORTS)
            self.current_effort = "medium"
            return

        models = result.get("data") or []
        selected = next((model for model in models if model.get("isDefault")), None)
        if selected is None:
            selected = next((model for model in models if not model.get("hidden", False)), None)

        efforts: list[str] = []
        if isinstance(selected, dict):
            for option in selected.get("supportedReasoningEfforts") or []:
                value = option.get("reasoningEffort") if isinstance(option, dict) else option
                if isinstance(value, str):
                    normalized = value.strip().lower()
                    if normalized in KNOWN_REASONING_EFFORTS and normalized not in efforts:
                        efforts.append(normalized)
            default_effort = selected.get("defaultReasoningEffort")
            if isinstance(default_effort, str) and default_effort.lower() in efforts:
                self.current_effort = default_effort.lower()

        self.supported_efforts = efforts or list(DEFAULT_REASONING_EFFORTS)
        if self.current_effort not in self.supported_efforts:
            self.current_effort = "medium" if "medium" in self.supported_efforts else self.supported_efforts[0]
        model_name = selected.get("model", "default") if isinstance(selected, dict) else "default"
        print(
            f"Codex model={model_name} effort={self.current_effort} "
            f"supported={','.join(self.supported_efforts)}"
        )

    def start_thread(self, project: Path) -> None:
        if self.app is None:
            raise AppServerError("Codex app server is not running")
        result = self.app.request(
            "thread/start",
            {
                "cwd": str(project),
                "approvalPolicy": "on-request",
                "sandbox": "workspace-write",
                "serviceName": "fun_modular_keyboard",
            },
        )
        self.thread_id = result["thread"]["id"]
        self.thread_has_task = False
        self.bridge_paused = False
        self.last_prompt = None
        self.active_turn_id = None
        self.pending_approval = None
        self.pending_user_input_request_id = None
        print(f"Codex thread ready: {self.thread_id} project={project}")
        if self.args.open_app and os.name == "nt":
            try:
                os.startfile(f"codex://threads/{self.thread_id}")  # type: ignore[attr-defined]
            except OSError as exc:
                print(f"Unable to open Codex desktop thread: {exc}", file=sys.stderr)

    def connect_serial(self) -> None:
        assert serial is not None
        port = self.args.port or discover_keyboard_port(self.args.baud)
        print(f"Opening keyboard bridge on {port} @ {self.args.baud}")
        self.serial_port = serial.Serial(port, self.args.baud, timeout=0.10, write_timeout=0.5)
        self.last_ping = 0.0
        self.refresh_visible_status(force=True)

    def start_hook_listener(self) -> None:
        listener = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        try:
            listener.bind((self.args.hook_host, self.args.hook_port))
            listener.setblocking(False)
        except OSError:
            listener.close()
            raise
        self.hook_socket = listener
        print(f"Listening for Codex desktop hooks on udp://{self.args.hook_host}:{self.args.hook_port}")

    def send_line(self, line: str) -> None:
        if self.serial_port is None or not self.serial_port.is_open:
            return
        self.serial_port.write((line + "\n").encode("ascii"))

    def send_status(self, status: str, count: int = 1, *, force: bool = False) -> None:
        count = max(1, min(int(count), 255))
        previous = self.current_status
        previous_count = self.current_status_count
        changed = status != previous or count != previous_count
        if force or changed:
            self.send_line(self.status_line(status, count))
            self.current_status = status
            self.current_status_count = count
            self.status_acknowledged = False
            self.last_status_sent_at = time.monotonic()
        if changed:
            print(
                f"[keyboard-status] {previous} x{previous_count} -> {status} x{count} "
                f"app_turn={self.active_turn_id or '-'} "
                f"hook_sessions={len(self.hook_active_turns)} "
                f"approvals={len(self.hook_approval_turns)}"
            )

    def status_line(self, status: str | None = None, count: int | None = None) -> str:
        status = status or self.current_status
        count = self.current_status_count if count is None else count
        suffix = f"|{self.current_effort.upper()}" if self.current_effort else ""
        return f"CX<STATUS|{status}|{count}{suffix}"

    def has_active_turns(self) -> bool:
        return self.active_turn_id is not None or bool(self.hook_active_turns)

    def refresh_visible_status(self, *, force: bool = False) -> None:
        now = time.monotonic()
        expired_paused = [task for task, until in self.paused_tasks.items() if until <= now]
        for task in expired_paused:
            self.paused_tasks.pop(task, None)

        app_task = self.thread_id or "bridge-app-session"
        active_tasks = set(self.hook_active_turns)
        if self.active_turn_id is not None:
            active_tasks.add(app_task)

        approval_tasks = set(self.hook_approval_turns)
        if self.pending_approval is not None:
            approval_tasks.add(app_task)

        waiting_tasks: set[str] = set()
        if self.pending_user_input_request_id is not None:
            waiting_tasks.add(app_task)

        running_tasks = active_tasks - approval_tasks - waiting_tasks
        paused_tasks = set(self.paused_tasks)
        if self.bridge_paused:
            paused_tasks.add(app_task)

        if approval_tasks:
            status = "APPROVAL"
            count = len(approval_tasks)
        elif waiting_tasks:
            status = "WAITING"
            count = len(waiting_tasks)
        elif running_tasks:
            status = "RUNNING"
            count = len(running_tasks)
        elif paused_tasks:
            status = "PAUSED"
            count = len(paused_tasks)
        elif self.ready_after > now and self.terminal_status is not None:
            status = self.terminal_status
            count = self.terminal_status_count
        else:
            self.ready_after = 0.0
            self.terminal_status = None
            self.terminal_status_count = 1
            status = "READY"
            count = 1
        self.send_status(status, count, force=force)

    def hold_terminal_status(self, status: str, duration: float = DONE_HOLD_SECONDS, count: int = 1) -> None:
        self.terminal_status = status
        self.terminal_status_count = count
        self.ready_after = time.monotonic() + duration
        self.refresh_visible_status()

    @staticmethod
    def hook_session_key(payload: dict[str, Any]) -> str:
        return str(payload.get("session_id") or "unknown-session")

    def remember_hook_project(self, payload: dict[str, Any]) -> None:
        raw_cwd = payload.get("cwd")
        if not isinstance(raw_cwd, str) or not raw_cwd.strip():
            return
        try:
            project = Path(raw_cwd).expanduser().resolve()
        except (OSError, RuntimeError):
            return
        if project.is_dir():
            self.last_active_project = project

    def mark_hook_session_active(self, payload: dict[str, Any], *, approval: bool = False) -> None:
        session_id = self.hook_session_key(payload)
        self.hook_active_turns[session_id] = str(payload.get("turn_id") or "unknown-turn")
        self.hook_last_seen[session_id] = time.monotonic()
        self.paused_tasks.pop(session_id, None)
        if approval:
            self.hook_approval_turns.add(session_id)
        else:
            self.hook_approval_turns.discard(session_id)

    def clear_hook_session(self, session_id: str, *, clear_terminal: bool = False) -> None:
        self.hook_active_turns.pop(session_id, None)
        self.hook_approval_turns.discard(session_id)
        self.hook_last_seen.pop(session_id, None)
        if clear_terminal:
            terminal_keys = [key for key in self.hook_terminal_turns if key[0] == session_id]
            for key in terminal_keys:
                self.hook_terminal_turns.pop(key, None)
            self.paused_tasks.pop(session_id, None)

    def remember_hook_terminal(self, payload: dict[str, Any], now: float | None = None) -> None:
        session_id = self.hook_session_key(payload)
        turn_id = str(payload.get("turn_id") or "unknown-turn")
        self.hook_terminal_turns[(session_id, turn_id)] = time.monotonic() if now is None else now

    def is_late_terminal_event(self, payload: dict[str, Any]) -> bool:
        session_id = self.hook_session_key(payload)
        turn_id = str(payload.get("turn_id") or "unknown-turn")
        return (session_id, turn_id) in self.hook_terminal_turns

    def is_stale_terminal_event(self, payload: dict[str, Any]) -> bool:
        session_id = self.hook_session_key(payload)
        active_turn = self.hook_active_turns.get(session_id)
        incoming_turn = str(payload.get("turn_id") or "unknown-turn")
        return active_turn is not None and active_turn != incoming_turn

    def expire_stale_hook_sessions(self, now: float) -> None:
        timeout = float(getattr(self.args, "hook_stale_timeout", DEFAULT_HOOK_STALE_TIMEOUT_SECONDS))
        expired = [
            session_id
            for session_id, seen_at in self.hook_last_seen.items()
            if now - seen_at >= timeout
        ]
        for session_id in expired:
            turn_id = self.hook_active_turns.get(session_id)
            if turn_id is not None:
                self.hook_terminal_turns[(session_id, turn_id)] = now
            self.clear_hook_session(session_id)
            print(f"[codex-hook] expired stale session={session_id}")
        expired_terminal = [
            key
            for key, seen_at in self.hook_terminal_turns.items()
            if now - seen_at >= timeout
        ]
        for key in expired_terminal:
            self.hook_terminal_turns.pop(key, None)
        expired_paused = [task for task, until in self.paused_tasks.items() if until <= now]
        for task in expired_paused:
            self.paused_tasks.pop(task, None)
        if expired or expired_paused:
            self.refresh_visible_status()

    def handle_hook_event(self, payload: dict[str, Any]) -> None:
        if payload.get("version") != 1:
            return
        event = payload.get("hook_event_name")
        if not isinstance(event, str):
            return

        session_id = self.hook_session_key(payload)
        if event != "SessionEnd":
            self.remember_hook_project(payload)
        # The bridge-owned App Server thread is already tracked through its
        # event stream. Its global hook events would otherwise count the same
        # task twice.
        if self.thread_id is not None and session_id == self.thread_id:
            return
        ignored_late_event = False
        if event == "UserPromptSubmit":
            if not self.is_late_terminal_event(payload):
                self.ready_after = 0.0
                self.terminal_status = None
                self.mark_hook_session_active(payload)
                self.refresh_visible_status()
            else:
                ignored_late_event = True
        elif event == "PermissionRequest":
            if not self.is_late_terminal_event(payload) and not self.is_stale_terminal_event(payload):
                self.mark_hook_session_active(payload, approval=True)
                self.refresh_visible_status()
            else:
                ignored_late_event = True
        elif event == "PostToolUse":
            if not self.is_late_terminal_event(payload) and not self.is_stale_terminal_event(payload):
                self.mark_hook_session_active(payload)
                self.refresh_visible_status()
            else:
                ignored_late_event = True
        elif event == "Stop":
            if self.is_late_terminal_event(payload) or self.is_stale_terminal_event(payload):
                ignored_late_event = True
            else:
                self.clear_hook_session(session_id)
                self.remember_hook_terminal(payload)
                self.hold_terminal_status("DONE")
        elif event == "Interrupt":
            if self.is_late_terminal_event(payload) or self.is_stale_terminal_event(payload):
                ignored_late_event = True
            else:
                self.clear_hook_session(session_id)
                self.remember_hook_terminal(payload)
                self.paused_tasks[session_id] = time.monotonic() + PAUSED_HOLD_SECONDS
                self.ready_after = 0.0
                self.terminal_status = None
                self.refresh_visible_status()
        elif event == "SessionStart":
            self.refresh_visible_status()
        elif event == "SessionEnd":
            active_turn = self.hook_active_turns.get(session_id)
            if active_turn is not None:
                self.hook_terminal_turns[(session_id, active_turn)] = time.monotonic()
            self.clear_hook_session(session_id)
            self.paused_tasks.pop(session_id, None)
            self.refresh_visible_status()
        else:
            return
        suffix = " ignored=stale-or-duplicate" if ignored_late_event else ""
        print(
            f"[codex-hook] {event} session={payload.get('session_id')} "
            f"turn={payload.get('turn_id')}{suffix}"
        )

    def drain_hook_events(self) -> None:
        if self.hook_socket is None:
            return
        while True:
            try:
                packet, source = self.hook_socket.recvfrom(MAX_HOOK_PACKET_BYTES)
            except BlockingIOError:
                break
            except OSError as exc:
                print(f"Codex hook listener error: {exc}", file=sys.stderr)
                break
            if source[0] != self.args.hook_host:
                continue
            try:
                payload = json.loads(packet.decode("utf-8"))
            except (UnicodeDecodeError, json.JSONDecodeError):
                print("Ignored malformed Codex hook packet", file=sys.stderr)
                continue
            if isinstance(payload, dict):
                self.handle_hook_event(payload)

    def handle_device_line(self, line: str) -> None:
        if not line.startswith(DEVICE_PREFIX):
            return
        print(f"[keyboard] {line}")
        if line.startswith("CX>STATUS|"):
            parts = line.split("|")
            if len(parts) in (3, 4):
                try:
                    count = int(parts[2])
                except ValueError:
                    return
                effort_matches = (
                    self.current_effort is None
                    or (len(parts) == 4 and parts[3].lower() == self.current_effort)
                )
                if parts[1] == self.current_status and count == self.current_status_count and effort_matches:
                    self.status_acknowledged = True
        elif line == "CX>TASK|ANALYZE":
            self.start_analysis()
        elif line == "CX>TASK|REVIEW":
            self.start_review()
        elif line == "CX>TASK|NEW":
            self.start_new_task()
        elif line == "CX>ACTION|ACCEPT":
            self.answer_approval("accept")
        elif line == "CX>ACTION|DECLINE":
            self.answer_approval("decline")
        elif line in ("CX>ACTION|PAUSE", "CX>ACTION|INTERRUPT"):
            self.pause_active_turn()
        elif line == "CX>ACTION|RESUME":
            self.resume_paused_task()
        elif line == "CX>ACTION|EFFORT_NEXT":
            self.adjust_effort(1)
        elif line == "CX>ACTION|EFFORT_PREVIOUS":
            self.adjust_effort(-1)
        elif line == "CX>ACTION|EFFORT_CURRENT":
            self.refresh_visible_status(force=True)

    def retry_unacknowledged_status(self, now: float) -> None:
        if self.status_acknowledged or now - self.last_status_sent_at < STATUS_ACK_RETRY_SECONDS:
            return
        self.send_line(self.status_line())
        self.last_status_sent_at = now

    def ensure_bridge_idle(self) -> bool:
        if self.active_turn_id is None and self.pending_approval is None and self.pending_user_input_request_id is None:
            return True
        print("Keyboard-owned Codex task is already running; command ignored")
        self.refresh_visible_status(force=True)
        return False

    def ensure_idle(self) -> bool:
        """Compatibility alias for older callers and tests."""
        return self.ensure_bridge_idle()

    def ensure_app_for_active_project(self) -> bool:
        target = self.last_active_project or self.args.project
        try:
            target = target.resolve()
        except (OSError, RuntimeError) as exc:
            print(f"Unable to resolve active Codex project: {exc}", file=sys.stderr)
            self.send_status("ERROR")
            return False
        if not target.is_dir():
            print(f"Active Codex project no longer exists: {target}", file=sys.stderr)
            target = self.args.project.resolve()

        if self.app is not None and self.thread_id is not None and self.app_project == target:
            return True

        if self.app is not None:
            self.app.close()
        self.app = None
        self.app_project = None
        self.thread_id = None
        self.active_turn_id = None
        self.thread_has_task = False
        self.bridge_paused = False
        self.last_prompt = None
        self.pending_approval = None
        self.pending_user_input_request_id = None
        try:
            self.start_app_server(target)
            print(f"Keyboard tasks now target: {target}")
            return True
        except AppServerError as exc:
            print(f"Unable to switch Codex project: {exc}", file=sys.stderr)
            self.send_status("ERROR")
            return False

    def start_prompt(self, prompt: str, *, operation: str) -> bool:
        if self.app is None or self.thread_id is None:
            return False
        try:
            params: dict[str, Any] = {
                "threadId": self.thread_id,
                "input": [{"type": "text", "text": prompt}],
                "approvalPolicy": "on-request",
                "sandboxPolicy": {
                    "type": "workspaceWrite",
                    "writableRoots": [str(self.app_project or self.args.project)],
                    "networkAccess": False,
                },
            }
            if self.current_effort:
                params["effort"] = self.current_effort
            result = self.app.request("turn/start", params)
            self.active_turn_id = result["turn"]["id"]
            self.thread_has_task = True
            self.bridge_paused = False
            self.last_prompt = prompt
            self.pending_user_input_request_id = None
            self.ready_after = 0.0
            self.terminal_status = None
            self.refresh_visible_status()
            return True
        except (AppServerError, KeyError) as exc:
            print(f"Unable to {operation}: {exc}", file=sys.stderr)
            self.send_status("ERROR")
            return False

    def start_new_task(self) -> None:
        if not self.ensure_bridge_idle() or not self.ensure_app_for_active_project():
            return
        if self.app is None or self.app_project is None:
            return
        prompt = self.tasks.get("NEW_TASK") or self.tasks.get("ANALYZE")
        if not prompt:
            print("NEW_TASK task is missing from the task file", file=sys.stderr)
            self.send_status("ERROR")
            return
        try:
            self.send_status("CREATING", force=True)
            if self.thread_has_task or self.bridge_paused:
                self.start_thread(self.app_project)
            self.start_prompt(prompt, operation="start a new task")
        except (AppServerError, KeyError) as exc:
            print(f"Unable to create a new task: {exc}", file=sys.stderr)
            self.send_status("ERROR")

    def start_analysis(self) -> None:
        if not self.ensure_bridge_idle() or not self.ensure_app_for_active_project():
            return
        if self.app is None or self.thread_id is None:
            return
        prompt = self.tasks.get("ANALYZE")
        if not prompt:
            print("ANALYZE task is missing from the task file", file=sys.stderr)
            self.send_status("ERROR")
            return
        self.start_prompt(prompt, operation="start analysis")

    def start_review(self) -> None:
        if not self.ensure_bridge_idle() or not self.ensure_app_for_active_project():
            return
        if self.app is None or self.thread_id is None:
            return
        try:
            result = self.app.request(
                "review/start",
                {
                    "threadId": self.thread_id,
                    "target": {"type": "uncommittedChanges"},
                    "delivery": "inline",
                },
            )
            self.active_turn_id = result["turn"]["id"]
            self.thread_has_task = True
            self.bridge_paused = False
            self.pending_user_input_request_id = None
            self.ready_after = 0.0
            self.terminal_status = None
            self.refresh_visible_status()
        except (AppServerError, KeyError) as exc:
            print(f"Unable to start review: {exc}", file=sys.stderr)
            self.send_status("ERROR")

    def answer_approval(self, decision: str) -> None:
        if self.pending_approval is None or self.app is None:
            print(f"No pending approval; {decision} ignored")
            return
        request_id, method, params = self.pending_approval
        if method == PERMISSIONS_APPROVAL_METHOD:
            result = {
                "permissions": params.get("permissions", {}) if decision == "accept" else {},
                "scope": "turn",
            }
        else:
            result = {"decision": decision}
        self.app.respond(request_id, result)
        self.pending_approval = None
        self.refresh_visible_status()
        print(f"Approval response sent for {method}: {decision}")

    def pause_active_turn(self) -> None:
        if self.active_turn_id is None or self.app is None or self.thread_id is None:
            print("No active keyboard-owned turn to pause")
            self.refresh_visible_status(force=True)
            return
        try:
            self.send_status("PAUSING", force=True)
            self.app.request(
                "turn/interrupt",
                {"threadId": self.thread_id, "turnId": self.active_turn_id},
                timeout=10.0,
            )
            self.active_turn_id = None
            self.pending_approval = None
            self.pending_user_input_request_id = None
            self.bridge_paused = True
            self.ready_after = 0.0
            self.terminal_status = None
            self.refresh_visible_status()
        except AppServerError as exc:
            print(f"Unable to pause turn: {exc}", file=sys.stderr)
            self.refresh_visible_status(force=True)

    def interrupt_active_turn(self) -> None:
        """Compatibility alias for the original firmware command."""
        self.pause_active_turn()

    def resume_paused_task(self) -> None:
        if not self.bridge_paused or self.active_turn_id is not None:
            print("No paused keyboard-owned task to resume")
            self.refresh_visible_status(force=True)
            return
        if self.app is None or self.thread_id is None:
            print("Paused task has no active Codex thread", file=sys.stderr)
            self.send_status("ERROR")
            return
        prompt = self.tasks.get("RESUME") or (
            "Continue the task that was just interrupted. Inspect the current workspace state first, "
            "do not repeat completed work, and then finish the original task."
        )
        self.send_status("RESUMING", force=True)
        if not self.start_prompt(prompt, operation="resume the paused task"):
            self.bridge_paused = True
            self.refresh_visible_status(force=True)

    def adjust_effort(self, direction: int) -> None:
        efforts = self.supported_efforts or list(DEFAULT_REASONING_EFFORTS)
        if not efforts:
            return
        current = self.current_effort if self.current_effort in efforts else efforts[0]
        index = efforts.index(current)
        selected = efforts[(index + (1 if direction > 0 else -1)) % len(efforts)]
        if self.app is not None and self.thread_id is not None:
            try:
                self.app.request(
                    "thread/settings/update",
                    {"threadId": self.thread_id, "effort": selected},
                    timeout=10.0,
                )
            except AppServerError as exc:
                print(f"Unable to change reasoning effort: {exc}", file=sys.stderr)
                self.send_status("ERROR")
                return
        self.current_effort = selected
        self.refresh_visible_status(force=True)
        applies = "next turn" if self.active_turn_id is not None else "subsequent turns"
        print(f"Reasoning effort changed to {selected} ({applies})")

    def handle_app_event(self, event: dict[str, Any]) -> None:
        method = event.get("method", "")
        params = event.get("params") or {}
        if method == "turn/started":
            self.active_turn_id = (params.get("turn") or {}).get("id", self.active_turn_id)
            self.thread_has_task = True
            self.bridge_paused = False
            self.pending_user_input_request_id = None
            self.paused_tasks.pop(self.thread_id or "bridge-app-session", None)
            self.ready_after = 0.0
            self.terminal_status = None
            self.refresh_visible_status()
        elif method in APPROVAL_METHODS and "id" in event:
            if self.pending_approval is not None:
                decline_result = {"permissions": {}} if method == PERMISSIONS_APPROVAL_METHOD else {"decision": "decline"}
                self.app.respond(event["id"], decline_result)  # type: ignore[union-attr]
                print("Extra approval request declined; only one hardware approval can be pending", file=sys.stderr)
                return
            self.pending_approval = (event["id"], method, params)
            self.refresh_visible_status()
            print(f"Approval requested: {method}")
        elif method == "item/tool/requestUserInput" and "id" in event:
            self.pending_user_input_request_id = event["id"]
            self.refresh_visible_status()
            print("Waiting for user input")
        elif method == "serverRequest/resolved":
            request_id = params.get("requestId")
            if request_id == self.pending_user_input_request_id:
                self.pending_user_input_request_id = None
                self.refresh_visible_status()
                print("User input received; Codex resumed")
        elif method == "turn/completed":
            turn = params.get("turn") or {}
            status = turn.get("status")
            self.active_turn_id = None
            self.pending_approval = None
            self.pending_user_input_request_id = None
            if status == "completed":
                self.bridge_paused = False
                self.hold_terminal_status("DONE")
            elif status == "interrupted":
                self.bridge_paused = True
                self.ready_after = 0.0
                self.terminal_status = None
                self.refresh_visible_status()
            else:
                self.hold_terminal_status("ERROR")
            print(f"Turn completed with status: {status}")
        elif method == "item/agentMessage/delta":
            print(params.get("delta", ""), end="", flush=True)
        elif method == "item/completed":
            item = params.get("item") or {}
            if item.get("type") == "agentMessage":
                print()
        elif method == "error":
            print(f"Codex error: {params}", file=sys.stderr)
            self.hold_terminal_status("ERROR")
        elif method == "bridge/appServerExited":
            raise AppServerError(f"Codex app server exited: {params.get('code')}")

    def run(self) -> None:
        self.start_app_server()
        self.start_hook_listener()
        while True:
            try:
                if self.serial_port is None or not self.serial_port.is_open:
                    self.connect_serial()

                now = time.monotonic()
                self.expire_stale_hook_sessions(now)
                if now - self.last_ping >= PING_INTERVAL_SECONDS:
                    self.send_line("CX<PING")
                    # Reassert the current state on every heartbeat. This makes
                    # a one-off serial transfer loss self-heal instead of
                    # leaving the LCD on an obsolete status indefinitely.
                    self.send_status(self.current_status, self.current_status_count, force=True)
                    self.last_ping = now
                if self.ready_after and now >= self.ready_after and not self.has_active_turns():
                    self.refresh_visible_status()

                raw = self.serial_port.readline()
                if raw:
                    self.handle_device_line(raw.decode("utf-8", errors="replace").strip())
                self.retry_unacknowledged_status(time.monotonic())

                self.drain_hook_events()

                assert self.app is not None
                while True:
                    try:
                        event = self.app.events.get_nowait()
                    except queue.Empty:
                        break
                    self.handle_app_event(event)
            except (serial.SerialException, OSError) as exc:
                print(f"Serial connection lost: {exc}; retrying...", file=sys.stderr)
                if self.serial_port is not None:
                    try:
                        self.serial_port.close()
                    except OSError:
                        pass
                self.serial_port = None
                self.current_status = "DISCONNECTED"
                self.current_status_count = 1
                self.status_acknowledged = False
                time.sleep(1.0)

    def close(self) -> None:
        if self.serial_port is not None and self.serial_port.is_open:
            try:
                self.send_status("DISCONNECTED", force=True)
                self.serial_port.close()
            except OSError:
                pass
        if self.hook_socket is not None:
            self.hook_socket.close()
            self.hook_socket = None
        if self.app is not None:
            self.app.close()
            self.app = None
            self.app_project = None


def probe_keyboard_port(device: str, baud: int) -> bool:
    if serial is None:
        return False
    connection: Any = None
    try:
        connection = serial.Serial(device, baud, timeout=0.20, write_timeout=0.5)
        if hasattr(connection, "reset_input_buffer"):
            connection.reset_input_buffer()
        connection.write(b"CX<PING\n")
        deadline = time.monotonic() + PORT_PROBE_TIMEOUT_SECONDS
        while time.monotonic() < deadline:
            line = connection.readline()
            if line.decode("utf-8", errors="replace").strip() == "CX>PONG":
                return True
    except (serial.SerialException, OSError):
        return False
    finally:
        if connection is not None:
            try:
                connection.close()
            except OSError:
                pass
    return False


def discover_keyboard_port(baud: int = 115200) -> str:
    if list_ports is None:
        raise RuntimeError("pyserial is required; install requirements-codex-bridge.txt")
    ports = list(list_ports.comports())
    usb_ports = [port for port in ports if port.vid is not None]
    usb_ports.sort(
        key=lambda port: (
            0
            if port.vid == CH340_USB_VID and port.pid == CH340_USB_PID
            else 1
            if port.vid == ESPRESSIF_USB_VID
            else 2,
            port.device,
        )
    )
    for port in usb_ports:
        if probe_keyboard_port(port.device, baud):
            print(f"Discovered keyboard bridge on {port.device} ({port.description})")
            return port.device
    details = ", ".join(f"{p.device} (VID={p.vid!r}, {p.description})" for p in ports) or "none"
    raise RuntimeError(
        "Cannot find a serial port that answers the keyboard CX<PING handshake. "
        f"Detected ports: {details}. Pass --port COMx explicitly if probing is blocked."
    )


def load_tasks(path: Path) -> dict[str, str]:
    data = json.loads(path.read_text(encoding="utf-8"))
    if not isinstance(data, dict) or not all(isinstance(k, str) and isinstance(v, str) for k, v in data.items()):
        raise ValueError("Task file must be a JSON object containing string prompts")
    return data


def parse_args() -> argparse.Namespace:
    software_dir = Path(__file__).resolve().parent
    default_project = software_dir.parent
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", help="Keyboard USB CDC port, for example COM50")
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--project", type=Path, default=default_project)
    parser.add_argument("--tasks", type=Path, default=software_dir / "codex_tasks.json")
    parser.add_argument("--codex-bin", default="codex")
    parser.add_argument("--open-app", action=argparse.BooleanOptionalAction, default=True)
    parser.add_argument("--hook-host", default=DEFAULT_HOOK_HOST)
    parser.add_argument("--hook-port", type=int, default=DEFAULT_HOOK_PORT)
    parser.add_argument(
        "--hook-stale-timeout",
        type=float,
        default=DEFAULT_HOOK_STALE_TIMEOUT_SECONDS,
        help="Seconds without a lifecycle event before an orphaned hook session is cleared",
    )
    parser.add_argument("--dry-run", action="store_true", help="Validate configuration without serial, app server, or model access")
    args = parser.parse_args()
    args.project = args.project.resolve()
    args.tasks = args.tasks.resolve()
    return args


def main() -> int:
    args = parse_args()
    try:
        if not args.project.is_dir():
            raise ValueError(f"Project directory does not exist: {args.project}")
        tasks = load_tasks(args.tasks)
        if "ANALYZE" not in tasks:
            raise ValueError("Task file does not define ANALYZE")
        if args.hook_stale_timeout <= 0:
            raise ValueError("--hook-stale-timeout must be greater than zero")
        if args.dry_run:
            print(
                f"Configuration OK\nproject={args.project}\ntasks={args.tasks}\n"
                f"open_app={args.open_app}\nhook={args.hook_host}:{args.hook_port}"
            )
            return 0
        if serial is None:
            raise RuntimeError("pyserial is required; install requirements-codex-bridge.txt")

        bridge = CodexKeyboardBridge(args, tasks)
        try:
            bridge.run()
        finally:
            bridge.close()
    except (AppServerError, RuntimeError, ValueError, json.JSONDecodeError) as exc:
        print(f"Bridge error: {exc}", file=sys.stderr)
        return 1
    except KeyboardInterrupt:
        print("Bridge stopped")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
