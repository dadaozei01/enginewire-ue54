# Claude Code 配置

Claude Code（命令行与桌面版 Code 标签共用同一份配置）可直接连接 Native MCP，不需要上游 Node 代理。本仓库的 scripts/mcp-headers.cjs 通过 headersHelper 在每次连接时从工程读取令牌，配置里不保存令牌副本。需要 Node 18+。

## 前提

1. UE 已启用 EngineWire 和 Native MCP Server 并重启，状态栏显示 MCP :3000。
2. 目标工程存在 Saved/MCP/capability-token。
3. 把 scripts/mcp-headers.cjs 放到固定位置（仓库克隆目录或单独复制一份），不要放在临时目录。

## 添加

下面的绝对路径换成实际路径。工程参数可以是 .uproject 或工程目录，路径含中文或空格时保留引号。

Windows PowerShell：

```powershell
$helper = 'D:/tools/enginewire-ue54/scripts/mcp-headers.cjs'
$proj = 'D:/UE/MyProject/MyProject.uproject'
$cfg = [ordered]@{ type = 'http'; url = 'http://127.0.0.1:3000/mcp'; headersHelper = "node `"$helper`" `"$proj`"" } | ConvertTo-Json -Compress
claude mcp add-json unreal $cfg --scope user
```

PowerShell 手写 JSON 引号容易出错，用 ConvertTo-Json 生成更稳妥。

Mac 终端：

```sh
claude mcp add-json unreal '{"type":"http","url":"http://127.0.0.1:3000/mcp","headersHelper":"node \"$HOME/tools/enginewire-ue54/scripts/mcp-headers.cjs\" \"$HOME/UE/MyProject/MyProject.uproject\""}' --scope user
```

单引号内 $HOME 不会展开，请改成实际绝对路径。若 Claude Code 找不到 node，把 node 换成 which node 输出的绝对路径。

没有 Node 时用同目录的 scripts/mcp-headers.py，macOS 自带的 /usr/bin/python3 即可：

```sh
claude mcp add-json unreal '{"type":"http","url":"http://127.0.0.1:3000/mcp","headersHelper":"/usr/bin/python3 \"/Users/you/tools/enginewire-ue54/scripts/mcp-headers.py\" \"/Users/you/UE/MyProject/MyProject.uproject\""}' --scope user
```

## 作用域

- --scope user：所有目录可用，适合长期只连一个工程。
- --scope project：写入当前目录的 .mcp.json，适合每个 UE 工程各自配置。该文件含本机绝对路径，不要提交到公共仓库。
- 切换 UE 工程时令牌不同，需要改 headersHelper 中的工程路径；先 claude mcp remove unreal -s user 再重新添加。

## 作用域令牌

Saved/MCP/capability-token 是旧式全权限（Admin）令牌，插件日志会提示已弃用。helper 优先读取 Saved/MCP/scoped-capability-token，不存在时才回退旧令牌。作用域令牌只能取 Read、Write、Destructive，不能取 Admin；编辑器内 Python 只需 Write。

1. 关闭 UE。编辑器运行时修改 Saved/Config 下的 ini 可能在退出时被覆盖，插件也只在启动时读取令牌配置。
2. 生成 64 位十六进制令牌，写入 Saved/MCP/scoped-capability-token，权限 600。
3. 在 Saved/Config/<平台>Editor/Game.ini（Mac 为 MacEditor，Windows 为 WindowsEditor）追加下面一段，Token 换成同一个值。放在 Saved 而不是 Config/DefaultGame.ini，避免令牌随工程配置分发：

```ini
[/Script/EngineWire.EngineWireSettings]
ScopedCapabilityTokens=(Profile="claude-code",Token="<64位十六进制>",Scopes=(Read,Write,Destructive))
```

Saved 下的 ini 按普通读取加载，不处理 + / - 数组前缀，写成 +ScopedCapabilityTokens 会被当作另一个键而不生效；多个令牌就写多行同名的 ScopedCapabilityTokens=。

4. 启动 UE 后重新连接 MCP。日志不再出现 deprecated all-or-nothing capability token 警告即生效。

旧令牌文件仍然有效，插件会在文件缺失时重新生成，不要靠删除它来吊销。

## 验证

```sh
claude mcp get unreal
```

显示 Status: Connected 即连接成功。已打开的 Claude Code 会话需重开或在会话中执行 /mcp 重新连接，之后才会出现 unreal 工具。首先只读查询，例如“列出当前关卡的 Actor 统计”，确认 worldName 是目标工程后再写入。

## 不建议

- claude mcp add --transport http ... --header "X-MCP-Capability-Token: ..."：令牌以明文写入 ~/.claude.json，重新生成令牌后也会失效。
- 在终端直接运行 helper 查看输出：输出含认证头。

## 故障定位

Failed to connect：确认 UE 已启动且 3000 端口在监听，先启动 UE 再连接。401：工程路径指向了别的工程，或令牌已重新生成。helper 报 Cannot read MCP capability token：工程路径错误或 Native MCP 未启动。工具调用方式见 agent-guide.md。
