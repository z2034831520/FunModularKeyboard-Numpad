"""Replay the actual firmware connection methods with a deterministic fake WiFi."""
import pathlib
import shutil
import subprocess
import tempfile
import unittest


class WiFiReconnectTest(unittest.TestCase):
    def test_stale_failure_does_not_cancel_new_attempt(self):
        compiler = shutil.which("g++")
        if compiler is None:
            self.skipTest("g++ is required for the firmware replay")
        root = pathlib.Path(__file__).resolve().parents[1]
        source = (root / "firmware/FunModularKeyboard/src/MainTask.cpp").read_text(encoding="utf-8")
        methods = []
        for start, end in (
            ("constexpr bool shouldFinishWiFiAttempt(", "    static_assert("),
            ("bool MainTask::ConnectToWiFi(", "void MainTask::scheduleWiFiConnectAttempt("),
            ("void MainTask::processWiFiReconnect(", "void MainTask::launchWindowsTarget("),
        ):
            begin = source.index(start)
            methods.append(source[begin:source.index(end, begin)])
        harness = (root / "software/tests/wifi_reconnect/replay.cpp").read_text(encoding="utf-8")
        # Compile real firmware methods, rather than a second copy of the policy.
        replay = harness.replace("// FIRMWARE_METHODS", "\n".join(methods))
        with tempfile.TemporaryDirectory(prefix="numpad-wifi-replay-") as directory:
            binary = pathlib.Path(directory) / "replay.exe"
            built = subprocess.run(
                [compiler, "-static", "-std=c++17", "-x", "c++", "-", "-o", str(binary)],
                input=replay, text=True, capture_output=True, timeout=30,
            )
            self.assertEqual(built.returncode, 0, built.stderr)
            result = subprocess.run([str(binary)], text=True, capture_output=True, timeout=10)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)


if __name__ == "__main__":
    unittest.main()
