# 驱动器导航去重

驱动器功能已接入统一 `ExplorerExtensions.exe`，不再生成单独的驱动器 EXE/DLL。原有独立盘符识别和导航枚举过滤逻辑保留；只改变协调、配置与安全驻留边界。

使用统一设置窗口，或 `ExplorerExtensions.exe --enable drive-navigation` / `--disable drive-navigation`。新配置为 `HKCU\Software\Nilesoft\Shell\ExplorerExtensions\DriveNavigationFilterEnabled`，默认关闭；旧 `DriveNavigationFilter\Enabled` 在第一次配置时迁移初始选择，然后置 0 并停用旧加载器。

继续使用 Nilesoft 的“独立显示／取消独立”菜单；独立入口匹配时过滤导航树“此电脑”下对应重复盘符，不改右侧列表、卷标、文件、磁盘映射或驱动器策略。某些异步枚举路径可能绕过过滤，仍需实机观察。

状态 `Drive=Enabled` 表示枚举入口已经建立，`FilteredEnumerations` 增加才表示遇到匹配过滤路径。没有配套的原始模拟测试 API，不能声称已运行原包中的历史模拟测试。

停用立即使后续枚举透传。已返回的 COM 枚举器仍可能引用 DLL，因此保留已发布入口和代码到 Explorer 退出。更新/回退按 [配置说明](configuration.md) 先全部停用并手动重启 Explorer。历史 0.2.0 原包完整保存在 `releases/drive-navigation-filter/0.2.0/`。
