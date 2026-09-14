import unittest
import json
from pathlib import Path
from types import SimpleNamespace

from software.codex_bridge import CodexKeyboardBridge


class CodexHookStateTests(unittest.TestCase):
    def make_bridge(self, *, stale_timeout=600.0):
        bridge = CodexKeyboardBridge(SimpleNamespace(hook_stale_timeout=stale_timeout), {})
        sent = []
        bridge.send_line = sent.append
        return bridge, sent

    @staticmethod
    def event(name, session_id="session-a", turn_id=None):
        payload = {
            "version": 1,
            "hook_event_name": name,
            "session_id": session_id,
        }
        if turn_id is not None:
            payload["turn_id"] = turn_id
        return payload

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

    def test_stale_status_ack_does_not_acknowledge_new_state(self):
        bridge, _ = self.make_bridge()
        bridge.send_status("RUNNING", 2)

        bridge.handle_device_line("CX>STATUS|READY|1")

        self.assertFalse(bridge.status_acknowledged)


if __name__ == "__main__":
    unittest.main()
