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
APPROVAL_METHODS = {
    "item/commandExecution/requestApproval",
    "item/fileChange/requestApproval",
    "item/permissions/requestApproval",
}
PERMISSIONS_APPROVAL_METHOD = "item/permissions/requestApproval"


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
        self.thread_id: str | None = None
        self.active_turn_id: str | None = None
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
        self.hook_terminal_turns: dict[str, str] = {}
        self.paused_tasks: dict[str, float] = {}
        self.last_ping = 0.0
        self.ready_after = 0.0
        self.terminal_status: str | None = None
        self.terminal_status_count = 1
        self.current_status = "DISCONNECTED"
        self.current_status_count = 1
        self.status_acknowledged = False
        self.last_status_sent_at = 0.0

    def start_app_server(self) -> None:
        executable = shutil.which(self.args.codex_bin)
        if executable is None:
            raise AppServerError(f"Cannot find '{self.args.codex_bin}' in PATH")
        self.app = AppServerClient(executable, self.args.project)
        self.app.request(
            "initialize",
            {"clientInfo": {"name": "fun_modular_keyboard", "title": "Fun Modular Keyboard", "version": "1.0.0"}},
        )
        self.app.notify("initialized")
        result = self.app.request(
            "thread/start",
            {
                "cwd": str(self.args.project),
                "approvalPolicy": "on-request",
                "sandbox": "workspace-write",
                "serviceName": "fun_modular_keyboard",
            },
        )
        self.thread_id = result["thread"]["id"]
        print(f"Codex thread ready: {self.thread_id}")
        if self.args.open_app and os.name == "nt":
            try:
                os.startfile(f"codex://threads/{self.thread_id}")  # type: ignore[attr-defined]
            except OSError as exc:
                print(f"Unable to open Codex desktop thread: {exc}", file=sys.stderr)

    def connect_serial(self) -> None:
        assert serial is not None
        port = self.args.port or discover_keyboard_port()
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
            self.send_line(f"CX<STATUS|{status}|{count}")
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

        if approval_tasks:
            status = "APPROVAL"
            count = len(approval_tasks)
        elif waiting_tasks:
            status = "WAITING"
            count = len(waiting_tasks)
        elif running_tasks:
            status = "RUNNING"
            count = len(running_tasks)
        elif self.paused_tasks:
            status = "PAUSED"
            count = len(self.paused_tasks)
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

    def mark_hook_session_active(self, payload: dict[str, Any], *, approval: bool = False) -> None:
        session_id = self.hook_session_key(payload)
        self.hook_active_turns[session_id] = str(payload.get("turn_id") or "unknown-turn")
        self.hook_last_seen[session_id] = time.monotonic()
        self.hook_terminal_turns.pop(session_id, None)
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
            self.hook_terminal_turns.pop(session_id, None)
            self.paused_tasks.pop(session_id, None)

    def is_late_terminal_event(self, payload: dict[str, Any]) -> bool:
        session_id = self.hook_session_key(payload)
        turn_id = str(payload.get("turn_id") or "unknown-turn")
        return self.hook_terminal_turns.get(session_id) == turn_id

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
            self.clear_hook_session(session_id)
            print(f"[codex-hook] expired stale session={session_id}")
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
        ignored_late_event = False
        if event == "UserPromptSubmit":
            self.ready_after = 0.0
            self.terminal_status = None
            self.mark_hook_session_active(payload)
            self.refresh_visible_status()
        elif event == "PermissionRequest":
            if not self.is_late_terminal_event(payload):
                self.mark_hook_session_active(payload, approval=True)
                self.refresh_visible_status()
            else:
                ignored_late_event = True
        elif event == "PostToolUse":
            if not self.is_late_terminal_event(payload):
                self.mark_hook_session_active(payload)
                self.refresh_visible_status()
            else:
                ignored_late_event = True
        elif event == "Stop":
            if self.is_late_terminal_event(payload) or self.is_stale_terminal_event(payload):
                ignored_late_event = True
            else:
                self.clear_hook_session(session_id)
                self.hook_terminal_turns[session_id] = str(payload.get("turn_id") or "unknown-turn")
                self.hold_terminal_status("DONE")
        elif event == "Interrupt":
            if self.is_late_terminal_event(payload) or self.is_stale_terminal_event(payload):
                ignored_late_event = True
            else:
                self.clear_hook_session(session_id)
                self.hook_terminal_turns[session_id] = str(payload.get("turn_id") or "unknown-turn")
                self.paused_tasks[session_id] = time.monotonic() + PAUSED_HOLD_SECONDS
                self.ready_after = 0.0
                self.terminal_status = None
                self.refresh_visible_status()
        elif event == "SessionStart":
            self.refresh_visible_status()
        elif event == "SessionEnd":
            self.clear_hook_session(session_id, clear_terminal=True)
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
            if len(parts) == 3:
                try:
                    count = int(parts[2])
                except ValueError:
                    return
                if parts[1] == self.current_status and count == self.current_status_count:
                    self.status_acknowledged = True
        elif line == "CX>TASK|ANALYZE":
            self.start_analysis()
        elif line == "CX>TASK|REVIEW":
            self.start_review()
        elif line == "CX>ACTION|ACCEPT":
            self.answer_approval("accept")
        elif line == "CX>ACTION|DECLINE":
            if self.pending_approval is not None:
                self.answer_approval("decline")
            else:
                self.interrupt_active_turn()
        elif line == "CX>ACTION|INTERRUPT":
            self.interrupt_active_turn()

    def retry_unacknowledged_status(self, now: float) -> None:
        if self.status_acknowledged or now - self.last_status_sent_at < STATUS_ACK_RETRY_SECONDS:
            return
        self.send_line(f"CX<STATUS|{self.current_status}|{self.current_status_count}")
        self.last_status_sent_at = now

    def ensure_idle(self) -> bool:
        if not self.has_active_turns():
            return True
        print("Codex is already running; command ignored")
        self.refresh_visible_status(force=True)
        return False

    def start_analysis(self) -> None:
        if not self.ensure_idle() or self.app is None or self.thread_id is None:
            return
        prompt = self.tasks.get("ANALYZE")
        if not prompt:
            print("ANALYZE task is missing from the task file", file=sys.stderr)
            self.send_status("ERROR")
            return
        try:
            result = self.app.request(
                "turn/start",
                {
                    "threadId": self.thread_id,
                    "input": [{"type": "text", "text": prompt}],
                    "approvalPolicy": "on-request",
                    "sandboxPolicy": {
                        "type": "workspaceWrite",
                        "writableRoots": [str(self.args.project)],
                        "networkAccess": False,
                    },
                },
            )
            self.active_turn_id = result["turn"]["id"]
            self.pending_user_input_request_id = None
            self.ready_after = 0.0
            self.terminal_status = None
            self.refresh_visible_status()
        except (AppServerError, KeyError) as exc:
            print(f"Unable to start analysis: {exc}", file=sys.stderr)
            self.send_status("ERROR")

    def start_review(self) -> None:
        if not self.ensure_idle() or self.app is None or self.thread_id is None:
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

    def interrupt_active_turn(self) -> None:
        if self.active_turn_id is None or self.app is None or self.thread_id is None:
            print("No active turn to interrupt")
            return
        try:
            self.app.request(
                "turn/interrupt",
                {"threadId": self.thread_id, "turnId": self.active_turn_id},
                timeout=10.0,
            )
        except AppServerError as exc:
            print(f"Unable to interrupt turn: {exc}", file=sys.stderr)

    def handle_app_event(self, event: dict[str, Any]) -> None:
        method = event.get("method", "")
        params = event.get("params") or {}
        if method == "turn/started":
            self.active_turn_id = (params.get("turn") or {}).get("id", self.active_turn_id)
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
                self.hold_terminal_status("DONE")
            elif status == "interrupted":
                self.paused_tasks[self.thread_id or "bridge-app-session"] = (
                    time.monotonic() + PAUSED_HOLD_SECONDS
                )
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


def discover_keyboard_port() -> str:
    if list_ports is None:
        raise RuntimeError("pyserial is required; install requirements-codex-bridge.txt")
    ports = list(list_ports.comports())
    espressif = [port.device for port in ports if port.vid == ESPRESSIF_USB_VID]
    if len(espressif) == 1:
        return espressif[0]
    details = ", ".join(f"{p.device} (VID={p.vid!r}, {p.description})" for p in ports) or "none"
    raise RuntimeError(
        "Cannot select the keyboard CDC port safely. "
        f"Detected ports: {details}. Pass --port COMx explicitly."
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
