# DataTable 数据表工作流

## 创建数据表

```
ue_actions_run(action="datatable.create", params={
    "table_name": "DT_Weapons",
    "package_path": "/Game/Data",
    "row_struct": "/Script/MyGame.WeaponData"
})
```

## 查看表结构

```
ue_actions_run(action="datatable.describe", params={"table_path": "/Game/Data/DT_Weapons"})
```

返回列名、类型、行数、行结构体路径。

## 添加行

```
ue_actions_run(action="datatable.add_row", params={
    "table_path": "/Game/Data/DT_Weapons",
    "row_name": "Sword_01",
    "row_data": {"Damage": 50, "AttackSpeed": 1.2, "DisplayName": "铁剑"}
})
```

## 删除行

```
ue_actions_run(action="datatable.delete_row", params={
    "table_path": "/Game/Data/DT_Weapons",
    "row_name": "Sword_01"
})
```

## 导出为 JSON

```
ue_actions_run(action="datatable.export_json", params={"table_path": "/Game/Data/DT_Weapons"})
```

返回完整表格，以行名为键的 JSON 对象。

## 提示

- `row_struct` 必须是有效的 UScriptStruct 路径（如 `/Script/Engine.DataTableRowHandle`）
- 行数据的字段名和类型必须匹配结构体的属性
- 添加行之前先用 `describe` 查看可用列