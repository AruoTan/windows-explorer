# Windows 实测与构建记录

日期：2026-10-28，用户时区 Asia/Shanghai。日志使用 UTC。通过 WSL 调用宿主 Windows 执行下列检查；不是将 Linux 模拟检查当作 Windows 验收。

## 实际目标

| 项目 | 实际值 |
| --- | --- |
| OS | Windows x64，10.0.26300.9711 |
| 当前交互会话 | 1 |
| Explorer PID | 28772（整个自动化验证期间未变化） |
| twinui 固定文件版本 | 10.0.26100.9549 |
| twinui 资源 FileVersion 字符串 | 10.0.26100.9949 (WinBuild.160101.0800) |
| twinui SHA-256 | `b2262a842adf22f78bf596dcf6815f1b3aec282469995d939901e93bcf943d65` |
| PDB GUID | `FDDD5C12E412A64AD5FB8AA23B502987` |
| PDB Age | 1 |
| 符号确定的 RVA | `0x358ED0`（3509968） |

实际 PDB 解码得到：

```text
public: virtual long __cdecl CAudioFlyoutController::VirtualDesktopSwitched(struct IVirtualDesktop * __ptr64,enum VirtualDesktopSwitchType) __ptr64
```

因此本模块采用 **HRESULT(this, IVirtualDesktop*, VirtualDesktopSwitchType)**。上表 RVA 只是实测证据；生产代码没有写入该偏移，也不按 OS Build 选择偏移。每次新安装仍核对目标模块 GUID/Age 与匹配 PDB、完整签名和可执行地址。

## 已通过的自动化子集

| 检查 | 结果与证据 |
| --- | --- |
| Linux → Windows x64 构建 | Zig 0.15.2，C++23，`-Wall -Wextra -Werror`，EXE/DLL 均成功 |
| Windows 独立进程 Hook 测试 | 两种实例 ABI 均使用实际 MinHook；启用时跳过原调用，禁用时完整保留 this/desktop/switchType 与原 HRESULT；4 调用线程及每种签名 20000 次原子开关切换通过，回调归零后才移除测试 Hook |
| 目标 PDB 定位 | Microsoft 匹配 PDB 下载和 GUID/Age 校验通过，`--diagnose` 返回 0，明确识别二参数版本 |
| 错误符号隔离 | 临时替换本目标缓存 PDB 为损坏文件，真实 Explorer 的 Popup=Unavailable（1306），Drive=Enabled；缓存由 finally 恢复 |
| 实际 Explorer 安装 | MinHook Initialize/CreateHook/EnableHook 均返回 MH_OK |
| 5 次关闭/开启 | 每次状态确认 Disabled/Enabled；日志仅有一次成功 CreateHook 和一次 EnableHook，后续仅切换开关，无重复安装 |
| 关闭全部与恢复选择 | 明确确认 Disabled 并等待加载器退出；保留原驱动器选择，提示功能恢复关闭，测试前无自启动，测试后仍无自启动 |
| Explorer 存活 | PID 28772 未变化；这只证明进程未退出，不替代任务栏目视检查 |
| 统一设置窗口 | 从最终解压目录启动 EXE，确认两个复选框和管理按钮存在，提示选项未选中；只读检查后关闭，无配置更改 |
| Windows PowerShell | 5.1 解析全部 PS 脚本无语法错误；原生打包成功 |
| 发布 ZIP | Linux/Windows 两种打包的 6 个文件内容一致，CRC 与二进制 SHA-256 通过；不包含旧 EXE 或 JScript 入口 |

快速关闭后重试曾暴露“Unavailable 被误当成关闭确认”的竞态，现已要求明确的 Disabled 确认，并在全部停用时等待加载器退出；上表为修复后的重跑结果。

目标 Explorer 实际加载的验证副本与最终构建 **逐字节相同**：

```text
c9dbaae1dbfa68c3708dfa8b33cfa86dea785f7ef832d6eff092e51dd8bbd05a  ExplorerExtensions.dll
b03b88edefc2906d7d13cb395379421c026741856e9f163ae5e3a5ec83ce2ddf  ExplorerExtensions.exe
```

ZIP SHA-256：`f3018025593854927c6bb2a47704f6d9bc4c0d859f9ab7379fa941ac2da52fe4`。

原始证据位于 [本次测试目录](test-records/2026-10-28-windows-26300.9711/)：`results.json`、`baseline.json`、`metadata.json`、`explorer.log`、`symbols.log`、`SHA256SUMS`。Explorer 日志保留最终验证时间段的原始行，早期构建的诊断不混入最终成功记录。日志不含桌面名称、窗口标题或应用内容。

## 待用户目视验收

以下未执行，不标记为通过。用户已表示可稍后反馈。

- 快捷键和任务视图切换均不再显示桌面名称提示。
- 默认名称与自定义名称的桌面均生效。
- 音量、亮度提示与桌面切换动画正常。
- 多显示器、连续快速切换、创建和删除桌面正常。
- 禁用后后续切换恢复原生提示。
- 任务栏界面正常；手动重启 Explorer 后恢复配置，无重复 Hook 或异常。

实际行为验收尚未全部完成。自动化没有切换/重命名桌面、调整动画或音量/亮度、创建/删除桌面，也没有自动重启 Explorer。Windows 原生 Zig 编译未执行（已执行 Linux 交叉编译及 Windows 实际运行）。

## 手动验收步骤

1. 测试期间加载过验证副本。开始最终目录的验收前，先从统一 EXE 全部停用，手动重启 Explorer，释放验证 DLL，再运行最终目录 `ExplorerExtensions.exe`。
2. 勾选“关闭虚拟桌面名称提示”并应用，状态确认 Popup=Enabled，分别用快捷键和任务视图切换；同时检查默认/自定义名称、多显示器和连续切换。
3. 检查音量、亮度及切换动画，创建/删除桌面并观察任务栏。无需修改桌面名称来实现屏蔽，名称仅用于验证现有命名场景。
4. 取消勾选并应用，确认 Popup=Disabled，后续切换应恢复原生提示。
5. 重复开启、手动重启 Explorer，检查配置、提示屏蔽和任务栏；记录新 PID/日志与各项目视结果。

这些操作均由测试者在实际 Windows 桌面完成。若需移动或更新文件，先全部停用并手动重启 Explorer，勿强行卸载已固定 DLL。
