# snapx

Windows 命令行截图工具。基于 `Windows.Graphics.Capture` API，通过进程 ID 截取指定应用窗口，支持 PNG / JPEG / BMP 输出。

需求规格见 [`docs/requirements.md`](docs/requirements.md)，完整命令行约定见 [`docs/cli-spec.md`](docs/cli-spec.md)，命令行界面设计风格见 [`docs/cli-design.md`](docs/cli-design.md)，内部模块划分见 [`docs/architecture.md`](docs/architecture.md)。

## 特性

- `snapx scan` 列出当前可截图的窗口（PID、进程名、窗口标题）
- `snapx capture --pid <id>` 精确截取单个窗口，不存在同名多进程歧义
- `snapx screen` 截取主显示器全屏
- `-c` / `--clipboard` 把截图直接复制到剪贴板，免落盘
- 输出 PNG、JPEG、BMP，格式由 `-o` 扩展名决定
- `--scale` 无损放大/缩小输出尺寸
- JPEG 可指定质量
- `-o -` 将图像写入 stdout，便于管道处理
- 命令与选项分域：`snapx --help` 与 `snapx <command> --help`
- 可执行文件图标由 RC 资源编译进 exe
- 纯 Windows SDK 实现，无第三方依赖

## 系统要求

| 项目 | 要求 |
|---|---|
| 操作系统 | Windows 10 1803 (build 17134) 或更高 |
| 编译器 | MSVC (Visual Studio 2022 或 Build Tools) |
| CMake | 3.21 或更高 |
| 生成器 | Ninja（`cmake --preset` 需要） |
| SDK | Windows 10/11 SDK（含 C++/WinRT 头文件） |

> `Windows.Graphics.Capture` 仅在 Windows 桌面设备上受支持。若不支持，`snapx` 会在截图时报错。

## 编译

必须在已加载 MSVC 环境的命令行中编译。可使用 **Developer Command Prompt for VS 2022**，或先手动加载环境：

```bat
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"
```

### Debug 构建

```bat
cmake --preset x64-debug
cmake --build --preset x64-debug
```

### Release 构建

```bat
cmake --preset x64-release
cmake --build --preset x64-release
```

所有构建产物位于 `build/` 目录下：

| 预设 | 输出路径 |
|---|---|
| `x64-debug` | `build\x64-debug\snapx.exe` |
| `x64-release` | `build\x64-release\snapx.exe` |

exe 图标由 `res/snapx.ico` 经 `res/snapx.rc` 编译进可执行文件；更换图标只需替换 `res/snapx.ico`。

### 运行测试

需先完成一次 Debug 配置与构建（`ctest --preset` 依赖对应预设的构建目录）：

```bat
cmake --preset x64-debug
cmake --build --preset x64-debug
ctest --preset x64-debug
```

## 使用

```
snapx scan
snapx capture --pid <id> [-o <path>] [-c] [-s <n>] [-q <n>]
snapx screen [-o <path>] [-c] [-s <n>] [-q <n>]
```

### 列出窗口（scan）

```bat
build\x64-release\snapx.exe scan
```

输出示例：

```
PID      PROCESS NAME                  TITLE
-------  ----------------------------  -----
21604    WindowsTerminal.exe           Windows PowerShell
19740    chrome.exe                    Example Page - Google Chrome
4608     Code.exe                      main.cpp - Visual Studio Code
```

无标题窗口、工具窗口会被过滤。最小化窗口会列出并标注 `[minimized]`，且可被正常截图。

### 截图（capture）

先 `scan` 取得目标 PID，再截图：

```bat
build\x64-release\snapx.exe capture --pid 4608 -o shot.png
```

省略 `-o` 时自动命名，写入当前工作目录：

```bat
build\x64-release\snapx.exe capture --pid 4608
# saved 1734x927 (PNG) to Code_4608_20260923-094533.png
```

指定格式（由扩展名决定）与 JPEG 质量：

```bat
build\x64-release\snapx.exe capture --pid 4608 -o shot.jpg -q 80
```

缩放输出尺寸（默认 1.0，范围 (0.0, 5.0]）：

```bat
build\x64-release\snapx.exe capture --pid 4608 -o half.png -s 0.5
build\x64-release\snapx.exe capture --pid 4608 -o dbl.png -s 2.0
```

写入 stdout 以接入管道：

```bat
build\x64-release\snapx.exe capture --pid 4608 -o - > shot.png
build\x64-release\snapx.exe capture --pid 4608 -o - | magick png:- shot.webp
```

### 全屏截图（screen）

`snapx screen` 截取主显示器（`MONITOR_DEFAULTTOPRIMARY`）整屏，选项与 `capture` 相同，只是不需要 `--pid`：

```bat
build\x64-release\snapx.exe screen
# saved 3840x2160 (PNG) to screen_20260924-101500.png
build\x64-release\snapx.exe screen -o shot.png
build\x64-release\snapx.exe screen -o - > shot.png
```

### 复制到剪贴板（-c）

`-c` / `--clipboard` 把截图直接写入剪贴板，不落盘，可用于 `capture` 与 `screen`。剪贴板同时提供 `CF_DIB` 与 `PNG` 两种格式，兼容画图、Office 与浏览器等应用。`-c` 与 `-o` 互斥；`-s` 同样作用于剪贴板内容。

```bat
build\x64-release\snapx.exe screen -c
# copied 3840x2160 to clipboard
build\x64-release\snapx.exe capture --pid 4608 -c
build\x64-release\snapx.exe screen -c -s 0.5
```

## 命令行界面

`snapx` 采用命令（操作域）模型，参考 Docker CLI：命令是可独立理解、独立获取帮助的操作域，命令的选项只属于该域。当前有三个命令：`scan`、`capture` 与 `screen`。

顶层帮助（`snapx --help`）：

```
snapx - capture a Windows application window or the primary display to an image

usage:
  snapx scan [--help]
  snapx capture --pid <id> [-o <path>] [-c] [-s <n>] [-q <n>]
  snapx screen [-o <path>] [-c] [-s <n>] [-q <n>]

commands:
  scan      list capturable windows
  capture   capture one window by process id
  screen    capture the primary display

run 'snapx <command> --help' for details
```

每个命令有独立帮助：

```bat
build\x64-release\snapx.exe scan --help
build\x64-release\snapx.exe capture --help
build\x64-release\snapx.exe screen --help
```

| 命令 | 说明 |
|---|---|
| `scan` | 列出可截图窗口；不接受截图选项 |
| `capture` | 截取一个窗口，需 `--pid` |
| `screen` | 截取主显示器全屏 |

命令必须显式给出：`snapx --pid 4608` 会因选项先于命令而报错，应写 `snapx capture --pid 4608`。

### capture 选项

| 选项 | 说明 |
|---|---|
| `--pid <id>` | 截取指定进程 ID 的窗口（必填） |
| `-o`, `--output <path>` | 输出路径；`-` 表示写 stdout；无扩展名补 `.png`；省略时自动命名 `<process>_<pid>_<timestamp>.<ext>` |
| `-c`, `--clipboard` | 复制到剪贴板而非写文件；与 `-o` 互斥 |
| `-s`, `--scale <value>` | 输出尺寸倍数，默认 `1.0`，范围 `(0.0, 5.0]` |
| `-q`, `--quality <1-100>` | JPEG 质量，默认 `90`（仅 jpeg 生效） |

`screen` 选项与 `capture` 相同，但没有 `--pid`，自动命名为 `screen_<timestamp>.<ext>`。

支持 `--key=value` 形式，例如 `--pid=4608 --output=shot.png`。

## 支持的输出格式

| 扩展名 | 格式 | 说明 |
|---|---|---|
| `.png` | PNG | 无损；无扩展名时默认 |
| `.jpg` / `.jpeg` | JPEG | 有损；受 `-q` 影响 |
| `.bmp` | BMP | 未压缩 |

其他格式（含 webp）不在支持范围内，会以退出码 2 报错。需要时通过管道转换（PNG 为无损，适合作为中间格式）：

```bat
build\x64-release\snapx.exe capture --pid 4608 -o - | magick png:- shot.webp
```

## 退出码

| 退出码 | 含义 |
|---|---|
| `0` | 成功 |
| `1` | 未匹配到窗口，或截图/编码失败 |
| `2` | 参数错误（未知选项、未知扩展名、取值越界、缺少命令、选项先于命令等） |

## 行为说明

- 每次 `capture` 只截取一个窗口（由 `--pid` 指定）；`screen` 截取主显示器整屏。
- 支持最小化窗口：目标若处于最小化状态，会先恢复、截图、再恢复为最小化，窗口状态保持不变。恢复期间窗口会短暂可见。
- 截图期间目标窗口周围会显示系统绘制的黄色边框，这是 `Windows.Graphics.Capture` 的固有行为，无法关闭（`screen` 全屏截图无此边框）。
- `-c` 时只写剪贴板，不产生文件；`-c` 与 `-o` 同时给出会以退出码 2 报错。
- `-o -` 时 stdout 只包含图像字节，所有提示与错误一律写入 stderr。
- 编码使用 WIC；`--scale` 通过 WIC 高质量插值完成。

## 故障排查

**`cmake` 找不到编译器 / `cl.exe` 未找到**

当前 shell 未加载 MSVC 环境。请使用 Developer Command Prompt，或先执行上文 `vcvars64.bat`。

**`error: no capturable window for pid <id>`**

该 PID 没有顶层窗口。用 `snapx scan` 确认 PID 是否正确、窗口是否已关闭。

**`error: Windows.Graphics.Capture is not supported on this device.`**

当前系统不支持该 API（需要 Windows 10 1803+ 桌面版）。

**`error: Timed out waiting for a capture frame.`**

目标窗口在超时时间内未产生新帧。部分受保护内容（如 DRM 窗口）可能无法捕获。

**输出被重定向时中文乱码**

`snapx` 写入的是 UTF-8 字节。若在非 UTF-8 代码页的终端（如旧版 `cmd`）查看，可先执行 `chcp 65001`。
