# WiFi reconnect regression

Run from the repository root:

```powershell
py -B -m unittest software/test_wifi_reconnect.py
```

Requires `g++` on PATH. The test extracts the actual connection and retry methods
from `MainTask.cpp`, compiles them with fake WiFi and clock dependencies, and runs
the resulting executable in a temporary directory.

Coverage: stale CONNECT_FAILED/NO_SSID/NO_SHIELD results do not cancel a new scan;
the 10-second timeout and 5-second retry delay remain bounded; a restored network
can connect; disabled WiFi and BLE mode do not connect; deliberate retry cleanup
does not overwrite a useful disconnect reason; event registration is not repeated.

This is a deterministic firmware logic test, not proof of real hotspot connectivity.
After flashing, verify startup and restoring a previously unavailable hotspot on
the device. No SSID, password, or device data partition is modified by the test.
