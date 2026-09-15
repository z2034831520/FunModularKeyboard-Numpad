import unittest
import json
import tempfile
from pathlib import Path
from types import SimpleNamespace
from unittest import mock

import software.codex_bridge as codex_bridge_module
from software.codex_bridge import CodexKeyboardBridge
from software.codex_status_hook import send_event


class CodexHookStateTests(unittest.TestCase):
    def make_bridge(self, *, stale_timeout=600.0):
        bridge = CodexKeyboardBridge(
            SimpleNamespace(
                hook_stale_timeout=stale_timeout,
                project=Path.cwd(),
                codex_bin="codex",
                open_app=False,
            ),
            {},
        )
        sent = []
        bridge.send_line = sent.append
        return bridge, sent

    @staticmethod
    def event(name, session_id="session-a", turn_id=None, cwd=None):
        payload = {
            "version": 1,
            "hook_event_name": name,
            "session_id": session_id,
        }
        if turn_id is not None:
            payload["turn_id"] = turn_id
        if cwd is not None:
            payload["cwd"] = str(cwd)
        return payload

    def test_hook_forwarder_includes_session_working_directory(self):
        payload = self.event("UserPromptSubmit", turn_id="turn-a", cwd=r"D:\\Work\\ProjectA")
        with mock.patch("software.codex_status_hook.socket.socket") as socket_factory:
            sender = socket_factory.return_value.__enter__.return_value
            send_event(payload, "127.0.0.1", 18765)

        forwarded = json.loads(sender.sendto.call_args_list[0].args[0].decode("utf-8"))
        self.assertEqual(r"D:\\Work\\ProjectA", forwarded["cwd"])

    def test_bridge_remembers_most_recent_hook_project(self):
        bridge, _ = self.make_bridge()
        with tempfile.TemporaryDirectory() as temp_dir:
            project = Path(temp_dir).resolve()
            bridge.handle_hook_event(self.event("UserPromptSubmit", turn_id="turn-a", cwd=project))
            self.assertEqual(project, bridge.last_active_project)

    def test_keyboard_task_switches_to_most_recent_hook_project(self):
        with tempfile.TemporaryDirectory() as fallback_dir, tempfile.TemporaryDirectory() as active_dir:
            args = SimpleNamespace(
                hook_stale_timeout=600.0,
                project=Path(fallback_dir).resolve(),
                codex_bin="codex",
                open_app=False,
            )
            bridge = CodexKeyboardBridge(args, {})
            bridge.app = mock.Mock()
            bridge.app_project = args.project
            bridge.thread_id = "old-thread"
            bridge.last_active_project = Path(active_dir).resolve()
            started = []

            def fake_start(project=None):
                started.append(project)
                bridge.app = mock.Mock()
                bridge.app_project = project
                bridge.thread_id = "new-thread"

            bridge.start_app_server = fake_start

            self.assertTrue(bridge.ensure_app_for_active_project())
            self.assertEqual([Path(active_dir).resolve()], started)
            self.assertEqual("new-thread", bridge.thread_id)

    def test_hook_events_for_bridge_owned_thread_are_not_double_counted(self):
        bridge, _ = self.make_bridge()
        bridge.thread_id = "bridge-thread"
        bridge.active_turn_id = "turn-a"
        bridge.refresh_visible_status()

        bridge.handle_hook_event(
            self.event("UserPromptSubmit", session_id="bridge-thread", turn_id="turn-a")
        )

        self.assertEqual({}, bridge.hook_active_turns)
        self.assertEqual("RUNNING", bridge.current_status)
        self.assertEqual(1, bridge.current_status_count)

    def test_port_discovery_uses_keyboard_protocol_handshake(self):
        class Port:
            def __init__(self, device, vid, pid, description):
                self.device = device
                self.vid = vid
                self.pid = pid
                self.description = description

        class Connection:
            def __init__(self, device, *_args, **_kwargs):
                self.device = device
                self.is_open = True

            def reset_input_buffer(self):
                pass

            def write(self, _data):
                pass

            def readline(self):
                return b"CX>PONG\n" if self.device == "COM50" else b""

            def close(self):
                self.is_open = False

        fake_serial = SimpleNamespace(Serial=Connection, SerialException=OSError)
        fake_ports = SimpleNamespace(
            comports=lambda: [
                Port("COM45", 0xC019, 0x0401, "USB Composite Device"),
                Port("COM50", 0x1A86, 0x7523, "USB-SERIAL CH340"),
            ]
        )
        with mock.patch.object(codex_bridge_module, "serial", fake_serial), mock.patch.object(
            codex_bridge_module, "list_ports", fake_ports
        ):
            self.assertEqual("COM50", codex_bridge_module.discover_keyboard_port())

    def test_new_prompt_replaces_stale_turn_in_same_session(self):
        bridge, sent = self.make_bridge()

        bridge.handle_hook_event(self.event("UserPromptSubmit", turn_id="old-turn"))
        bridge.handle_hook_event(self.event("UserPromptSubmit", turn_id="new-turn"))
        bridge.handle_hook_event(self.event("Stop", turn_id="new-turn"))

        self.assertFalse(bridge.has_active_turns())
        self.assertEqual("DONE", bridge.current_status)
        self.assertEqual("CX<STATUS|DONE|1", sent[-1])

    def test_stop_keeps_a_different_session_running(self):
        bridge, _ = self.make_bridge()

        bridge.handle_hook_event(self.event("UserPromptSubmit", session_id="session-a", turn_id="turn-a"))
        bridge.handle_hook_event(self.event("UserPromptSubmit", session_id="session-b", turn_id="turn-b"))
        bridge.handle_hook_event(self.event("Stop", session_id="session-b", turn_id="turn-b"))

        self.assertTrue(bridge.has_active_turns())
        self.assertEqual({"session-a": "turn-a"}, bridge.hook_active_turns)
        self.assertEqual("RUNNING", bridge.current_status)
        self.assertEqual(1, bridge.current_status_count)

    def test_stale_hook_session_expires_to_ready(self):
        bridge, sent = self.make_bridge(stale_timeout=10.0)
        bridge.handle_hook_event(self.event("UserPromptSubmit", turn_id="orphaned-turn"))
        bridge.hook_last_seen["session-a"] = 100.0

        bridge.expire_stale_hook_sessions(110.0)

        self.assertFalse(bridge.has_active_turns())
        self.assertEqual("READY", bridge.current_status)
        self.assertEqual("CX<STATUS|READY|1", sent[-1])

    def test_late_post_tool_use_cannot_reopen_completed_turn(self):
        bridge, _ = self.make_bridge()
        turn = self.event("UserPromptSubmit", turn_id="turn-a")
        bridge.handle_hook_event(turn)
        bridge.handle_hook_event(self.event("Stop", turn_id="turn-a"))

        bridge.handle_hook_event(self.event("PostToolUse", turn_id="turn-a"))

        self.assertFalse(bridge.has_active_turns())
        self.assertEqual("DONE", bridge.current_status)

    def test_late_duplicate_prompt_cannot_reopen_completed_turn(self):
        bridge, _ = self.make_bridge()
        prompt = self.event("UserPromptSubmit", turn_id="turn-a")
        bridge.handle_hook_event(prompt)
        bridge.handle_hook_event(self.event("Stop", turn_id="turn-a"))

        bridge.handle_hook_event(prompt)

        self.assertFalse(bridge.has_active_turns())
        self.assertEqual("DONE", bridge.current_status)

        bridge.handle_hook_event(
            self.event("UserPromptSubmit", session_id="session-b", turn_id="turn-b")
        )
        self.assertEqual({"session-b": "turn-b"}, bridge.hook_active_turns)
        self.assertEqual("RUNNING", bridge.current_status)
        self.assertEqual(1, bridge.current_status_count)

    def test_old_turn_event_cannot_replace_newer_active_turn(self):
        bridge, _ = self.make_bridge()
        bridge.handle_hook_event(self.event("UserPromptSubmit", turn_id="turn-a"))
        bridge.handle_hook_event(self.event("Stop", turn_id="turn-a"))
        bridge.handle_hook_event(self.event("UserPromptSubmit", turn_id="turn-b"))

        bridge.handle_hook_event(self.event("PostToolUse", turn_id="turn-a"))
        bridge.handle_hook_event(self.event("Stop", turn_id="turn-b"))

        self.assertFalse(bridge.has_active_turns())
        self.assertEqual("DONE", bridge.current_status)

    def test_late_session_start_does_not_clear_active_turn(self):
        bridge, _ = self.make_bridge()
        bridge.handle_hook_event(self.event("UserPromptSubmit", turn_id="turn-a"))

        bridge.handle_hook_event(self.event("SessionStart", turn_id=None))

        self.assertTrue(bridge.has_active_turns())
        self.assertEqual("RUNNING", bridge.current_status)

    def test_force_reasserts_unchanged_status(self):
        bridge, sent = self.make_bridge()
        bridge.send_status("READY")
        bridge.send_status("READY", force=True)

        self.assertEqual(["CX<STATUS|READY|1", "CX<STATUS|READY|1"], sent)

    def test_interrupt_is_visible_as_paused(self):
        bridge, sent = self.make_bridge()
        bridge.handle_hook_event(self.event("UserPromptSubmit", turn_id="turn-a"))

        bridge.handle_hook_event(self.event("Interrupt", turn_id="turn-a"))

        self.assertFalse(bridge.has_active_turns())
        self.assertEqual("PAUSED", bridge.current_status)
        self.assertEqual("CX<STATUS|PAUSED|1", sent[-1])

    def test_new_prompt_overrides_paused_status(self):
        bridge, _ = self.make_bridge()
        bridge.handle_hook_event(self.event("Interrupt", turn_id="turn-a"))

        bridge.handle_hook_event(self.event("UserPromptSubmit", turn_id="turn-b"))

        self.assertEqual("RUNNING", bridge.current_status)

    def test_app_server_waiting_for_input_resumes_running(self):
        bridge, _ = self.make_bridge()
        bridge.handle_app_event(
            {"method": "turn/started", "params": {"turn": {"id": "turn-a"}}}
        )

        bridge.handle_app_event(
            {"id": 17, "method": "item/tool/requestUserInput", "params": {"turnId": "turn-a"}}
        )
        self.assertEqual("WAITING", bridge.current_status)

        bridge.handle_app_event(
            {"method": "serverRequest/resolved", "params": {"requestId": 17}}
        )
        self.assertEqual("RUNNING", bridge.current_status)

    def test_interrupted_app_server_turn_is_visible_as_paused(self):
        bridge, _ = self.make_bridge()
        bridge.handle_app_event(
            {"method": "turn/started", "params": {"turn": {"id": "turn-a"}}}
        )

        bridge.handle_app_event(
            {"method": "turn/completed", "params": {"turn": {"id": "turn-a", "status": "interrupted"}}}
        )

        self.assertEqual("PAUSED", bridge.current_status)

    def test_running_count_updates_when_parallel_session_stops(self):
        bridge, sent = self.make_bridge()
        bridge.handle_hook_event(self.event("UserPromptSubmit", session_id="session-a", turn_id="turn-a"))
        bridge.handle_hook_event(self.event("UserPromptSubmit", session_id="session-b", turn_id="turn-b"))

        self.assertEqual("RUNNING", bridge.current_status)
        self.assertEqual(2, bridge.current_status_count)
        self.assertEqual("CX<STATUS|RUNNING|2", sent[-1])

        bridge.handle_hook_event(self.event("Stop", session_id="session-b", turn_id="turn-b"))

        self.assertEqual("RUNNING", bridge.current_status)
        self.assertEqual(1, bridge.current_status_count)
        self.assertEqual("CX<STATUS|RUNNING|1", sent[-1])

    def test_approval_count_has_priority_over_running_sessions(self):
        bridge, _ = self.make_bridge()
        bridge.handle_hook_event(self.event("UserPromptSubmit", session_id="running", turn_id="turn-a"))
        bridge.handle_hook_event(self.event("PermissionRequest", session_id="approval-a", turn_id="turn-b"))
        bridge.handle_hook_event(self.event("PermissionRequest", session_id="approval-b", turn_id="turn-c"))

        self.assertEqual("APPROVAL", bridge.current_status)
        self.assertEqual(2, bridge.current_status_count)

    def test_parallel_interrupts_are_counted_after_running_tasks_finish(self):
        bridge, _ = self.make_bridge()
        bridge.handle_hook_event(self.event("UserPromptSubmit", session_id="session-a", turn_id="turn-a"))
        bridge.handle_hook_event(self.event("UserPromptSubmit", session_id="session-b", turn_id="turn-b"))

        bridge.handle_hook_event(self.event("Interrupt", session_id="session-a", turn_id="turn-a"))
        self.assertEqual("RUNNING", bridge.current_status)
        self.assertEqual(1, bridge.current_status_count)

        bridge.handle_hook_event(self.event("Interrupt", session_id="session-b", turn_id="turn-b"))
        self.assertEqual("PAUSED", bridge.current_status)
        self.assertEqual(2, bridge.current_status_count)

    def test_stale_stop_cannot_clear_a_newer_turn_in_same_session(self):
        bridge, _ = self.make_bridge()
        bridge.handle_hook_event(self.event("UserPromptSubmit", turn_id="old-turn"))
        bridge.handle_hook_event(self.event("UserPromptSubmit", turn_id="new-turn"))

        bridge.handle_hook_event(self.event("Stop", turn_id="old-turn"))

        self.assertEqual({"session-a": "new-turn"}, bridge.hook_active_turns)
        self.assertEqual("RUNNING", bridge.current_status)

    def test_stale_interrupt_cannot_pause_a_newer_turn_in_same_session(self):
        bridge, _ = self.make_bridge()
        bridge.handle_hook_event(self.event("UserPromptSubmit", turn_id="old-turn"))
        bridge.handle_hook_event(self.event("UserPromptSubmit", turn_id="new-turn"))

        bridge.handle_hook_event(self.event("Interrupt", turn_id="old-turn"))

        self.assertEqual({"session-a": "new-turn"}, bridge.hook_active_turns)
        self.assertEqual("RUNNING", bridge.current_status)

    def test_state_ordering_hooks_are_synchronous(self):
        hooks_path = Path(__file__).resolve().parents[1] / ".codex" / "hooks.json"
        hooks = json.loads(hooks_path.read_text(encoding="utf-8"))["hooks"]

        for event in ("PermissionRequest", "PostToolUse", "Interrupt"):
            handler = hooks[event][0]["hooks"][0]
            self.assertFalse(handler.get("async", False), event)

    def test_matching_status_acknowledges_latest_serial_state(self):
        bridge, _ = self.make_bridge()
        bridge.send_status("RUNNING", 2)
        self.assertFalse(bridge.status_acknowledged)

        bridge.handle_device_line("CX>STATUS|RUNNING|2")

        self.assertTrue(bridge.status_acknowledged)

    def test_status_and_ack_include_reasoning_effort(self):
        bridge, sent = self.make_bridge()
        bridge.current_effort = "high"

        bridge.send_status("READY", force=True)
        bridge.handle_device_line("CX>STATUS|READY|1|HIGH")

        self.assertEqual("CX<STATUS|READY|1|HIGH", sent[-1])
        self.assertTrue(bridge.status_acknowledged)

    def test_decline_without_approval_does_not_pause_task(self):
        bridge, _ = self.make_bridge()
        bridge.pause_active_turn = mock.Mock()

        bridge.handle_device_line("CX>ACTION|DECLINE")

        bridge.pause_active_turn.assert_not_called()

    def test_pause_requests_interrupt_for_bridge_owned_turn(self):
        bridge, sent = self.make_bridge()
        bridge.app = mock.Mock()
        bridge.thread_id = "thread-a"
        bridge.active_turn_id = "turn-a"

        bridge.pause_active_turn()

        bridge.app.request.assert_called_once_with(
            "turn/interrupt",
            {"threadId": "thread-a", "turnId": "turn-a"},
            timeout=10.0,
        )
        self.assertEqual("CX<STATUS|PAUSED|1", sent[-1])
        self.assertTrue(bridge.bridge_paused)
        self.assertIsNone(bridge.active_turn_id)

    def test_resume_starts_continuation_turn_with_selected_effort(self):
        bridge, _ = self.make_bridge()
        bridge.tasks["RESUME"] = "continue safely"
        bridge.app = mock.Mock()
        bridge.app.request.return_value = {"turn": {"id": "turn-b"}}
        bridge.app_project = Path.cwd()
        bridge.thread_id = "thread-a"
        bridge.bridge_paused = True
        bridge.current_effort = "xhigh"

        bridge.resume_paused_task()

        method, params = bridge.app.request.call_args.args
        self.assertEqual("turn/start", method)
        self.assertEqual("thread-a", params["threadId"])
        self.assertEqual("xhigh", params["effort"])
        self.assertEqual("continue safely", params["input"][0]["text"])
        self.assertEqual("turn-b", bridge.active_turn_id)
        self.assertFalse(bridge.bridge_paused)

    def test_rotary_effort_uses_model_supported_order(self):
        bridge, sent = self.make_bridge()
        bridge.app = mock.Mock()
        bridge.thread_id = "thread-a"
        bridge.supported_efforts = ["low", "medium", "high"]
        bridge.current_effort = "medium"

        bridge.adjust_effort(1)

        bridge.app.request.assert_called_once_with(
            "thread/settings/update",
            {"threadId": "thread-a", "effort": "high"},
            timeout=10.0,
        )
        self.assertEqual("high", bridge.current_effort)
        self.assertEqual("CX<STATUS|READY|1|HIGH", sent[-1])

    def test_model_catalog_defines_available_reasoning_efforts(self):
        bridge, _ = self.make_bridge()
        bridge.app = mock.Mock()
        bridge.app.request.return_value = {
            "data": [
                {
                    "model": "test-model",
                    "isDefault": True,
                    "hidden": False,
                    "defaultReasoningEffort": "high",
                    "supportedReasoningEfforts": [
                        {"reasoningEffort": "low", "description": "fast"},
                        {"reasoningEffort": "high", "description": "deep"},
                    ],
                }
            ]
        }

        bridge.load_model_efforts()

        self.assertEqual(["low", "high"], bridge.supported_efforts)
        self.assertEqual("high", bridge.current_effort)

    def test_new_task_starts_configured_prompt(self):
        bridge, _ = self.make_bridge()
        bridge.tasks["NEW_TASK"] = "wait for instructions"
        bridge.app = mock.Mock()
        bridge.app.request.return_value = {"turn": {"id": "turn-new"}}
        bridge.app_project = Path.cwd()
        bridge.last_active_project = Path.cwd()
        bridge.thread_id = "thread-a"
        bridge.current_effort = "high"

        bridge.start_new_task()

        method, params = bridge.app.request.call_args.args
        self.assertEqual("turn/start", method)
        self.assertEqual("wait for instructions", params["input"][0]["text"])
        self.assertEqual("high", params["effort"])
        self.assertEqual("turn-new", bridge.active_turn_id)

    def test_stale_status_ack_does_not_acknowledge_new_state(self):
        bridge, _ = self.make_bridge()
        bridge.send_status("RUNNING", 2)

        bridge.handle_device_line("CX>STATUS|READY|1")

        self.assertFalse(bridge.status_acknowledged)


if __name__ == "__main__":
    unittest.main()
