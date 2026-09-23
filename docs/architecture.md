# snapx 架构

本文档描述 `snapx` 的内部模块划分、依赖关系、运行时数据流与关键技术决策。命令行接口契约见 [`cli-spec.md`](cli-spec.md)，界面排版风格见 [`cli-design.md`](cli-design.md)，用户向指南见 [`../README.md`](../README.md)。

## 1. 设计目标与约束

- **单一职责的命令行工具**：一次调用完成一次截图并退出，无守护进程、无配置、无状态复用。
- **仅依赖 Windows SDK**：C++23 / MSVC，截图用 `Windows.Graphics.Capture`，编码用 WIC，剪贴板用 Win32，无第三方库。
- **清晰的模块边界**：CLI 解析、窗口枚举、取帧、编码、剪贴板各自独立，通过最小接口通信。
- **可测试的参数层**：参数校验与截图/编码解耦，使 `--help`、错误路径可以在无 GUI 的 CI 中通过退出码断言。

## 2. 模块总览

所有源码位于 `src/`，一个模块对应一对 `.h` / `.cpp`（`main.cpp` 例外）：

| 模块 | 文件 | 职责 |
|---|---|---|
| 入口 / 编排 | `main.cpp` | 解析结果落地：命令分派、窗口匹配、取帧、输出与日志、退出码 |
| 参数 | `args.h` / `args.cpp` | 命令行解析、选项校验、帮助文本；定义 `Options`、`Command`、`ImageFormat` |
| 窗口枚举 | `window_enum.h` / `window_enum.cpp` | 枚举顶层可截图窗口、按 PID 匹配、查询进程名 |
| 取帧 | `capture.h` / `capture.cpp` | 通过 WGC 抓取窗口或显示器的一帧，转成 CPU 侧 BGRA 像素缓冲 |
| 编码 | `encode.h` / `encode.cpp` | WIC 编码到文件或内存、`--scale` 缩放、BGRA 缩放供剪贴板使用 |
| 剪贴板 | `clipboard.h` / `clipboard.cpp` | 把图像以 `CF_DIB` + 注册 `PNG` 写入系统剪贴板 |

## 3. 依赖关系

模块间为单向依赖，无环：

```
main.cpp
├── args.h         (无内部依赖)
├── window_enum.h  (无内部依赖)
├── capture.h      (无内部依赖)
├── encode.h       → args.h, capture.h
└── clipboard.h    → capture.h   (clipboard.cpp 另用 encode.h)
```

两点值得注意：

- `encode` 依赖 `args.h` 只是为了 `ImageFormat` 枚举；`ImageFormat` 语义上属于编码域，目前寄居在参数模块。若要进一步解耦，可将其移入独立头文件。
- `clipboard` 的实现依赖 `encode`：剪贴板的 `PNG` 格式复用 `EncodeToMemory`，缩放复用 `ScaleToBgra`。它本身不实现任何编码逻辑。

### 目标平台数据类型

| 类型 | 定义处 | 说明 |
|---|---|---|
| `Command` | `args.h` | 命令枚举，含 `Help` / `ScanHelp` / `CaptureHelp` / `ScreenHelp` 等帮助态 |
| `Options` | `args.h` | 解析结果：命令、pid、输出路径、stdout / clipboard 开关、scale、jpeg quality |
| `ImageFormat` | `args.h` | `Png` / `Jpeg` / `Bmp`，由输出扩展名解析 |
| `WindowInfo` | `window_enum.h` | `hwnd`、`processId`、`processName`、`title`、`minimized` |
| `CaptureResult` | `capture.h` | `width`、`height`、`rowPitch`、`pixels`（紧凑 32bpp BGRA，`rowPitch == width * 4`） |

`CaptureResult::pixels` 始终使用紧凑行距（`width * 4`），与 WGC staging 纹理可能更宽的行距解耦——`capture.cpp` 逐行拷贝以完成这一归一化。

## 4. 运行时数据流

进程单线程、同步、一次性：`wmain` → 解析 → 采集 → 输出 → `return`。没有后台线程或常驻资源。

### 4.1 scan

```
wmain → ParseCommandLine(Command::Scan) → RunList
      → EnumerateWindows() → 格式化三列表格 → WriteStdOut
```

`scan` 不接触 D3D/WIC，只做窗口枚举与文本输出。

### 4.2 capture → 文件

```
ParseCommandLine(Command::Capture)
→ FindWindowByPid(pid)                  // 匹配不到：退出码 1
→ [若最小化] ShowWindow(SW_RESTORE) + 等待 → CaptureWindow
→ [若最小化] ShowWindow(SW_MINIMIZE)     // 恢复原状态
→ ResolveFormatFromOutput(-o)            // 决定 ImageFormat
→ EncodeToFile(...)                      // scale / jpegQuality 在此生效
→ WriteStdOut("saved WxH (FORMAT) to PATH")
```

### 4.3 capture → stdout

`-o -` 时不直接写句柄，而是先 `EncodeToFile` 到系统临时文件，再以 64 KiB 分块 `WriteFile` 复制到 stdout，最后删除临时文件（`main.cpp: WriteToStdout`）。这样 stdout 上只有图像字节，提示与错误走 stderr。

### 4.4 capture / screen → 剪贴板

```
if options.clipboard:
  CopyImageToClipboard(image, scale)
    → ScaleToBgra(scale)          // 无缩放时直接拷贝
    → EncodeToMemory(Png)         // 复用编码核心
    → OpenClipboard(带重试) → EmptyClipboard
    → SetClipboardData(CF_DIB)    // 32bpp 自下而上
    → SetClipboardData("PNG")     // 注册格式
    → CloseClipboard
  → WriteStdOut("copied WxH to clipboard")
```

`-c` 与 `-o` 互斥，在参数校验阶段即拒绝。

### 4.5 screen（主显示器）

```
ParseCommandLine(Command::Screen)
→ MonitorFromPoint({0,0}, MONITOR_DEFAULTTOPRIMARY) → HMONITOR
→ CaptureMonitor(hmonitor, 5000)        // CreateForMonitor
→ 与 4.2 / 4.3 / 4.4 相同的输出分支
→ 自动命名 screen_<timestamp>.<ext>
```

`screen` 无窗口匹配与最小化恢复流程，也没有窗口周围的系统黄色边框。

## 5. 各模块职责

### 5.1 args（参数）

- 逐参数线性扫描，大小写不敏感；`--key=value` 内联形式仅支持长选项。
- 命令必须先出现；选项命名空间按命令隔离（`scan` 拒绝截图选项，`screen` 拒绝 `--pid`）。
- `--help` 可出现在任意位置，命令上下文中的帮助态覆盖其余校验。
- 输出选项（`-o` / `-c` / `-s` / `-q`）由 `capture` 与 `screen` 共享，但各自校验。
- 校验：`--scale ∈ (0,5]`、`--quality ∈ [1,100]`、`capture` 必须有 `--pid`、`-c` 不得与 `-o` 并用、`-o` 扩展名必须是 png/jpeg/bmp。
- 帮助文本集中在此模块（`PrintUsage` / `PrintHelp` / `PrintScanHelp` / `PrintCaptureHelp` / `PrintScreenHelp`）。

### 5.2 window_enum（窗口枚举）

- `EnumWindows` 回调过滤：标题非空、无 owner、无 `WS_EX_TOOLWINDOW`、可见或已最小化。
- 进程名经 `QueryFullProcessImageNameW` 取全路径后截取文件名。
- 标题按 256 起、倍增到 65536 的容量循环读取，避免固定缓冲截断。
- `FindWindowByPid` 优先返回非最小化窗口；仅剩最小化窗口时取第一个作回退。

### 5.3 capture（取帧）

核心链路（`GrabItem`，窗口与显示器共用）：

1. `CreateD3DDevice`：硬件设备，`D3D11_CREATE_DEVICE_BGRA_SUPPORT`，特性级别 11.1 → 11.0。
2. `WrapDevice`：`IDXGIDevice` → `CreateDirect3D11DeviceFromDXGIDevice` → WinRT `IDirect3DDevice`。
3. 目标捕获项：
   - 窗口：`IGraphicsCaptureItemInterop::CreateForWindow`
   - 显示器：`IGraphicsCaptureItemInterop::CreateForMonitor`
4. `CreateFreeThreaded` 帧池 + `CreateCaptureSession` + `StartCapture`，同步轮询 `TryGetNextFrame` 直到超时。
5. `frame.Surface()` 经 `IDirect3DDxgiInterfaceAccess` 取 `ID3D11Texture2D`，复制到 `D3D11_USAGE_STAGING` 纹理后 `Map` 读取。
6. 逐行 `memcpy` 到 `CaptureResult`，把 staging 的 `RowPitch` 归一化为紧凑行距。

`CreateItemFor*` 与帧读取都在 `try/catch (winrt::hresult_error)` 内，异常转成可读 `error`。

### 5.4 encode（编码）

- `EncodeCore` 是唯一编码核心：创建编码器 → 设置 JPEG 质量 → `SetSize` → 由内存 BGRA 建 `IWICBitmap` → 必要时 `WICBitmapScaler`（`Fant` 插值）→ `SetPixelFormat` 探测目标格式 → `IWICFormatConverter` → `WriteSource` → 两级 `Commit`。
- `EncodeToFile` 用文件流（`InitializeFromFilename`），`EncodeToMemory` 用 `CreateStreamOnHGlobal`，二者仅流来源不同。
- `ScaleToBgra` 供剪贴板使用：无缩放时直接拷贝，否则缩放并把结果 `CopyPixels` 为紧凑 BGRA。
- 缩放导致宽或高为 0 时返回 `--scale produces a zero-sized image`。

### 5.5 clipboard（剪贴板）

- `CopyImageToClipboard` 输出两种格式：`CF_DIB`（`BITMAPINFOHEADER` + 自下而上行序）与注册格式 `PNG`；任一成功即视为成功。
- 打开剪贴板最多重试 10 次、每次间隔 10 ms，缓解与其他进程的争用。

### 5.6 main（编排）

- `wmain` 分派命令与帮助态；`FailUsage` 统一「`error:` + usage + 退出码 2」。
- `WriteStream` 处理控制台与非控制台（重定向）两种 stdout/stderr：前者 `WriteConsoleW`，后者转 UTF-8 `WriteFile`。
- 自动命名：`AutoOutputName`（进程名 + pid + 时间戳）、`AutoScreenName`（`screen_` + 时间戳）、`SanitizeFileName`、`TimestampNow`。

## 6. 关键技术决策

- **不用 `GraphicsCapturePicker`**：该 API 总会弹出系统 UI，无法无头选择目标。改为对具体 `HWND` / `HMONITOR` 调用 `IGraphicsCaptureItemInterop::CreateFor*`。
- **经 interop 头转换类型**：WinRT 的 `IDirect3DSurface` 与 `ID3D11Texture2D` 分属不同 ABI，必须经 `IDirect3DDxgiInterfaceAccess` 桥接；链接 `windowsapp` 提供 WinRT 消费侧。
- **BGRA 直通 WIC**：WGC 帧为 32bpp BGRA，与 `GUID_WICPixelFormat32bppBGRA` 一致，编码器无需预转换像素布局。
- **stdout 经临时文件**：WIC 需要一个可 seek 的输出流，而管道不可 seek；因此先编码到临时文件再流式复制。
- **剪贴板双格式**：`CF_DIB` 兼容传统应用（画图/Office），注册 `PNG` 兼容现代应用与浏览器，兼顾最大兼容性。
- **最小化窗口透明恢复**：截图前 `SW_RESTORE`、截图后 `SW_MINIMIZE`，对调用者保持窗口状态不变（代价是恢复期间窗口短暂可见）。
- **无注释、`/W4 /WX`**：编译期把警告当错误，约束代码保持干净。

## 7. 错误处理与退出码

- 参数错误在 `ParseCommandLine` 内以 `error` 字符串返回，`main` 打印后退出码 2。
- 运行期错误（无匹配窗口、取帧失败、编码/写文件失败、剪贴板失败）返回退出码 1。
- 成功、`--help`、`scan`、`-o -`、`-c` 均为退出码 0。
- 稳定错误消息清单见 [`cli-spec.md`](cli-spec.md) 第 7 节，脚本可依赖这些文本。

## 8. 构建与测试

- CMake + `CMakePresets.json`：`x64-debug` / `x64-release`，Ninja 生成器，C++23。
- 编译选项：`/W4 /WX /utf-8 /EHsc /permissive-`；定义 `UNICODE` / `_UNICODE` / `NOMINMAX` / `WIN32_LEAN_AND_MEAN`。
- 链接库：`windowsapp`、`d3d11`、`dxgi`、`dwmapi`、`windowscodecs`、`shlwapi`、`shell32`、`user32`、`ole32`（按目标归属，不全局）。
- 测试（`ctest`）全部为 CLI 级黑盒断言：成功路径断言退出码 0，错误路径用 `WILL_FAIL` 断言非 0。新增命令或校验时在 `CMakeLists.txt` 追加对应 `add_test`。

## 9. 扩展指南

- **新增输出格式**：在 `ImageFormat` 增加枚举 → `args.cpp` 的 `ParseFormat` / `FormatExtension` / 校验消息 → `encode.cpp` 的 `ContainerForFormat`。
- **新增命令**：在 `Command` 增加枚举（含 `*Help`）→ `ParseCommandLine` 增加裸词与帮助分支 → `Print*Help` + 顶层帮助列表 → `wmain` 分派 → 在对应输出阶段接线 → 追加 `ctest` 用例。
- **新增截图来源**：在 `capture` 增加 `CreateItemFor*` 变体并复用 `GrabItem`，避免复制帧读取逻辑。
- **新增输出目标**：优先复用 `EncodeToMemory` / `ScaleToBgra`，让新目标只负责「如何送达」，不重复编码逻辑。
