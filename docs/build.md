# 构建与依赖

Linux / WSL / devcontainer 可直接交叉编译 Windows x64，无需 Windows SDK。Windows 使用者只需要完整 ZIP，无需开发环境。

## 固定依赖

- Zig **0.15.2**：版本入口 `toolchains/zig.version`，构建脚本拒绝其他版本。目标 `x86_64-windows-gnu`、C++23、静态 C++ 运行库、`-Wall -Wextra -Werror`。
- MinHook **1.3.4**：固定提交 `c3fcafdc10146beb5919319d0683e44e3c30d537`，所需 Windows x64 实现与许可证置于 `third_party/minhook/`，记录于 `third_party/dependencies.json`。BSD-2-Clause，包含 HDE 许可。
- DbgHelp、WinHTTP、Shell、Version 等使用 Windows 系统 API，不需要安装额外开发或运行环境；DbgHelp/WinHTTP 仅链接到 EXE，DLL 不导入二者。
- Linux 构建使用 Bash 与 sha256sum，打包使用 Python 3.9+ 标准库；Windows 构建/打包使用 PowerShell 5.1+ 和系统 .NET。

Zig 官方归档：[下载索引](https://ziglang.org/download/index.json)。

| 主机 | 归档 | SHA-256 |
| --- | --- | --- |
| Linux x64 | [0.15.2 tar.xz](https://ziglang.org/download/0.15.2/zig-x86_64-linux-0.15.2.tar.xz) | `02aa270f183da276e5b5920b1dac44a63f1a49e55050ebde3aecc9eb82f93239` |
| Windows x64 | [0.15.2 ZIP](https://ziglang.org/download/0.15.2/zig-x86_64-windows-0.15.2.zip) | `3a0ed1e8799a2f8ce2a6e6290a9ff22e6906f8227865911fb7ddedc3cc14cb0c` |

构建不联网下载依赖。Zig 采用 MIT；静态运行库、LLVM libc++/libc++abi/libunwind 与 MinGW 的完整许可已整理到随包 `THIRD_PARTY_NOTICES.txt`，MinHook/HDE 完整许可单独随包保留。组件许可不替代本项目自身的许可。

## Linux / WSL / devcontainer

```bash
bash scripts/build.sh /path/to/zig
python3 scripts/package.py
bash scripts/test.sh /path/to/zig
```

脚本按自身目录定位项目，可从项目外调用。默认缓存 `.cache/zig/`，可通过 `ZIG_GLOBAL_CACHE_DIR` 指定可写目录。临时编译文件放在 `/tmp` 并在退出时清理。

输出为 `out/explorer-extensions/ExplorerExtensions.exe`、`ExplorerExtensions.dll`、`SHA256SUMS`；测试产物为 `out/tests/hook-smoke.exe` 与 `native-aliases.exe`。

## Windows 开发机

```powershell
.\scripts\build.ps1 -Zig C:\tools\zig\zig.exe
.\scripts\package.ps1
```

不修改系统执行策略。WSL 可调用 Windows PowerShell 运行验证脚本，但执行真实 Explorer 测试会改变本项目配置，应先阅读脚本及验证记录。

## 打包与测试

`python3 scripts/package.py` 或 `scripts/package.ps1` 在校验两个不同二进制文件的 SHA-256 后生成 `dist/ExplorerExtensions-x64.zip`。ZIP 仅含 EXE、DLL、SHA256SUMS、README.md、THIRD_PARTY_NOTICES.txt、MinHook-LICENSE.txt，EXE 是唯一入口。

可指定输出路径：`python3 scripts/package.py --destination /tmp/ExplorerExtensions-x64.zip`，或 PowerShell `-Destination`。

`native-aliases.exe` 验证原生命名空间注册识别，仅使用本项目临时注册表键。`hook-smoke.exe` 在自身 Windows 进程测试两种实例 ABI、实际 MinHook、返回值与参数转发及并发启停，不连接 Explorer。`scripts/validate-windows.ps1` 默认仅诊断；加 `-ExerciseExplorer` 才执行当前 26300.9711 模块的错误符号隔离、安装和多次启停子集。该脚本不切换/重命名桌面、不重启 Explorer、不采集应用内容。完整脚本不应在同一 Explorer 已加载旧验证 DLL 时重复运行；更新/重新完整验证前先停用并手动重启 Explorer。

目视效果、多显示器、音量/亮度、动画以及 Explorer 重启恢复仍需 Windows 实机验收；自动化子集和交叉编译不能代替。
