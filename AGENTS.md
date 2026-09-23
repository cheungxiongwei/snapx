# AGENTS.md

Windows CLI tool. C++23 / MSVC / Windows SDK. CMake + CMakePresets. No third-party deps.

## Stack constraints

- Language: C++23, compiler: MSVC only (`cl.exe`). Do not use GCC/Clang-only extensions.
- Dependencies: none beyond the Windows SDK. WinRT is available as SDK headers; this is not considered a third-party dependency. Do not add vcpkg/Conan/FetchContent libraries without asking.
- Argument parsing: use `CommandLineToArgvW` / `wmain`, not a third-party parser.
- Prefer Unicode (`W`) Win32 APIs and `std::wstring`/`std::filesystem` for paths.
- Source files are UTF-8; add `/utf-8` to compiler flags so MSVC doesn't misinterpret literals as the ANSI codepage.

## Build / test commands

Presets are the source of truth; read `CMakePresets.json` before changing the build.

```
cmake --preset <configure-preset>
cmake --build --preset <build-preset>
ctest --preset <test-preset>
```

- Build artifacts go under `build/`; never commit them.
- Run from a Developer Command Prompt (or a shell with `vcvarsall.bat` sourced) so `cl.exe` and the Windows SDK are on `PATH`. A missing compiler is a shell problem, not a CMake problem.

## Conventions

- Do not add comments unless asked.
- Narrow to `wWinMain`/`wmain` for the entrypoint; keep `main`/`WinMain` out.
- Link only the SDK libs actually used (e.g. `shell32`, `shlwapi`, `advapi32`); add them in the `target_link_libraries` of the owning target, not globally.
- Enable `/W4` and treat warnings as errors in the project's CMake flags; fix warnings rather than suppressing them.

## Gotchas

- C++/WinRT is consumed as SDK headers (`<winrt/...>`), not a package; link the eventual-consume lib `windowsapp`.
- `Windows.Graphics.Capture` cannot be driven headlessly for a chosen window via `GraphicsCapturePicker` (that always shows system UI). Target a specific `HWND` with `IGraphicsCaptureItemInterop::CreateForWindow` from `<windows.graphics.capture.interop.h>`.
- Convert the frame's `IDirect3DSurface` through `IDirect3DDxgiInterfaceAccess` (`<windows.graphics.directx.direct3d11.interop.h>`) to get the `ID3D11Texture2D`; copy to a `D3D11_USAGE_STAGING` texture before mapping.
- Encoding uses WIC (`windowscodecs`). Captured pixels are 32bpp **BGRA** (`GUID_WICPixelFormat32bppBGRA`), which matches the D3D surface layout directly.
- Console output: `WriteConsoleW` silently fails when stdout/stderr is redirected. Detect with `GetConsoleMode` and fall back to UTF-8 `WriteFile`. This bites in CI and shell pipelines.
