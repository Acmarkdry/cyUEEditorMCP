---
inclusion: auto
---

# UE Editor MCP

本项目是一个 MCP 服务器，通过 TCP 桥接 AI 与 Unreal Engine 编辑器。

## 架构

```
Kiro ──MCP──▶ Python Server (8 tools) ──TCP──▶ UE Editor Plugin (141+ C++ actions)
```

- Python 端：`Python/ue_editor_mcp/`
- C++ 端：`Source/UEEditorMCP/`

## MCP 工具

| 工具 | 用途 |
|------|------|
| `ue_ping` | 连接检查 |
| `ue_actions_search` | 按关键词搜索 action |
| `ue_actions_schema` | 获取 action 的完整 schema |
| `ue_actions_run` | 执行单个 action |
| `ue_batch` | 批量执行（单次 TCP 往返） |
| `ue_resources_read` | 读取内嵌文档 |
| `ue_logs_tail` | 查看日志 |
| `ue_skills` | 按需加载领域 skill（含 action schema + workflow tips） |

## 工作流

当需要在 UE 编辑器中执行操作时：

1. `ue_skills(action="list")` — 查看可用 skill 领域
2. `ue_skills(action="load", skill_id="<id>")` — 加载对应领域的完整 action schema 和工作流提示
3. `ue_actions_run` 或 `ue_batch` — 根据 schema 执行操作

不要猜测 action 参数，先加载 skill 获取准确 schema。

## 通用约定

- Action ID 点号分隔：`domain.verb`（如 `blueprint.create`）
- Blueprint 修改后必须 `blueprint.compile`
- Material 修改后必须 `material.compile`
- 优先用 `ue_batch` 减少往返
- 节点位置 `[X, Y]`；Actor 位置 `[X, Y, Z]`；颜色 `[R, G, B, A]` 值域 0–1

## UE 编译环境

- UE 引擎目录：`D:\UnrealEngine5\UnrealEngine\`
- 项目目录：`D:\UnrealGame\Lyra_56\`
- 项目文件：`D:\UnrealGame\Lyra_56\Lyra_56.uproject`
- 插件目录（当前工作区）：`D:\UnrealGame\Lyra_56\Plugins\UEEditorMCP\`
- 引擎版本：UE 5.6（源码编译版）
- 编译目标：`LyraEditor`

### 编译命令

```
D:\UnrealEngine5\UnrealEngine\Engine\Build\BatchFiles\Build.bat LyraEditor Win64 Development "D:\UnrealGame\Lyra_56\Lyra_56.uproject" -WaitMutex
```

### C++ 修改后的工作流

1. 修改 C++ 代码
2. 执行编译命令，过滤错误输出：`... 2>&1 | Select-Object -Last 60`
3. 如果有编译错误，分析并修复
4. 重复直到编译通过（`Result: Succeeded`）
5. 提交代码

### 版本兼容注意事项

本插件代码基于 UE 5.7 API 编写，运行在 UE 5.6 上，需注意：
- 使用 `ENGINE_MAJOR_VERSION` / `ENGINE_MINOR_VERSION` 条件编译处理 API 差异
- 5.7 新增的枚举值（如 `EDiffType::CUSTOM_OBJECT`）需要 `#if` 包裹
- 5.7 改签名的 API（如 `GetMaterialResource`）需要提供 5.6 的替代调用
- 编译错误中出现 "is not a member of" 或 "no overloaded function" 通常是版本差异
