# 键盘与桌面 Codex 双向互动

键盘通过主板串口发送动作，电脑端 `codex_bridge.py` 再通过本机 Codex App Server
创建线程和任务。桌面 Codex 的生命周期 Hook 通过本机 UDP 将运行、审批与完成状态
发送给桥接器，再由桥接器统一回传到 LCD 和 RGB 灯效。

## 全局安装（推荐）

在仓库根目录运行：

```powershell
powershell -ExecutionPolicy Bypass -File software\install_codex_bridge.ps1
```

安装程序会把桥接器复制到 `%USERPROFILE%\.codex\keyboard-bridge`，将用户级 Hook
安装到 `%USERPROFILE%\.codex\hooks.json`，并注册登录自启动任务
`FunModularKeyboard Codex Bridge`。用户级 Hook 会覆盖所有 Codex 工作目录；首次安装或
Hook 内容变化后，需要在 Codex 中打开 `/hooks` 审核并信任新的用户级 Hook。

Hook 会把每个会话的 `cwd` 一并发送给桥接器。LCD/RGB 会汇总所有项目的任务状态；
K10 会把最近活动的 Codex 工作目录作为目标项目。如果尚未收到任何全局 Hook，
则回退到安装时所在的仓库。串口号默认通过 `CX<PING`/`CX>PONG` 协议自动探测，因此
CH340 的 COM 编号变化后通常不需要重新配置。

仓库自身的 `.codex/hooks.json` 可以保留，方便其他用户直接使用。当前仓库同时加载
项目级与用户级 Hook 时会出现重复事件，但桥接器会按会话和轮次去重，不会重复计算任务。

## 默认按键

| 按键 | 动作 |
| --- | --- |
| K10 | 在最近活动的项目中新建一个桥接器任务 |
| K11 | 暂停当前桥接器任务 |
| K12 | 同意当前唯一一项待审批操作；没有待审批时无效 |
| K13 | 拒绝待审批操作；没有待审批时无效 |
| K14 | 在原线程中继续被暂停的桥接器任务 |

旋钮默认仍控制电脑音量：旋转调节音量、短按静音。长按旋钮约 0.7 秒可在音量模式和
Codex 思考等级模式之间切换；进入思考等级模式后，旋转会按照当前模型实际支持的等级
循环切换，短按会重新显示当前等级。LCD 状态末尾会显示 `LOW`、`MEDIUM`、`HIGH` 等等级。
运行中调整的等级从下一轮开始生效。

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

`codex_tasks.json` 中的 `NEW_TASK` 和 `RESUME` 可以修改 K10/K14 对应的提示词。桥接程序必须保持运行；退出后 LCD
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
| 任务暂停 | 黄色双闪，保持到 K14 继续或 K10 新建任务 |
| 任务完成 | 绿色常亮，随后恢复青色呼吸 |
| 发生错误 | 红色快速闪烁 |
| 桥接器断开 | 恢复键盘原有 RGB 配置 |
