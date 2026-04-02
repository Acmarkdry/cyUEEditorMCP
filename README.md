# cyUEEditorMCP

> 基于 [lilklon/UEBlueprintMCP](https://github.com/lilklon/UEBlueprintMCP) 和 [yangskin/UEEditorMCP](https://github.com/yangskin/UEEditorMCP) 扩展，感谢两位作者的贡献。

面向 AI 辅助开发的 MCP 插件，用于 Unreal Engine 5.6+ 编辑器操作。

**7 个固定 MCP 工具 · 159 个动作 · 持久 TCP 连接 · 仅限编辑器**

---

## 功能特性

- **7 个固定工具** — 工具接口永远不变，动作通过注册表动态扩展
- **159 个动作** — 蓝图、图节点、材质、UMG/MVVM、AnimGraph、增强输入、PIE、Outliner 等
- **批量执行** — `ue_batch` 单次 TCP 往返执行最多 50 个动作
- **Skill 系统** — 按领域分组加载 action schema，减少 AI 上下文消耗
- **崩溃保护** — SEH + C++ 异常双重防护
- **自动保存** — 每次成功操作后自动保存脏包

## 动作域

| 域 | 数量 | 说明 |
|----|------|------|
| `blueprint.*` | 11 | 蓝图增删改查、组件、接口 |
| `graph.*` | 18 | 图连线、检视、补丁、折叠重构 |
| `node.*` | 19 | 蓝图图节点创建 |
| `variable.*` | 8 | 变量管理 |
| `material.*` | 16 | 材质创建、编辑、编译 |
| `widget.*` | 21 | UMG 控件 + MVVM |
| `animgraph.*` | 18 | AnimGraph 读取/创建/修改/编译 |
| `editor.*` | 18 | 关卡、资产、日志、PIE、Outliner |
| 其他 | 30 | function/dispatcher/layout/input/batch/component/macro |

完整动作列表见 [docs/actions.md](docs/actions.md)。

## 快速开始

```
1. 编译 C++ 插件（需要 Visual Studio 2022）
2. 运行 setup_mcp.ps1 配置 Python 环境
3. 在 Unreal Editor 中打开项目
4. 在 VS Code 中使用 GitHub Copilot Chat
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
