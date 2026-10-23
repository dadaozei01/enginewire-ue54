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
