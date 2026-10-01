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
`FunModularKeyboard Codex Bridge`。计划任务会直接托管桥接进程，并在异常退出后每分钟
自动重试。用户级 Hook 会覆盖所有 Codex 工作目录；首次安装或
Hook 内容变化后，需要在 Codex 中打开 `/hooks` 审核并信任新的用户级 Hook。

如果登录后桥接器没有运行，可直接双击仓库根目录的
`start_codex_keyboard_bridge.cmd`。它会复用同一个计划任务，避免重复启动，并在失败时显示
错误。运行日志保存在 `%USERPROFILE%\.codex\keyboard-bridge\logs\codex_bridge.log`。

Hook 会把每个会话的 `cwd` 一并发送给桥接器。LCD/RGB 会汇总所有项目的任务状态。
K10 会通过 Windows 注册的 `codex://threads/new` 链接打开桌面 Codex 的新任务页；
串口号默认通过 `CX<PING`/`CX>PONG` 协议自动探测，因此
CH340 的 COM 编号变化后通常不需要重新配置。

仓库自身的 `.codex/hooks.json` 可以保留，方便其他用户直接使用。当前仓库同时加载
项目级与用户级 Hook 时会出现重复事件，但桥接器会按会话和轮次去重，不会重复计算任务。

## 默认按键

| 按键 | 动作 |
| --- | --- |
| K10 | 唤起桌面 Codex，并打开一个用户可见的新任务页 |
| K11 | 暂停当前桥接器任务 |
| K12 | 同意当前唯一一项待审批操作；没有待审批时无效 |
| K13 | 拒绝待审批操作；没有待审批时无效 |
| K14 | 在原线程中继续被暂停的桥接器任务 |

旋钮默认仍控制电脑音量：旋转调节音量、短按静音。长按旋钮约 0.7 秒可在音量模式和
Codex 思考等级模式之间切换；进入思考等级模式后，旋转会按照当前模型实际支持的等级
循环切换，短按旋钮也会切换到下一个等级。没有 `E:` 标记时，旋钮处于音量模式。

旋钮通过 Codex App Server 保存用户级默认 `model_reasoning_effort`，并读取当前工作目录下
的有效本地配置。这只能证明磁盘配置已更新，不能证明桌面 GUI 的等级选择器或正在运行的
任务已经改变。项目级配置等更高优先级设置可能覆盖用户级默认值。

K10 打开新任务页后，仅在没有已知运行任务时尝试通过 Windows UI Automation 的语义控件
选择等级，并重新读取模型与等级进行确认。不会点击固定坐标、自动提交任务或操作审批。
找不到唯一的模型和等级控件时会放弃自动控制，保留任务状态及 RGB 联动。
启动、旋钮选择及周期检查只读取 GUI，不会自动修改已有任务的选择器。

思考等级模式下，`E:?` 表示桌面端等级尚未确认；只有处于 READY 且当前 GUI 选择器的模型
和等级均匹配时，才显示 `E:HIGH` 等标记。此标记仍不证明任务运行时实际使用的等级。
运行中的 GUI 任务不提供可核验的等级字段，因此显示 `E:?`，LCD/RGB 继续汇总任务状态。
若当前桌面版本无法识别控件，请手动在桌面新任务页选择等级，不要仅依据磁盘配置判断生效。

上述 `E:?` 语义需要配套固件：旧固件在状态消息省略等级时会保留旧等级，并导致桥接器
收不到匹配回执而重发。更新桥接脚本后还需烧录修正后的固件；没有烧录时请以电脑端报告
为准，不要把 LCD 上残留的等级当作确认结果。

可从仓库根目录执行只读兼容性检查（不打开串口、不修改桌面）：

```powershell
py software\codex_bridge.py --desktop-probe --probe-model gpt-5.6-sol --probe-effort high
```

查看旋钮选择、本地有效配置和 GUI 确认结果：

```powershell
powershell -ExecutionPolicy Bypass -File software\show_codex_effort.ps1
```

加 `-Watch` 可持续查看。报告保存于 `%USERPROFILE%\.codex\keyboard-bridge\effort-state.json`。
`saved_host_hint` 只是桌面保存的主机提示，不是当前任务执行位置的证据；
`RuntimeVerified` 当前始终为 false。启动桥接器时加 `--no-desktop-sync` 可关闭 GUI 检测与尝试，
不影响 Hook 状态联动。

开发回归检查：`py -B -m unittest software/test_codex_bridge.py software/test_codex_desktop.py`。
串口固件回归测试位于 `software/tests/codex_serial`，使用模拟串口编译实际的
`CodexSerialBridge.cpp`，验证旧等级清除、回执、心跳和断连语义，不需要连接硬件。

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

桥接器通过 App Server 发起的分析、审查等任务默认限制在当前仓库，使用
`workspace-write` 沙箱、`on-request` 审批，并关闭任务网络访问。LCD 显示
`CODEX READY` 后才接受任务键。K10 不再创建后台 App Server 任务，而是直接打开桌面
Codex 的新任务页；输入任务并发送后，Hook 才会让 LCD/RGB 进入运行状态。若本机未注册
`codex://` 链接，K10 会显示错误状态。`--no-open-app` 只关闭桥接器线程的自动展示，
不会禁用 K10 的桌面新任务入口。

可先执行不连接硬件、不启动 App Server 的配置检查：

```powershell
py software\codex_bridge.py --dry-run
```

`codex_tasks.json` 中的 `RESUME` 可以修改 K14 对应的提示词。K10 只打开桌面新任务页，
不会自动填入或提交提示词。桥接程序必须保持运行；退出后 LCD
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
