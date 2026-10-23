# 编译 UE 5.4

Windows 需要 UE 5.4、VS2022 C++ 和匹配 SDK；Mac 需要真实 Mac、UE 5.4 与 UE 支持的 Xcode。Windows 不能直接产出这里需要的 Mac 插件二进制。

先克隆仓库，把下面路径换成实际绝对路径。输出目录必须是新的独立目录。

Windows PowerShell：

```powershell
& 'C:/Program Files/Epic Games/UE_5.4/Engine/Build/BatchFiles/RunUAT.bat' BuildPlugin '-Plugin=D:/tools/enginewire-ue54/plugins/EngineWire/EngineWire.uplugin' '-Package=D:/Builds/EngineWire-UE54-Win64' -TargetPlatforms=Win64 -VS2022
```

Mac 终端：

```sh
xcode-select -p
"/Users/Shared/Epic Games/UE_5.4/Engine/Build/BatchFiles/RunUAT.sh" BuildPlugin -Plugin="$HOME/tools/enginewire-ue54/plugins/EngineWire/EngineWire.uplugin" -Package="$HOME/Builds/EngineWire-UE54-Mac" -TargetPlatforms=Mac
```

Mac 命令尚未实机验证，架构由 UE/Xcode 配置决定；对生成 dylib 执行 lipo -info，不能未验证就标记 Universal。实际加载并验证 MCP 后再打包上传。不要把 Windows DLL 复制为 Mac 产物。

发布包保留 EngineWire.uplugin、Source、Config、Resources、上游 Content（如存在）、Binaries 下运行所需文件及许可证，排除 Intermediate、Saved、PDB、dSYM 和用户资产。ZIP 根目录应为 EngineWire，生成 SHA-256 校验。

若 UAT 被引擎内无关插件规则阻断，先查日志定位，不删除其他插件；当前 Windows 发布的回退构建情况见 UPSTREAM.md。
