# 虚拟桌面名称提示拦截：原始验收约束

本文件保留原始开发与验收约束。实现、配置和实测状态分别见 `docs/architecture.md`、`docs/configuration.md` 与 `docs/validation.md`；不得仅凭代码完成将全部实机验收标为通过。

## 行为与定位

在当前用户交互会话的 Shell `explorer.exe` 中，定位 `twinui.dll` 的 `CAudioFlyoutController::VirtualDesktopSwitched`。只跳过桌面名称提示的原始回调并返回 `S_OK`，禁用时完整转发。不能拦截全部虚拟桌面通知分发，也不能依赖具体快捷键。

快捷键、任务视图等来源均需覆盖；保留切换动画、任务视图、命名功能及音量/亮度提示，不能修改桌面名称、关闭所有动画或切换到其他桌面规避。

参考签名为下列实例方法，实际 Hook 必须包含隐含 `this` 参数；最终签名必须由目标系统符号确认：

```cpp
HRESULT VirtualDesktopSwitched(IVirtualDesktop* desktop);
HRESULT VirtualDesktopSwitched(IVirtualDesktop* desktop,
                              VirtualDesktopSwitchType switchType);
```

[参考实现源码](https://github.com/ramensoftware/windhawk-mods/blob/main/mods/virtual-desktop-popups-disable.wh.cpp) 是需求提供的参考，不代表已经在目标系统验证。

## 兼容与失败策略

首个目标为 Windows x64 **OS Build 26300.9711**，必须使用与目标 DLL 匹配的 PDB 实际确认。缓存按 PDB 标识和 Age 区分，不使用跨版本固定偏移。

最初收到的源码没有 PDB 解析模块。现已新增独立辅助进程解析，并固定 MinHook 依赖与许可证。符号缺失、不匹配、签名不确定或安装失败时仅停用此功能，保留系统行为和其他扩展功能。

## 配置、生命周期与权限

提供命名符合现有风格的独立布尔配置 `DisableVirtualDesktopPopups`，默认关闭。复用现有加载/持久化机制，线程安全地处理重复启停和 Explorer 重启。安装与配置切换不能发生竞态。

`DllMain` 内不得下载符号、阻塞等待或执行复杂初始化。卸载需确认没有正在执行或仍可进入的 Hook 回调；不满足时延迟卸载并记录原因。当前 DLL 的永久驻留策略不等于已经实现该卸载协议。

保持当前用户权限，不新增自动提权或服务；无权访问目标进程时明确报告。Linux devcontainer 可交叉编译 Windows x64，Windows 测试机无需开发环境。

## 日志与验收

记录 OS 内部版本、PID、模块版本、符号匹配结果、选用签名，以及安装/启用/禁用/卸载结果；API 失败记录返回值和适用错误码。默认不记录每次切换，不记录桌面名称、窗口标题或应用内容。

目标机器测试必须覆盖：

- 快捷键与任务视图切换，默认和自定义桌面名称均无提示。
- 音量、亮度和切换动画正常。
- 多显示器、连续快速切换、创建/删除桌面正常。
- 禁用后后续切换恢复原提示。
- 多次启停和 Explorer 重启后无重复 Hook、崩溃或任务栏异常，配置恢复。
- 符号不可用/不匹配时仅此功能停用。

交付应包含源码、构建配置、配置说明、诊断/回退方法与真实的目标机器记录。Linux 构建、模拟测试和其他 Windows 版本的成功记录均不能代替实机验收。各项实际执行结果以 `docs/validation.md` 为准，未执行的条目必须保持未验收。
