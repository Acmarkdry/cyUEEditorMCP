# 架构与技术细节

## 架构图

```
VS Code / MCP 客户端（GitHub Copilot 等）
        │
        │  7 个 MCP 工具（stdio）
        ▼
  server_unified.py（动作注册表 + 分发器）
        │
        │  TCP/JSON（端口 55558，持久连接，长度前缀帧）
        ▼
  C++ 插件（FMCPServer → 每连接一个 FMCPClientHandler）
        │
        │  游戏线程分发
        ▼
  FEditorAction 子类（~160 个处理器）→ 校验 → 执行 → 自动保存
        │
        ▼
  Unreal Editor
```

## C++ 服务器（`FMCPServer`）

- 监听 `127.0.0.1:55558`（仅限本地）
- 每个连接派生独立的 `FMCPClientHandler` 线程，最多 8 路并发
- `ping` / `close` 直接在客户端线程处理，其余命令通过 `AsyncTask + FEvent` 分发到游戏线程
- 客户端超时：120 秒无活动后断开
- 启用 `SO_REUSEADDR`，避免编辑器重启时端口冲突

## Python 服务器（`server_unified.py`）

- 7 个固定工具的单一 MCP 服务器
- 动作注册表含 159 个 ActionDef，支持关键字搜索和模式自省
- `ue_batch` 批量执行（每次最多 50 个动作，单次 TCP 往返）
- 命令日志环形缓冲区，供 `ue_logs_tail` 使用

## 通信协议

```
[4 字节：消息长度（大端序）] [UTF-8 JSON 载荷]
```

请求：
```json
{"type": "create_blueprint", "params": {"name": "BP_MyActor", "parent_class": "Actor"}}
```

响应：
```json
{"success": true, "blueprint_name": "BP_MyActor", "path": "/Game/Blueprints/BP_MyActor"}
```

## 编辑器专属安全保障

| 层级 | 机制 | 效果 |
|------|------|------|
| `.uplugin` | `"Type": "Editor"` | UBT 对所有非编辑器目标跳过此模块 |
| `.Build.cs` | 依赖 `UnrealEd`、`BlueprintGraph`、`Kismet`、`UMGEditor` 等 | 无法链接到游戏目标 |
| `.uplugin` | `"PlatformAllowList": ["Win64", "Mac", "Linux"]` | 仅限桌面编辑器平台 |

## 关键文件

| 文件 | 用途 |
|------|------|
| `Python/ue_editor_mcp/server_unified.py` | 单一 MCP 服务器，7 个工具，动作分发 |
| `Python/ue_editor_mcp/registry/__init__.py` | ActionRegistry 类，关键字搜索引擎 |
| `Python/ue_editor_mcp/registry/actions.py` | 159 个带模式/标签/示例的 ActionDef 条目 |
| `Python/ue_editor_mcp/connection.py` | `PersistentUnrealConnection`（TCP、心跳、自动重连） |
| `Source/Private/MCPServer.cpp` | TCP Accept 循环 + 每客户端处理线程 |
| `Source/Private/MCPBridge.cpp` | 动作处理器注册表（~160 条命令） |
| `Source/Private/Actions/AnimGraphActions.cpp` | AnimGraph 全部 18 个 Action |
| `Python/ue_editor_mcp/skills/animgraph.md` | AnimGraph 工作流提示 |
