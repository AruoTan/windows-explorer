# 驱动器导航去重

此功能只扩展 Windows Explorer。使用 `ExplorerExtensions.exe --enable drive-navigation` 或 `--disable drive-navigation` 管理，配置为 `HKCU\Software\ExplorerExtensions\DriveNavigationFilterEnabled`，默认关闭。

## Windows 原生识别

读取当前用户和系统的 `Software\Microsoft\Windows\CurrentVersion\Explorer\Desktop\NameSpace`，再通过合并的 `HKEY_CLASSES_ROOT\CLSID` 视图验证每个注册入口。仅在以下条件全部满足时，过滤“此电脑”导航枚举中的重复盘符：

- 入口使用有效 CLSID，且 `System.IsPinnedToNameSpaceTree` 为 DWORD 1。
- 入口为 Windows Folder Shortcut 类型，具有标准 `Instance\InitPropertyBag`。
- `TargetFolderPath` 是盘符根目录，例如 `C:` 或 `C:\`。

未注册、未固定、目标为子目录/UNC 路径、未知类型和无效数据均不参与过滤。不读取其他应用的控制键，不创建菜单、命名空间入口或修改驱动器策略。

只处理 `SHCONTF_NAVIGATION_ENUM` 且源对象为“此电脑”的枚举；右侧文件列表、磁盘映射和文件内容保持原样。不同 Shell 类型和异步导航路径可能不符合该条件，需实机确认。

## 状态、测试与回退

`Drive=Enabled` 表示枚举入口已建立，`FilteredEnumerations` 表示经过匹配的过滤路径。测试程序 `native-aliases.exe` 仅在本项目的临时注册表键中构造注册数据，验证识别与保守跳过行为，不写入系统命名空间。

停用后后续枚举透传。已返回的 COM 枚举器可能仍引用代码，因此保留入口和 DLL 到 Explorer 退出。更新/回退按 [配置说明](configuration.md) 操作。

Windows 原生注册机制参考：[命名空间位置](https://learn.microsoft.com/en-us/windows/win32/shell/nse-junction)、[合并的类注册视图](https://learn.microsoft.com/en-us/windows/win32/sysinfo/merged-view-of-hkey-classes-root)。
