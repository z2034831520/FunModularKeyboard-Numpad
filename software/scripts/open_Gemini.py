import os
import sys
import webbrowser


def main():
    # 防止松开按键时再次打开网页
    if os.environ.get("FUN_KEYBOARD_TRIGGER_PRESSED", "1") != "1":
        return 0

    if len(sys.argv) < 2:
        print("没有配置网页地址")
        return 1

    # 每个附加参数对应一个网址
    for url in sys.argv[1:]:
        url = url.strip()

        if not url.startswith(("http://", "https://")):
            url = "https://" + url

        print(f"正在打开：{url}")
        webbrowser.open(url, new=2)

    return 0


if __name__ == "__main__":
    raise SystemExit(main())