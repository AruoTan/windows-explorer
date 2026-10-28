# 0.4.0 独立性清理验证

日期：2026-10-28，Asia/Shanghai。

## 改动范围

统一登录启动项为 `ExplorerExtensions`，配置为 `HKCU\Software\ExplorerExtensions`，数据为 `%LOCALAPPDATA%\ExplorerExtensions`，同步对象和应用清单使用本项目身份。移除跨项目迁移、旧发布包和失效测试入口；驱动器识别改用 Windows 原生注册。

## 验证状态

- 隔离目录中的 Linux → Windows x64 交叉编译通过。
- Windows 独立进程 Hook 测试通过：两种实例 ABI、完整转发、实际 x64 Hook 和并发启停。
- Windows 原生注册识别测试通过：空/未注册入口、未固定入口、盘符根目录、子目录/UNC、未知类型和无效 CLSID。仅使用本项目临时注册表键。
- Windows 26300.9711 的 `--diagnose` 返回 0；使用新的数据目录下载并匹配 PDB，GUID/Age 和完整签名确认通过。
- 当前目录及最终二进制扫描未发现已移除业务项目的标识。
- 发布包已按本次构建重新生成，包含统一 EXE/DLL 与必要许可说明；Linux 与 Windows PowerShell 打包内容逐字节一致，校验值正确。
- Windows 设置窗口检查通过：两个开关均关闭，不读取或导入其他配置。
- PowerShell 脚本语法检查通过。
- 目视桌面提示、多显示器、任务视图、音量/亮度及 Explorer 重启仍需实际验收。

此前的目标模块/PDB 与 Hook 验证见 [0.3.0 实测记录](test-records/2026-10-28-windows-26300.9711/README.md)。该记录是本项目旧构建证据，不冒充当前版本实测结果。

本次测试证据：[0.4.0 记录](test-records/2026-10-28-independence/)，包含当前二进制 SHA-256、诊断日志和测试结果。当前版本尚未重新执行真实 Explorer 中的完整安装/切换/重启验收，不能以此前版本记录替代。
