import json
import subprocess
import tempfile
import unittest
from pathlib import Path
from types import SimpleNamespace
from unittest import mock

from software import codex_desktop


class DesktopProbeTests(unittest.TestCase):
    def test_saved_remote_host_is_only_a_hint(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "state.json"
            path.write_text(json.dumps({"selected-remote-host-id": "remote-control:example"}), encoding="utf-8")
            self.assertEqual("remote-control", codex_desktop.saved_host_hint(path))

    def test_malformed_state_does_not_dump_private_content(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "state.json"
            path.write_text("private invalid JSON", encoding="utf-8")
            self.assertEqual("unknown", codex_desktop.saved_host_hint(path))

    def probe(self, response):
        with mock.patch.object(codex_desktop.os, "name", "nt"), mock.patch.object(
            codex_desktop.subprocess, "run", return_value=response
        ):
            return codex_desktop.inspect_picker("gpt-5.6-sol", "high")

    def test_matching_picker_is_not_runtime_confirmation(self):
        response = SimpleNamespace(returncode=0, stdout=json.dumps({
            "status": "confirmed", "reason": "picker_readback_matches",
            "confirmed_model": "gpt-5.6-sol", "confirmed_effort": "high",
        }))
        result = self.probe(response)
        self.assertEqual("confirmed", result["status"])
        self.assertFalse(result["runtime_verified"])

    def test_wrong_model_cannot_confirm_selection(self):
        response = SimpleNamespace(returncode=0, stdout=json.dumps({
            "status": "confirmed", "confirmed_model": "different",
            "confirmed_effort": "high",
        }))
        result = self.probe(response)
        self.assertEqual("unverified", result["status"])
        self.assertIsNone(result["confirmed_effort"])

    def test_timeout_is_unverified(self):
        with mock.patch.object(codex_desktop.os, "name", "nt"), mock.patch.object(
            codex_desktop.subprocess, "run", side_effect=subprocess.TimeoutExpired("probe", 8)
        ):
            result = codex_desktop.inspect_picker("gpt-5.6-sol", "high", sync=True)
        self.assertEqual("unsupported", result["status"])
        self.assertIsNone(result["confirmed_effort"])

    def test_invalid_output_is_unverified(self):
        result = self.probe(SimpleNamespace(returncode=0, stdout="invalid JSON"))
        self.assertEqual("unsupported", result["status"])
        self.assertIsNone(result["confirmed_effort"])
