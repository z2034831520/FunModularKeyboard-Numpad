# 键盘与桌面 Codex 双向互动

键盘通过主板串口发送动作，电脑端 `codex_bridge.py` 再通过本机 Codex App Server
创建线程和任务。桌面 Codex 的生命周期 Hook 通过本机 UDP 将运行、审批与完成状态
发送给桥接器，再由桥接器统一回传到 LCD 和 RGB 灯效。

## 默认按键

| 按键 | 动作 |
| --- | --- |
| K10 | 分析当前项目，不主动修改文件 |
| K11 | 审查当前项目未提交的改动 |
| K12 | 同意当前唯一一项待审批操作；没有待审批时无效 |
| K13 | 拒绝待审批操作；没有待审批时中断当前任务 |

## 使用方法

1. 确认桌面 Codex 已登录，并且 PowerShell 中执行 `codex --version` 能找到 Codex CLI。
2. 将键盘设为有线模式，烧录固件，并单独上传 `data` 分区，让 `keymap1.ini` 的映射生效。
3. 安装 Python 依赖：

   ```powershell
   py -m pip install -r software\requirements-codex-bridge.txt
   ```

4. 关闭占用键盘 CDC 串口的串口监视器，然后从仓库根目录启动桥接器：

   ```powershell
   py software\codex_bridge.py --port COM50
   ```

   请把 `COM50` 替换成主板 CH340 当前对应的端口。当前固件的烧录与 Codex
   通信共用该端口，因此启动桥接器前必须关闭 PlatformIO 串口监视器；需要重新
   烧录时，也必须先退出桥接器释放串口。

5. 重新打开 Codex 桌面端中的项目。首次加载 `.codex/hooks.json` 时，在 Codex 的
   Hook 管理界面审核并信任这些项目级 Hook。Hook 未被信任时，键盘按键发起的任务
   仍可联动，但桌面端主动发起的任务不会回传状态。

桥接器默认将 Codex 工作目录限制在当前仓库，使用 `workspace-write` 沙箱、
`on-request` 审批，并关闭任务网络访问。LCD 显示 `CODEX READY` 后才接受任务键。
程序会尝试在桌面 Codex 中打开新线程；若本机未注册 `codex://` 链接，终端中的
任务和状态回传仍可正常工作，也可用 `--no-open-app` 关闭自动打开。

可先执行不连接硬件、不启动 App Server 的配置检查：

```powershell
py software\codex_bridge.py --dry-run
```

`codex_tasks.json` 可以修改 K10 对应的提示词。桥接程序必须保持运行；退出后 LCD
会在心跳超时后显示 `CODEX OFF`。

桌面端 Hook 默认向 `127.0.0.1:18765` 发送状态。若端口被其他程序占用，可用
`--hook-port` 修改桥接器监听端口，并在 `.codex/hooks.json` 的命令中为
`codex_status_hook.py` 传入相同的 `--port`。Hook 不直接打开串口，因此不会与桥接器
争用 COM50。

桌面端任务的审批灯效只用于提示。K12/K13 仍只能处理由键盘桥接器自身发起的审批；
桌面端发起的审批需要在桌面 Codex 中确认或拒绝。

## RGB 状态

| Codex 状态 | RGB 灯效 |
| --- | --- |
| 已连接并待机 | 青色呼吸 |
| 正在运行 | 蓝色流水 |
| 等待审批 | 橙色闪烁 |
| 任务完成 | 绿色常亮，随后恢复青色呼吸 |
| 发生错误 | 红色快速闪烁 |
| 桥接器断开 | 恢复键盘原有 RGB 配置 |
