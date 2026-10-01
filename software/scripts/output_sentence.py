import ctypes
import os
import sys
import time
import tkinter as tk


VK_CONTROL = 0x11
VK_V = 0x56
KEYEVENTF_KEYUP = 0x0002


def paste_text(text: str) -> None:
    # 将文字写入 Windows 剪贴板
    root = tk.Tk()
    root.withdraw()
    root.clipboard_clear()
    root.clipboard_append(text)
    root.update()

    time.sleep(0.05)

    # 模拟 Ctrl+V
    user32 = ctypes.windll.user32

    user32.keybd_event(VK_CONTROL, 0, 0, 0)
    user32.keybd_event(VK_V, 0, 0, 0)
    user32.keybd_event(VK_V, 0, KEYEVENTF_KEYUP, 0)
    user32.keybd_event(VK_CONTROL, 0, KEYEVENTF_KEYUP, 0)

    time.sleep(0.2)
    root.destroy()


def main() -> int:
    # 防止松开按键时再次触发
    if os.environ.get("FUN_KEYBOARD_TRIGGER_PRESSED", "1") != "1":
        return 0

    # 附加参数为空时使用默认文字
    if len(sys.argv) > 1:
        text = "\n".join(sys.argv[1:])
    else:
        text = "你好，这是一段由数字小键盘自动输入的文字。"

    paste_text(text)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())