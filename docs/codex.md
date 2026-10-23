# Codex 配置

原生 HTTP 本身不需要 Node。本仓库的动态请求头 helper 使用 Node 18+，从工程读取令牌，不保存副本。将下列绝对路径换成实际路径：

```toml
[mcp_servers.unreal_engine]
url = "http://127.0.0.1:3000/mcp"
http_headers_helper = '"C:/Program Files/nodejs/node.exe" "D:/tools/enginewire-ue54/scripts/mcp-headers.cjs" "D:/UE/MyProject/MyProject.uproject"'
startup_timeout_sec = 30
tool_timeout_sec = 120
```

Mac 将命令内的 Node、脚本、工程路径替换为实际绝对路径即可。工程参数也可以是工程目录；省略参数时读取 UE_PROJECT_PATH 环境变量。helper 标准输出含认证头，仅供 MCP 客户端读取，不要在终端直接运行展示输出。

UE 先启动 Native MCP，再重启 Codex，确认 unreal 工具出现后进行只读查询。其他 Agent 配置相同 HTTP 地址和动态请求头即可；如果不支持动态请求头，使用上游匹配版本的代理，参考上游文档，不要混用旧稳定版服务端。

参考：[Codex MCP](https://developers.openai.com/codex/mcp/)、[配置参考](https://developers.openai.com/codex/config-reference/)。
