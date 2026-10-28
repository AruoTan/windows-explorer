# Explorer Extensions

面向 Windows x64 的当前用户 Explorer 扩展。**统一入口为 `ExplorerExtensions.exe`**，双击打开设置窗口；不再通过多个 EXE 或 JScript 管理不同功能。

- 驱动器导航去重：保留现有独立盘符去重逻辑。
- 关闭虚拟桌面名称提示：独立配置，默认关闭，只拦截 `twinui!CAudioFlyoutController::VirtualDesktopSwitched`。

## 使用

完整解压 `dist/ExplorerExtensions-x64.zip`，保持 EXE 和 DLL 在同一目录，双击 EXE，勾选所需功能后点击“应用”。首次开启虚拟桌面提示拦截会在后台下载匹配的 Microsoft PDB；点击“状态”查看实际结果，“日志”打开诊断目录。保存配置不等于 Hook 已成功安装。

Windows 测试机无需安装开发环境。没有自动提权、系统服务或自动重启 Explorer。

命令行也使用同一个 EXE：

```text
ExplorerExtensions.exe --enable drive-navigation
ExplorerExtensions.exe --disable drive-navigation
ExplorerExtensions.exe --enable virtual-desktop-popups
ExplorerExtensions.exe --disable virtual-desktop-popups
ExplorerExtensions.exe --disable
ExplorerExtensions.exe --status
ExplorerExtensions.exe --diagnose
```

`--disable` 停用全部功能并删除统一登录启动项。`--diagnose` 仅检查 OS、模块和匹配符号，不安装 Hook、不改功能配置。命令行成功返回 0，失败返回 1，详情在日志中；`--status` 显示状态窗口。双击不自动启用新功能。

首次应用配置会停用旧 `DriveNavigationFilter` 加载器并移除其旧启动项；旧驱动器开关会作为初始选择保留。新功能默认关闭。旧 DLL 仍可能驻留，更新/移动文件前应先全部停用并手动重启 Explorer。旧 EXE 与脚本仅保存在 `releases/` 历史包中。

## 开发

```bash
bash scripts/build.sh /path/to/zig
python3 scripts/package.py
bash scripts/test.sh /path/to/zig
```

Zig 固定为 `toolchains/zig.version` 指定的 0.15.2。输出为 `out/explorer-extensions/` 和 `dist/ExplorerExtensions-x64.zip`。依赖源码已固定在 `third_party/`；构建时不下载依赖。

```text
extensions/explorer-extensions/
  src/host.cpp                 统一设置界面、命令行和加载器
  src/dllmain.cpp              DLL 入口和功能协调线程
  src/features/               驱动器过滤与桌面提示拦截
  src/symbol_*.cpp            独立进程符号解析/通信
  src/module_identity.cpp     模块 PDB 身份核对
  src/diagnostics.cpp         诊断日志
  include/                    内部接口
  resources/                  设置窗口与 asInvoker 清单
scripts/                      构建、打包和验证
packaging/explorer-extensions/ 发布说明和完整第三方许可
third_party/                  MinHook 固定源码及依赖记录
releases/                     原始发布包
out/、dist/                    生成产物，不入库
```

[构建说明](docs/build.md) · [架构与生命周期](docs/architecture.md) · [配置、诊断与回退](docs/configuration.md) · [Windows 实测记录](docs/validation.md) · [原始验收要求](docs/requirements/virtual-desktop-popup.md)

Windows 26300.9711 的实际 PDB 定位、安装与自动化测试结果以验证记录为准。快捷键/任务视图的可见效果、动画、音量/亮度、多显示器及 Explorer 重启仍需逐项实机确认，不能用编译或独立进程测试代替。
