import os
import subprocess
import sys
from pathlib import Path


def parse_app_paths() -> list[str]:
    raw = os.environ.get("FUN_KEYBOARD_APP_PATHS", "")
    return [entry.strip() for entry in raw.split("||") if entry.strip()]


def read_trigger_context() -> dict[str, str]:
    return {
        "type": os.environ.get("FUN_KEYBOARD_TRIGGER_TYPE", ""),
        "value": os.environ.get("FUN_KEYBOARD_TRIGGER_VALUE", ""),
        "text": os.environ.get("FUN_KEYBOARD_TRIGGER_TEXT", ""),
    }


def launch_program(program: str) -> tuple[bool, str]:
    path = Path(program)
    if not path.exists():
        return False, f"skip missing path: {program}"

    try:
        subprocess.Popen([str(path)], shell=False)
        return True, f"launched: {program}"
    except Exception as exc:  # noqa: BLE001
        return False, f"failed to launch {program}: {exc}"


def main() -> int:
    trigger = read_trigger_context()
    app_paths = parse_app_paths()
    extra_args = sys.argv[1:]

    print(f"trigger_type={trigger['type']}")
    print(f"trigger_value={trigger['value']}")
    if trigger["text"]:
        print(f"trigger_text={trigger['text']}")

    if extra_args:
        print("extra_args=" + " | ".join(extra_args))

    if not app_paths:
        print("no app paths configured in FUN_KEYBOARD_APP_PATHS")
        return 1

    failures = []
    for app_path in app_paths:
        ok, message = launch_program(app_path)
        print(message)
        if not ok:
            failures.append(message)

    return 0 if not failures else 2


if __name__ == "__main__":
    raise SystemExit(main())
