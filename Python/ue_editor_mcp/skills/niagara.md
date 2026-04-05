# Niagara 粒子系统工作流

## 快速开始：创建粒子系统

```
1. ue_actions_run(action="niagara.create_system", params={"system_name": "NS_Fire", "package_path": "/Game/FX"})
2. ue_actions_run(action="niagara.describe_system", params={"system_path": "/Game/FX/NS_Fire"})
3. ue_actions_run(action="niagara.add_emitter", params={"system_path": "/Game/FX/NS_Fire", "emitter_name": "Sparks"})
4. ue_actions_run(action="niagara.compile", params={"system_path": "/Game/FX/NS_Fire"})
```

## 查看模块堆栈

```
ue_actions_run(action="niagara.get_modules", params={"system_path": "/Game/FX/NS_Fire"})
```

返回所有发射器及其模块堆栈（Spawn、Update、Render）。

## 修改模块参数

```
ue_actions_run(action="niagara.set_module_param", params={
    "system_path": "/Game/FX/NS_Fire",
    "emitter_name": "Sparks",
    "module_name": "SpawnRate",
    "param_name": "SpawnRate",
    "value": 100.0
})
```

## 移除发射器

```
ue_actions_run(action="niagara.remove_emitter", params={
    "system_path": "/Game/FX/NS_Fire",
    "emitter_name": "Sparks"
})
```

## 提示

- 修改后务必 `compile`，验证系统是否有效
- 修改前先用 `describe_system` 查看当前状态
- 发射器名称在系统内必须唯一