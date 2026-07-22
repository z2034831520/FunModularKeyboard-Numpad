"""
连续点击按键 3 次触发电脑关机 —— 文件状态机 + GUI 取消弹窗。

原理：
  键盘每次按键事件触发本脚本（trigger_type="input"）。
  脚本用临时文件追踪按键点击次数，在时间窗口内连击 3 次后执行关机。
  关机后弹出 Windows 对话框，可一键取消。

环境变量（由上位机注入）：
  FUN_KEYBOARD_TRIGGER_TYPE   - "input" 或 "manual"
  FUN_KEYBOARD_TRIGGER_VALUE  - 按键标识，如 "K2"
  FUN_KEYBOARD_TRIGGER_TEXT   - 附加文本（可选）

用法：
  python desktop_automation_long_press_shutdown.py [--force] [--dry-run]
"""

import ctypes
import json
import os
import subprocess
import sys
import time
from pathlib import Path

# =====================================================================
# 配置
# =====================================================================

CLICK_COUNT = 3              # 触发关机需要的连击次数
CLICK_WINDOW_SEC = 2.0       # 连击时间窗口（秒），超时重置计数
SHUTDOWN_COUNTDOWN_SEC = 30  # shutdown /t 倒计时秒数

STATE_DIR = Path(os.environ.get("TEMP", os.path.join(os.path.expanduser("~"), "AppData", "Local", "Temp"))) / "FunKeyboard"
STATE_FILE = STATE_DIR / "shutdown_state.json"
# =====================================================================
# 简单的文件互斥（atomic create + delete）
# =====================================================================

def _acquire_mutex(name: str, timeout: float = 2.0) -> bool:
    """通过原子创建文件实现简单互斥锁。"""
    STATE_DIR.mkdir(parents=True, exist_ok=True)
    mutex_path = STATE_DIR / f".mutex_{name}"
    deadline = time.time() + timeout
    while True:
        try:
            fd = os.open(str(mutex_path), os.O_CREAT | os.O_EXCL | os.O_RDWR, 0o644)
            os.close(fd)
            return True
        except FileExistsError:
            # 检查持有者是否已死（文件过旧 > 10s）
            try:
                if time.time() - mutex_path.stat().st_mtime > 10.0:
                    mutex_path.unlink(missing_ok=True)
                    continue
            except OSError:
                pass
        if time.time() >= deadline:
            return False
        time.sleep(0.05)

def _release_mutex(name: str) -> None:
    mutex_path = STATE_DIR / f".mutex_{name}"
    mutex_path.unlink(missing_ok=True)

# =====================================================================
# 状态读写（原子 write-then-rename）
# =====================================================================

def _read_state() -> dict:
    if STATE_FILE.exists():
        try:
            return json.loads(STATE_FILE.read_text(encoding="utf-8"))
        except (json.JSONDecodeError, OSError):
            pass
    return {}

def _write_state(state: dict) -> None:
    STATE_DIR.mkdir(parents=True, exist_ok=True)
    tmp = STATE_FILE.with_suffix(".tmp")
    tmp.write_text(json.dumps(state, ensure_ascii=False), encoding="utf-8")
    tmp.replace(STATE_FILE)  # atomic on Windows NTFS

# =====================================================================
# GUI 取消弹窗
# =====================================================================

if sys.platform == "win32":
    _MB_YESNO = 0x00000004
    _MB_ICONWARNING = 0x00000030
    _MB_DEFBUTTON2 = 0x00000100
    _MB_SYSTEMMODAL = 0x00001000
    _IDYES = 6

    def _show_cancel_dialog(key_name: str, countdown: int) -> bool:
        message = (
            f"按键 {key_name} 连击 {CLICK_COUNT} 次触发关机！\n\n"
            f"电脑将在 {countdown} 秒后关机。\n"
            "点击「是」继续关机，点击「否」立即取消。"
        )
        result = ctypes.windll.user32.MessageBoxW(
            0,
            message,
            "FunKeyboard - 关机确认",
            _MB_YESNO | _MB_ICONWARNING | _MB_DEFBUTTON2 | _MB_SYSTEMMODAL,
        )
        return result == _IDYES
else:
    def _show_cancel_dialog(key_name: str, countdown: int) -> bool:
        print(f"[!] {key_name} 触发关机，{countdown}s 后执行")
        return True

# =====================================================================
# 系统命令
# =====================================================================

def execute_shutdown(*, countdown: int, dry_run: bool) -> tuple:
    if sys.platform != "win32":
        return False, f"平台 {sys.platform} 不支持"
    if countdown <= 0:
        cmd = ["shutdown", "/s", "/t", "0", "/f"]
        desc = "立即强制关机"
    else:
        cmd = ["shutdown", "/s", "/t", str(countdown)]
        desc = f"{countdown} 秒后关机"
    if dry_run:
        return True, f"[DRY-RUN] {' '.join(cmd)}"
    try:
        subprocess.run(cmd, check=True, shell=False)
        return True, desc
    except Exception as exc:
        return False, str(exc)

def cancel_shutdown(dry_run: bool = False) -> tuple:
    if dry_run:
        return True, "[DRY-RUN] shutdown /a"
    try:
        subprocess.run(["shutdown", "/a"], check=True, shell=False)
        return True, "已取消"
    except subprocess.CalledProcessError:
        return False, "无计划关机"
    except FileNotFoundError:
        return False, "shutdown 未找到"

# =====================================================================
# 连击计数器
# =====================================================================

def _process_click(key: str) -> bool:
    """处理一次按键点击，返回 True 表示达到连击次数应触发关机。

    在 CLICK_WINDOW_SEC 秒内连击 CLICK_COUNT 次则触发。
    超时自动重置计数。
    """
    state = _read_state()
    now = time.time()
    entry = state.get(key)

    if entry is None:
        # 第一次点击
        state[key] = {"count": 1, "first": now, "done": False}
        _write_state(state)
        print(f"[连击] {key} 第 1 次点击 (需 {CLICK_COUNT} 次 / {CLICK_WINDOW_SEC}s 内)")
        return False

    if entry.get("done"):
        return False

    elapsed = now - entry.get("first", now)

    if elapsed > CLICK_WINDOW_SEC:
        # 时间窗口已过期，重新计数
        state[key] = {"count": 1, "first": now, "done": False}
        _write_state(state)
        print(f"[连击] {key} 窗口过期 ({elapsed:.1f}s > {CLICK_WINDOW_SEC}s)，重新计数")
        return False

    # 窗口内，累加计数
    count = entry.get("count", 0) + 1
    entry["count"] = count
    state[key] = entry
    _write_state(state)
    print(f"[连击] {key} 第 {count} 次点击 (需 {CLICK_COUNT} 次)")

    if count >= CLICK_COUNT:
        entry["done"] = True
        state[key] = entry
        _write_state(state)
        print(f"[连击] {key} 达到 {CLICK_COUNT} 次连击，触发！")
        return True

    return False

# =====================================================================
# 主入口
# =====================================================================

def main() -> int:
    dry_run = "--dry-run" in sys.argv
    force = "--force" in sys.argv

    trig_type = os.environ.get("FUN_KEYBOARD_TRIGGER_TYPE", "")
    trig_value = os.environ.get("FUN_KEYBOARD_TRIGGER_VALUE", "")
    trig_text = os.environ.get("FUN_KEYBOARD_TRIGGER_TEXT", "")
    trig_pressed = os.environ.get("FUN_KEYBOARD_TRIGGER_PRESSED", "1")  # 默认按下

    print(f"trigger_type={trig_type}")
    print(f"trigger_value={trig_value}")
    print(f"trigger_pressed={trig_pressed}")
    if trig_text:
        print(f"trigger_text={trig_text}")

    # ---- 类型校验 ----
    if trig_type not in ("input", "manual"):
        print(f"跳过: 不支持类型 '{trig_type}'")
        return 0
    if not trig_value:
        print("跳过: 无按键标识")
        return 0

    # ---- 获取互斥锁 ----
    if not _acquire_mutex("shutdown", timeout=2.0):
        print("跳过: 无法获取互斥锁（并发冲突）")
        return 0

    try:
        # ---- 判定是否触发 ----
        should_trigger = False
        reason = ""

        if trig_type == "manual":
            should_trigger = True
            reason = f"manual 触发 (按键 {trig_value})"
        else:  # input
            if trig_pressed != "1":
                return 0  # 只处理按下事件
            should_trigger = _process_click(trig_value)
            if should_trigger:
                reason = f"连击 {trig_value} {CLICK_COUNT} 次"

        if not should_trigger:
            return 0

        print(f">>> {reason}，执行关机")

        # ---- 执行关机 ----
        countdown = 0 if force else SHUTDOWN_COUNTDOWN_SEC
        ok, desc = execute_shutdown(countdown=countdown, dry_run=dry_run)

        if dry_run:
            print(f"[DRY-RUN] 将弹取消对话框")
            print(f"[DRY-RUN] {desc}")
            # dry-run 也重置状态，方便反复测试
            state = _read_state()
            if trig_value in state:
                del state[trig_value]
                _write_state(state)
            return 0

        if not ok:
            print(f"关机失败: {desc}")
            return 1

        print(f"已执行 shutdown: {desc}")

        # ---- 弹窗取消 ----
        if countdown > 0:
            user_continue = _show_cancel_dialog(trig_value, countdown)
            if not user_continue:
                cancel_ok, cancel_msg = cancel_shutdown(dry_run=False)
                print(cancel_msg if cancel_ok else f"取消失败: {cancel_msg}")
                # 取消后重置状态，允许再次触发
                state = _read_state()
                if trig_value in state:
                    del state[trig_value]
                    _write_state(state)
                    print(f"[连击] {trig_value} 状态已重置，可再次触发")
                return 0 if cancel_ok else 1

        return 0

    finally:
        _release_mutex("shutdown")


if __name__ == "__main__":
    raise SystemExit(main())
