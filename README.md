# EngineWire UE 5.4

基于 [ChiR24/Unreal_mcp v0.6.0-beta-f](https://github.com/ChiR24/Unreal_mcp/releases/tag/v0.6.0-beta-f) 的 UE 5.4 兼容源码、Windows 编译包与 Agent 接入说明。非上游官方发行，保留 MIT 许可证。此仓库不包含用户工程或测试资产。

## 安装

从 [Releases](https://github.com/dadaozei01/enginewire-ue54/releases) 下载 Windows ZIP 与 SHA256SUMS-Win64.txt。关闭 UE，将 ZIP 内 EngineWire 文件夹放到以下一个位置：

- 工程级：<工程>/Plugins/EngineWire
- 公共插件：<UE_5.4>/Engine/Plugins/Marketplace/EngineWire

正确层级是 EngineWire/EngineWire.uplugin，不要双重嵌套。备份旧版本，避免工程级与引擎级重复安装同名插件。打开 UE，在插件窗口启用 EngineWire，然后在 Project Settings → Plugins → EngineWire 勾选 Enable Native MCP Server，重启编辑器。

状态显示 MCP :3000 (0) 表示服务已启动，0 是连接客户端数量。勾选后仍显示 off 时先确认重启。

## 统一操作规则

已验证的 EngineWire MCP 优先；成熟批量任务可用编辑器内 UE Python；独立 Remote Execution 备用。正常接入不需要任务按钮，也不要求另开 Python 网络连接。

跨 Agent 的共享规范以 [ue-ai-bridge-protocol](https://github.com/dadaozei01/ue-ai-bridge-protocol/blob/main/docs/agent-entry.md) 为准；本仓库负责插件安装、编译包与客户端接入细节。

## 平台验证

| 平台 | 状态 |
|---|---|
| Windows x64 / UE 5.4.4 | 已编译、加载，MCP 与蓝图/材质/Niagara 操作实测通过（2026-10-23 包）；v0.6.0-beta-f-ue54.3 包（2026-10-26）含退出崩溃修复，重新编译后编辑器加载、作用域令牌连接、只读查询实测通过，退出稳定性与写入尚未在该版本上重新验证；2026-10-23 的旧包不含此修复 |
| Mac (Apple Silicon) / UE 5.4.4 | 已用 Xcode 27.0 编译为 Universal（arm64 + x86_64）；2026-10-25 含退出崩溃修复重新编译，编辑器加载、作用域令牌连接、只读查询实测通过，此后多次退出未再崩溃；写入已实测蓝图创建与复制、组件、批量写图表、删节点与函数、生成 Actor、编辑器内 Python、PIE 与截图，其他写入首次使用后需检查结果 |

2026-10-25 起：修复编辑器退出时 Native MCP 关闭竞态导致的偶发崩溃（见 [UPSTREAM.md](UPSTREAM.md) 第 7 条）；请求头 helper 优先使用作用域令牌，配置见 [Claude Code 配置](docs/claude-code.md) 的「作用域令牌」。

实际 Windows 构建使用 5.4.4，描述符兼容标记为 5.4.0；定制引擎或其他版本应重新编译。

- [其他 Agent 如何连接并操作](docs/agent-guide.md)
- [Codex 配置与认证](docs/codex.md)
- [Claude Code 配置与认证](docs/claude-code.md)
- [Windows / Mac 编译方法](docs/build.md)
- [来源、兼容修改与验证范围](UPSTREAM.md)
