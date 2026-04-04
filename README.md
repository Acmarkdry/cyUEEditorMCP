# cyUEEditorMCP

> 基于 [lilklon/UEBlueprintMCP](https://github.com/lilklon/UEBlueprintMCP) 和 [Acmarkdry/UEEditorMCP](https://github.com/Acmarkdry/UEEditorMCP) 扩展，感谢两位作者的贡献。

面向 AI 辅助开发的 MCP 插件，用于 Unreal Engine 5.6+ 编辑器操作。

**10 个 MCP 工具 · ~95 个 C++ 动作 + Python exec · 异步执行 · Commandlet CLI · 持久 TCP 连接 · 仅限编辑器**

---

## 功能特性

- **10 个工具** — 包括新增的 `ue_python_exec`（任意 Python 代码执行）和 `ue_async_run`（异步提交/轮询）
- **~95 个 C++ 动作** — 图节点、材质分析、UMG/MVVM、AnimGraph、增强输入等复杂操作
- **ue_python_exec** — 通过 `import unreal` 直接调用 UE Python API，替代 45+ 原有 C++ 动作（Actor 管理、蓝图创建/编译、材质操作、视口控制、PIE 等）
- **异步执行** — `ue_async_run` 支持 submit/poll 模式，适合长时间运行的操作
- **Commandlet 模式** — `UEEditorMCP` Commandlet 支持 CLI/CI 批量执行
- **批量执行** — `ue_batch` 单次 TCP 往返执行最多 50 个动作
- **Skill 系统** — 按领域分组加载 action schema，减少 AI 上下文消耗
- **崩溃保护** — SEH + C++ 异常双重防护
- **自动保存** — 每次成功操作后自动保存脏包

## 动作域

| 域 | 数量 | 说明 |
|----|------|------|
| `python.exec` | 1 | Python 代码执行（替代 45+ C++ 动作） |
| `graph.*` | 18 | 图连线、检视、补丁、折叠重构 |
| `node.*` | 19 | 蓝图图节点创建 |
| `variable.*` | 8 | 变量管理 |
| `material.*` | 14 | 材质分析、诊断、布局（创建/编译→Python） |
| `widget.*` | 21 | UMG 控件 + MVVM |
| `animgraph.*` | 18 | AnimGraph 读取/创建/修改/编译 |
| `editor.*` | 8 | 日志、缩略图、源码管理 diff（Actor/PIE→Python） |
| `blueprint.*` | 2 | 蓝图内省（创建/编译→Python） |
| 其他 | ~10 | function/dispatcher/layout/input/macro |

完整动作列表见 [docs/actions.md](docs/actions.md)。

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

详细安装步骤见 [docs/installation.md](docs/installation.md)。

## 文档

| 文档 | 说明 |
|------|------|
| [docs/actions.md](docs/actions.md) | 动作域完整表格、AI 工作流、编译诊断 |
| [docs/architecture.md](docs/architecture.md) | 架构图、技术细节、关键文件 |
| [docs/installation.md](docs/installation.md) | 安装配置、日志工具 |
| [docs/development.md](docs/development.md) | 新增动作指南、测试 |
| [docs/devplan.md](docs/devplan.md) | 详细开发计划 |

## 许可证

MIT