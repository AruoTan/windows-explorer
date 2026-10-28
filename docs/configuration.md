# 配置、诊断与回退

统一执行 `ExplorerExtensions.exe`。无参数打开设置窗口；两个独立复选框通过“应用”保存。“全部停用”关闭所有功能并删除统一登录启动项。

## 配置

`HKCU\Software\Nilesoft\Shell\ExplorerExtensions`：

| DWORD | 默认 | 含义 |
| --- | --- | --- |
| `DriveNavigationFilterEnabled` | 0 | 原驱动器导航去重 |
| `DisableVirtualDesktopPopups` | 0 | 仅抑制桌面名称提示 |

仅值 1 表示启用；缺失视为关闭。首次配置时，旧 `DriveNavigationFilter\Enabled` 作为驱动器初始选择，之后停用旧加载器与旧启动项。请通过统一 EXE 配置，以获得锁保护、登录启动项同步和停用确认。

任意功能启用时，在 `HKCU\Software\Microsoft\Windows\CurrentVersion\Run` 写入 `NilesoftExplorerExtensions`，值为当前 EXE 绝对路径及 `--background`。全部停用则删除。不会新增服务或请求管理员权限。

## 状态和日志

`--status` 显示配置请求及每个当前用户/会话 Explorer 的 `DriveState`/`PopupState` 与错误码，依据 PID 和进程创建时间排除旧状态。`Enabled` 表示安装并启用；`Preparing` 表示后台符号准备；`Unavailable` 表示该功能失败并保持原始行为；`Disabled` 表示透传。驱动器的计数只表示匹配的枚举路径，不证明界面效果。

日志位于 `%LOCALAPPDATA%\Nilesoft\ExplorerExtensions\logs\<PID>-<进程启动时间>.log`，使用 UTC 时间和 UTF-8。记录 OS 内部版本、目标 PID、模块固定版本和资源版本字符串、PDB GUID/Age、匹配/签名/RVA、API 返回码及启停/延迟卸载结果。`hook.MH_*` 的数字是 MinHook 状态枚举，其余错误按对应 API 的 Win32/HRESULT/HTTP 语义解释。

默认不记录每次桌面切换，也不读取或记录桌面名称、窗口标题或应用内容。首次解析时的符号候选日志只涉及目标函数及编译器生成的内部名称。

## 符号失败

执行 `--diagnose`，只做符号诊断，不修改配置或安装 Hook。PDB 通过 HTTPS 从 Microsoft 符号服务器下载，缓存为：

```text
%LOCALAPPDATA%\Nilesoft\ExplorerExtensions\symbols\twinui.pdb\<GUID+Age>\twinui.pdb
```

缺失、损坏、不匹配、未知/多义签名、非可执行地址、网络错误及 Hook 失败均使提示拦截不可用，不影响驱动器功能。恢复网络后关闭再开启重试。损坏缓存可在提示功能关闭、辅助进程退出后删除对应 GUID+Age 目录，再执行诊断重新获取。不要套用其他版本的偏移或 PDB。

无法打开 Explorer 或安装线程消息钩子时记录错误；同一目标线程不重复提权/反复安装，等待目标变更或重新启用加载器后再试。

## 回退

1. 执行 `ExplorerExtensions.exe --disable`，确认状态变为 Disabled。
2. 手动重启 Explorer，释放已固定的 DLL 与跳板。
3. 替换/移动完整发布包，再运行最终目录下的 EXE 重新配置。

延迟卸载日志是安全驻留说明。不得用进程工具强行卸载 DLL。需要回到原 0.2.0 时，在上述停用和手动重启后使用 `releases/drive-navigation-filter/0.2.0/` 原包；它仅包含驱动器功能。
