# cyUEEditorMCP

> 基于 [lilklon/UEBlueprintMCP](https://github.com/lilklon/UEBlueprintMCP) 和 [Acmarkdry/UEEditorMCP](https://github.com/Acmarkdry/UEEditorMCP) 扩展，感谢两位作者的贡献。

面向 AI 辅助开发的 MCP 插件，用于 Unreal Engine 5.6+ 编辑器操作。

**12 个 MCP 工具 · ~123 个 C++ 动作 + Python exec · 事件推送 · 异步执行 · 批量原子回滚 · 权限分级 · Commandlet CLI · 持久 TCP 连接 · 上下文记忆 · 仅限编辑器**

---

## 功能特性

- **12 个工具** — 包括 `ue_python_exec`（任意 Python 代码执行）、`ue_events`（编辑器事件推送）、`ue_async_run`（异步提交/轮询）和 `ue_context`（跨会话上下文记忆）
- **~123 个 C++ 动作** — 图节点、材质分析、UMG/MVVM、AnimGraph、Niagara 粒子、DataTable、Sequencer、性能分析、自动化测试等
- **ue_python_exec** — 通过 `import unreal` 直接调用 UE Python API，替代 45+ 原有 C++ 动作（Actor 管理、蓝图创建/编译、材质操作、视口控制、PIE 等）
- **事件推送** — `ue_events` 订阅编辑器事件（蓝图编译、资产变更、PIE 状态、Undo/Redo 等），轮询获取
- **异步执行** — `ue_async_run` 支持 submit/poll 模式，适合长时间运行的操作
- **批量原子回滚** — `ue_batch` 支持 `transactional` 模式，失败时自动撤销全部更改
- **权限分级** — `auto` / `confirm_destructive` / `readonly` 三级权限策略
- **Undo/Redo** — 所有写操作自动包裹 `FScopedTransaction`，支持 undo/redo/get_undo_history
- **视口截图** — 编辑器视口和 PIE 运行时截图，base64 返回
- **智能错误建议** — 找不到 Pin/资产时自动返回可用列表和相似建议
- **Commandlet 模式** — `UEEditorMCP` Commandlet 支持 CLI/CI 批量执行
- **Skill 系统** — 按领域分组加载 action schema，减少 AI 上下文消耗
- **崩溃保护** — SEH + C++ 异常双重防护
- **自动保存** — 每次成功操作后自动保存脏包
- **上下文记忆** — `ue_context` 工具自动记录操作历史和工作集，AI 新开对话一次 resume 即可恢复上次工作状态

## 动作域

| 域 | 数量 | 说明 |
|----|------|------|
| `python.exec` | 1 | Python 代码执行（替代 45+ C++ 动作） |
| `graph.*` | 18 | 图连线、检视、补丁、折叠重构 |
| `node.*` | 19 | 蓝图图节点创建 |
| `variable.*` | 8 | 变量管理 |
| `material.*` | 21 | 材质分析、诊断、Diff、参数提取、批量实例、节点替换、布局 |
| `widget.*` | 21 | UMG 控件 + MVVM |
| `animgraph.*` | 18 | AnimGraph 读取/创建/修改/编译 |
| `niagara.*` | 7 | Niagara 粒子系统创建/描述/发射器/模块/编译 |
| `datatable.*` | 5 | DataTable 创建/描述/行增删/JSON 导出 |
| `sequencer.*` | 5 | Level Sequence 创建/描述/绑定/轨道/范围 |
| `editor.*` | 13 | 日志、截图、Undo/Redo、缩略图、源码管理 diff |
| `test.*` | 2 | UE 自动化测试运行/列表 |
| `level.*` | 2 | Sublevel 列表、World Settings |
| `profiler.*` | 2 | 帧统计、内存统计 |
| `blueprint.*` | 2 | 蓝图内省（创建/编译→Python） |
| 其他 | ~10 | function/dispatcher/layout/input/macro |

完整动作列表见 [docs/actions.md](docs/actions.md)。

## MCP 工具列表

| # | 工具 | 说明 |
|---|------|------|
| 1 | `ue_ping` | 连接健康检查 |
| 2 | `ue_actions_search` | 按关键词/标签搜索动作 |
| 3 | `ue_actions_schema` | 获取动作完整 schema |
| 4 | `ue_actions_run` | 执行单个动作 |
| 5 | `ue_batch` | 批量执行（支持原子回滚） |
| 6 | `ue_resources_read` | 读取内嵌文档 |
| 7 | `ue_logs_tail` | 查看日志（Python/Editor/both） |
| 8 | `ue_skills` | 按领域加载 action catalog |
| 9 | `ue_python_exec` | 在 UE 内嵌 Python 中执行代码 |
| 10 | `ue_events` | 订阅/轮询编辑器事件推送 |
| 11 | `ue_async_run` | 异步提交/轮询 |
| 12 | `ue_context` | 跨会话上下文记忆 |

## 快速开始

```
1. 编译 C++ 插件（需要 Visual Studio 2022）
2. 运行 setup_mcp.ps1 配置 Python 环境
3. 在 Unreal Editor 中打开项目
4. 在 VS Code 中使用 GitHub Copilot Chat
```

### Commandlet 模式（CLI/CI）

```bash
UnrealEditor-Cmd.exe YourProject.uproject -run=UEEditorMCP -command=exec_python -params="{\"code\":\"import unreal; _result=unreal.SystemLibrary.get_engine_version()\"}" -json
```

### 运行时功能测试

```bash
cd Plugins/UEEditorMCP
python -m tests.test_runtime_e2e          # 全量 E2E 测试（需 UE Editor 运行）
python -m tests.test_runtime_e2e --list   # 查看所有测试分类
python -m tests.test_runtime_e2e -c ping editor p10_profiler  # 按分类运行
```

详细安装步骤见 [docs/installation.md](docs/installation.md)。

## 文档

| 文档 | 说明 |
|------|------|
| [docs/actions.md](docs/actions.md) | 动作域完整表格、AI 工作流、编译诊断 |
| [docs/architecture.md](docs/architecture.md) | 架构图、技术细节、关键文件 |
| [docs/installation.md](docs/installation.md) | 安装配置、日志工具 |
| [docs/development.md](docs/development.md) | 新增动作指南、测试 |
| [docs/devplan.md](docs/devplan.md) | 详细开发计划 |

## 版本历史

| 版本 | 日期 | 里程碑 |
|------|------|--------|
| v0.3.0 | 2026-04-06 | Phase 7-10 全量升级：+28 Action（~123 总计）、事件推送、批量原子回滚、权限分级、12 个 MCP 工具 |
| v0.2.0 | 2026-04-04 | Python exec、异步命令、Commandlet、ContextStore、CI |
| v0.1.0 | 初版 | 基础蓝图/材质/UMG/AnimGraph 操作 |

## 许可证

MIT