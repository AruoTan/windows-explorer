# Explorer Extensions 0.3.0

Windows x64（AMD64），当前用户权限，无自动提权或服务。Windows 测试机器无需安装开发工具。

1. 完整解压，保持 ExplorerExtensions.exe 与 ExplorerExtensions.dll 同目录。
2. 双击 ExplorerExtensions.exe，分别选择驱动器导航去重和关闭虚拟桌面名称提示，然后点击“应用”。新安装的两个开关均默认关闭；旧驱动器选择可迁移。
3. 首次开启桌面提示拦截时，在独立辅助进程准备匹配 twinui.dll 的 PDB。准备期间和失败时保留系统原始提示；“状态”中的 Popup=Enabled 才代表安装并启用。
4. “状态”查看功能结果，“日志”查看系统/模块版本、符号身份、签名及 API 错误。默认不记录桌面切换事件、桌面名称、窗口标题或应用内容。
5. 取消勾选并应用即可停用对应功能。“全部停用”停用两个功能并删除统一登录启动项；ExplorerExtensions.exe --disable 与此相同。

统一命令行：

    ExplorerExtensions.exe --enable drive-navigation
    ExplorerExtensions.exe --disable drive-navigation
    ExplorerExtensions.exe --enable virtual-desktop-popups
    ExplorerExtensions.exe --disable virtual-desktop-popups
    ExplorerExtensions.exe --disable
    ExplorerExtensions.exe --status
    ExplorerExtensions.exe --diagnose

命令行成功返回 0，失败返回 1，请查看日志。--diagnose 只解析符号，不安装 Hook、不修改功能配置。

配置：HKCU\Software\Nilesoft\Shell\ExplorerExtensions
DWORD DriveNavigationFilterEnabled（默认 0）
DWORD DisableVirtualDesktopPopups（默认 0）
请通过 EXE 修改配置。启用任意功能会登记当前用户的单个 NilesoftExplorerExtensions 登录启动项。
首次配置会停用旧 DriveNavigationFilter 并移除其旧启动项。请勿再运行旧 EXE。

日志：%LOCALAPPDATA%\Nilesoft\ExplorerExtensions\logs
符号：%LOCALAPPDATA%\Nilesoft\ExplorerExtensions\symbols\twinui.pdb\<GUID+Age>\twinui.pdb
符号失败只使桌面提示拦截不可用。恢复网络/修复该身份的缓存后，先关闭再开启此功能重试。不要从其他版本复制 RVA/偏移。

安全更新/回退：先全部停用，再手动重启 Explorer，之后替换整个包。已发布的回调和 MinHook 跳板会驻留到 Explorer 退出；日志中的 unload.deferred 是安全延迟卸载，不代表停用失败。不强行释放 DLL，不结束 Explorer。

项目源码与详细实测记录位于项目 docs/；GUI 的配置请求或 Enabled 状态不能代替可见行为验收。目标 OS Build 为 26300.9711；其他版本需重新确认其模块/PDB 与实际行为。
第三方许可证：THIRD_PARTY_NOTICES.txt、MinHook-LICENSE.txt。
