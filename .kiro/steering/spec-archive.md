# Spec 归档约定

## 目录结构

- 活跃 spec：`.kiro/specs/{feature-name}/`
- 已归档 spec：`.kiro/specs/_archived/{feature-name}/`
- 归档索引：`.kiro/specs/_archived/index.md`

## 归档前置条件

1. `tasks.md` 所有必需任务（非 `*` 标注）均已标记 `[x]`
2. 代码已通过测试
3. 已提交到 git

## 归档步骤

```powershell
$feature = "my-feature-name"
New-Item -ItemType Directory -Path ".kiro/specs/_archived/$feature" -Force
Move-Item ".kiro/specs/$feature/*" ".kiro/specs/_archived/$feature/"
Remove-Item ".kiro/specs/$feature" -Recurse -Force
```

然后在 `.kiro/specs/_archived/index.md` 追加一行：

```
| feature-name | YYYY-MM-DD | 简述 | commit-hash |
```

## 归档索引格式

```markdown
| Feature | 归档日期 | 说明 | Commit |
|---------|---------|------|--------|
| animation-graph-read | 2026-04-02 | AnimGraph MCP 18 个 Action | c4fda9e |
```
