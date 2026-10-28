# 驱动器导航去重：独立版 0.2.0

适用于 Windows 10/11 x64（AMD64）。不依赖 Windhawk、PowerShell 或额外 C++ 运行库。
当前已完成编译与本地模拟检查，尚未在 Windows Explorer 实机验证。

1. 保持本目录的文件放在一起，双击 `DriveNavigationFilter.exe` 启用。
2. 成功时没有终端窗口或完成提示。它会在当前用户的登录启动项中登记自己，并启动一个无窗口后台进程。
3. 继续使用 Nilesoft 的“独立显示／取消独立”菜单；独立入口生效时过滤左侧“此电脑”下对应的重复盘符，右侧列表保留。
4. 双击 `status.js` 查看配置、加载器和当前 Explorer 的状态。Ready 仅表示指针钩子建立；过滤次数增加才表示遇到符合条件的导航枚举。
5. 双击 `disable.js` 停用并删除本功能的登录启动项，后台加载器会退出。它不会取消已有独立入口。

如果 `.js` 默认由编辑器打开，请通过“打开方式”选择 Windows Script Host；也可以显式执行 `wscript.exe //E:JScript .\disable.js` 或 `wscript.exe //E:JScript .\status.js`，无需修改默认文件关联。

如果导航树显示未更新，关闭并重新打开资源管理器窗口。必要时自行重启 Explorer。
DLL 为避免被仍在使用的 COM 枚举器调用，在首次加载过滤器后保留映射直到 Explorer 退出；停用会使它透传。
更新、移动或删除 DLL 前，请先停用，再手动重启 Explorer 释放旧 DLL。移动后重新运行 EXE，会覆盖本功能的单个登录启动项路径，不累积历史启动项。

加载器每两秒检查当前用户、当前会话的系统 Explorer 窗口，并通过定向到这些线程的 Windows 消息钩子加载同目录 DLL。
不修改驱动器隐藏策略、卷标、文件内容或磁盘映射，不请求管理员权限，也不结束 Explorer。
自动展开和异步通知在某些 Windows 版本上可能绕过普通导航枚举，实际去重效果仍须验证。

源码：项目的 `extensions/drive-navigation-filter/`。
完整说明：项目的 `docs/drive-navigation-filter.md`。
`SHA256SUMS` 用于核对随包二进制文件。
