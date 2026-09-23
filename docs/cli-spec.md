# snapx CLI 规范

本文档定义 `snapx` 的命令行接口契约：命令、选项、语义、输出与退出码。实现见 `src/args.cpp`、`src/main.cpp`，用户向指南见 [`../README.md`](../README.md)。界面排版风格见 [`cli-design.md`](cli-design.md)。

## 1. 概要

```
snapx scan
snapx capture --pid <id> [-o <path>] [-s <n>] [-q <n>]
snapx --help
snapx <command> --help
```

`snapx` 采用命令（操作域）模型，参考 Docker CLI：每个命令是可独立理解、独立获取帮助的操作域，命令的选项只属于该域，不在命令之间复用。当前有两个操作域：`scan` 列出可截图窗口，`capture` 截取单个窗口。`capture` 每次调用只处理一个目标窗口，目标由进程 ID (`--pid`) 唯一确定，不存在按标题匹配带来的同名多进程歧义。

## 2. 词法规则

- 参数以 `wmain` + `CommandLineToArgvW` 解析，遵循 Windows 命令行引用规则；成对的首尾双引号会被剥离（见 `StripQuotes`）。
- 第一个裸词必须是命令：`scan` 或 `capture`，大小写不敏感。选项必须出现在命令之后。
- `--help` 可出现在任意位置：顶层 `snapx --help` 显示总帮助；`snapx <command> --help` 显示该命令的帮助。命令上下文中的 `--help` 覆盖该命令的其余校验。
- 选项名大小写不敏感（`ToLower` 后比较），例如 `--PID`、`-O` 均可。
- 长选项支持 `--key=value` 内联形式；`-o` 等短选项不支持 `=`。
- 不带 `=` 的长选项从下一个参数取值。
- 未知选项一律报错，不做前缀匹配或位置参数推断；命令不匹配的选项同样按未知选项报错，命名空间相互隔离。

## 3. 命令与选项

### 3.1 Commands

| 命令 | 说明 |
|---|---|
| `scan` | 列出可截图窗口后退出。不接受任何截图选项 |
| `capture` | 截取一个窗口，需 `--pid` |
| `--help` | 顶层或命令级帮助 |

- 命令必须显式给出，不可省略。`snapx --pid 1` 报 `unknown option '--pid'`（选项先于命令出现）。
- 无参数、无命令时报 `no command given; use scan or capture`（退出码 2）。
- 给 `scan` 传截图选项报 `unknown option '<opt>'`（退出码 2），体现选项命名空间隔离。
- 重复命令（如 `snapx scan capture`）报 `unknown option 'capture'`（退出码 2）。

### 3.2 capture options

| 选项 | 取值 | 默认 | 说明 |
|---|---|---|---|
| `--pid <id>` | 十进制 `uint32` | 无 | 截取该进程 ID 的窗口。必填 |
| `-o`, `--output <path>` | 路径或 `-` | 自动命名 | 输出目标。`-` 表示写 stdout；无扩展名视为 `.png`；省略时自动命名为 `<process>_<pid>_<timestamp>.<ext>` |
| `-s`, `--scale <value>` | 浮点，`(0.0, 5.0]` | `1.0` | 输出尺寸倍数，经 WIC 高质量插值 |
| `-q`, `--quality <1-100>` | 整数 | `90` | JPEG 质量；仅对 JPEG 生效 |

取值校验：

- `--scale` 必须满足 `> 0.0` 且 `<= 5.0`，否则报错。
- `--quality` 必须为 `1..100`，否则报错。
- `--scale` 导致输出宽或高为 0 像素时报错（退出码 1，编码阶段）。
- 缺少 `--pid` 时报 `--pid is required`（退出码 2）。

## 4. 输出格式

格式由 `-o` 的扩展名决定，大小写不敏感。

| 扩展名 | 格式 | 说明 |
|---|---|---|
| `.png` | PNG | 无损；无扩展名或省略 `-o` 时默认 |
| `.jpg` / `.jpeg` | JPEG | 有损，受 `-q` 影响 |
| `.bmp` | BMP | 未压缩 |

- 扩展名识别以最后一个 `.` 为准，且该 `.` 必须位于最后一个路径分隔符之后（`ResolveFormatFromOutput`）。
- 其他扩展名在参数校验阶段直接报错 `unsupported output extension '...'; use png, jpeg, or bmp`（退出码 2）。
- `-o -`（stdout）时无扩展名可推断，默认 PNG。

## 5. 行为语义

### 5.1 窗口枚举与匹配

- `scan` 与 `capture` 共用同一套枚举规则：仅包含有非空标题、非所有者窗口、无 `WS_EX_TOOLWINDOW`、且可见或已最小化的顶层窗口。
- `scan` 输出三列 `PID`、`PROCESS NAME`、`TITLE`；最小化窗口在标题后追加 ` [minimized]`。行宽固定：PID 左对齐 7 字符，进程名左对齐截断至 28 字符，其后为标题。
- `capture --pid` 匹配同 PID 的窗口：优先返回非最小化窗口；若仅有最小化窗口，则返回其中第一个作为回退。
- 匹配不到时报 `error: no capturable window for pid <id>`（退出码 1）。

### 5.2 最小化窗口

目标若最小化，截图前先 `SW_RESTORE`，等待恢复（最多 2s，另加 100ms 稳定延迟），截图后重新 `SW_MINIMIZE`。恢复期间窗口会短暂可见。

### 5.3 截图

- 使用 `Windows.Graphics.Capture`，`Direct3D11CaptureFramePool::CreateFreeThreaded` 取帧，超时 5000ms。
- 帧经 `IDirect3DDxgiInterfaceAccess` 转为 `ID3D11Texture2D`，复制到 staging 纹理后映射读取。
- 像素为 32bpp BGRA，与 WIC `GUID_WICPixelFormat32bppBGRA` 直接对应。
- 不支持的设备报 `Windows.Graphics.Capture is not supported on this device.`；超时报 `Timed out waiting for a capture frame.`（均退出码 1）。

### 5.4 编码与命名

- 编码走 WIC。`--scale != 1.0` 时用 `WICBitmapInterpolationModeFant` 缩放，再由格式转换器写入目标帧。
- 成功写文件后，向 stdout 打印 `saved <W>x<H> (<FORMAT>) to <path>`，其中宽高为缩放后的尺寸。
- 自动命名规则：取进程名去扩展名，替换 `<>:"/\|?*` 及控制字符为 `_`（空则 `window`），拼接 `_<pid>_<YYYYMMDD-HHMMSS>` 与格式扩展名。

### 5.5 stdout 模式

- `-o -` 时，先编码到系统临时文件，再以分块 `WriteFile` 复制到 stdout，随后删除临时文件。stdout 仅含图像字节。
- 所有提示、错误一律写 stderr。非控制台句柄下文本以 UTF-8 字节输出；控制台句柄下用 `WriteConsoleW`。

## 6. 退出码

| 退出码 | 含义 |
|---|---|
| `0` | 成功（含 `--help`、`scan`、`-o -` 成功） |
| `1` | 未匹配到窗口，或截图/编码/读写失败 |
| `2` | 参数错误：未知选项、缺值、取值非法、未知扩展名、缺少 `--pid`、缺少命令、选项先于命令 |

参数错误发生时，向 stderr 打印 `error: <message>` 后紧跟 usage，再以退出码 2 结束。

## 7. 错误消息（稳定契约）

以下消息文本属于接口的一部分，脚本可依赖：

| 场景 | 消息 |
|---|---|
| 未知选项（含跨命令选项、重复命令） | `error: unknown option '<arg>'` |
| 缺少取值 | `error: --pid requires a value`（及各选项对应文本） |
| PID 非法 | `error: invalid pid '<value>'` |
| scale 越界 | `error: --scale must be in (0.0, 5.0], got <value>` |
| quality 越界 | `error: --quality must be 1-100, got <value>` |
| 缺少 `--pid` | `error: --pid is required` |
| 缺少命令 | `error: no command given; use scan or capture` |
| 扩展名不支持 | `error: unsupported output extension '<ext>'; use png, jpeg, or bmp` |
| 无窗口 | `error: no capturable window for pid <id>` |
| 缩放为零尺寸 | `error: --scale produces a zero-sized image` |
| 无法写入 | `error: Cannot write '<path>'` |

注：帮助、扫描列表与错误消息当前均为英文；usage 与 help 文本见 `src/args.cpp` 的 `PrintUsage` / `PrintHelp` / `PrintScanHelp` / `PrintCaptureHelp`。

## 8. 保证与非保证

保证：

- 同一 `--pid` 在枚举规则不变时映射到同一窗口。
- `-o -` 的 stdout 可安全重定向到文件或管道。
- `scan` 与 `capture` 的错误与退出码符合第 6、7 节。
- 选项按命令隔离：`scan` 只接受 `--help`，截图选项仅属于 `capture`。
- `snapx --help`、`snapx scan --help`、`snapx capture --help` 分别输出总帮助与命令级帮助，退出码 0。

非保证：

- 窗口标题、进程名等文本内容属于运行时数据，格式固定但内容不保证。
- 截图期间系统会在目标窗口周围绘制黄色边框，这是 `Windows.Graphics.Capture` 的固有行为，无法关闭。
- 受保护内容（如 DRM）可能取帧失败，代理返回超时错误。
