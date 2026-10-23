# 来源、改动与验证

上游：https://github.com/ChiR24/Unreal_mcp
固定版本：v0.6.0-beta-f，MIT 许可证。非官方构建。

UE 5.4 修改：

1. McpSystemMaterialUsage.cpp、McpAssetMeshMatSlots.h 显式包含 Engine/SkinnedAssetCommon.h。
2. McpAudioMetaSoundNodesConnect.cpp 新前端输出别名重试受 MCP_HAS_METASOUND_FRONTEND_V2 条件保护。
3. McpPCGGraph.cpp 两处 GetNodeTitle(EPCGNodeTitleType) 版本判断从 5.5 起改为 5.4 起。
4. 按上游 UE 5.2+ 打包策略使用必需 PCG 依赖；Installed=true，EngineVersion=5.4.0，移除 EnabledByDefault。

2026-10-23 Windows 构建环境：UE 5.4.4，MSVC 14.38.33145，SDK 10.0.18362.0。引擎与插件 BuildId 33043543。RunUAT 因本机无关 LightForge 插件规则扫描异常未完整通过；使用其生成的隔离 HostProject 直接 UBT 编译，参数 -DisablePlugin=LightForge，仅排除该插件而不修改其文件。UBT 退出 0，EngineWire、EngineWireFab DLL 链接成功，再整理发布包。

已实测编辑器加载、Native MCP 与 Codex 连接、随机巡逻蓝图、蓝色门材质、蓝白 Niagara 拖尾。验证仅代表这些操作，不能代表上游所有能力。Mac 尚未编译或验证。本仓库不上传测试资产、私有场景、模型、专用动画、截图、认证文件或本机完整日志。
