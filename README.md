# EngineWire UE 5.4

基于 [ChiR24/Unreal_mcp v0.6.0-beta-f](https://github.com/ChiR24/Unreal_mcp/releases/tag/v0.6.0-beta-f) 的 UE 5.4 兼容源码、Windows 编译包与 Agent 接入说明。非上游官方发行，保留 MIT 许可证。此仓库不包含用户工程或测试资产。

## 安装

从 [Releases](https://github.com/dadaozei01/enginewire-ue54/releases) 下载 Windows ZIP 与 SHA256SUMS.txt。关闭 UE，将 ZIP 内 EngineWire 文件夹放到以下一个位置：

- 工程级：<工程>/Plugins/EngineWire
- 公共插件：<UE_5.4>/Engine/Plugins/Marketplace/EngineWire

正确层级是 EngineWire/EngineWire.uplugin，不要双重嵌套。备份旧版本，避免工程级与引擎级重复安装同名插件。打开 UE，在插件窗口启用 EngineWire，然后在 Project Settings → Plugins → EngineWire 勾选 Enable Native MCP Server，重启编辑器。

状态显示 MCP :3000 (0) 表示服务已启动，0 是连接客户端数量。勾选后仍显示 off 时先确认重启。

## 平台状态

| 平台 | 状态 |
|---|---|
| Windows x64 / UE 5.4.4 | 已编译、加载，MCP 与蓝图/材质/Niagara 操作实测通过 |
| Mac / UE 5.4 | 仅提供源码与编译说明，尚未编译或验证 |

实际 Windows 构建使用 5.4.4，描述符兼容标记为 5.4.0；定制引擎或其他版本应重新编译。

- [其他 Agent 如何连接并操作](docs/agent-guide.md)
- [Codex 配置与认证](docs/codex.md)
- [Claude Code 配置与认证](docs/claude-code.md)
- [Windows / Mac 编译方法](docs/build.md)
- [来源、兼容修改与验证范围](UPSTREAM.md)
