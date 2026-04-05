# Level Sequence 过场动画工作流

## 创建 Level Sequence

```
ue_actions_run(action="sequencer.create", params={
    "sequence_name": "LS_Intro",
    "package_path": "/Game/Cinematics"
})
```

## 查看序列信息

```
ue_actions_run(action="sequencer.describe", params={"sequence_path": "/Game/Cinematics/LS_Intro"})
```

返回绑定列表、轨道数量、播放范围等。

## 绑定场景中的 Actor

```
ue_actions_run(action="sequencer.add_possessable", params={
    "sequence_path": "/Game/Cinematics/LS_Intro",
    "actor_name": "BP_MainCharacter"
})
```

将关卡中的 Actor 绑定到 Sequence，后续可为其添加动画轨道。

## 添加轨道

```
ue_actions_run(action="sequencer.add_track", params={
    "sequence_path": "/Game/Cinematics/LS_Intro",
    "binding_name": "BP_MainCharacter",
    "track_type": "Transform"
})
```

支持的轨道类型：Transform、Visibility、SkeletalAnimation 等。

## 设置播放范围

```
ue_actions_run(action="sequencer.set_range", params={
    "sequence_path": "/Game/Cinematics/LS_Intro",
    "start_frame": 0,
    "end_frame": 300
})
```

## 典型工作流

```
1. sequencer.create       → 创建 Sequence 资产
2. sequencer.add_possessable → 绑定场景 Actor
3. sequencer.add_track    → 为绑定添加动画轨道
4. sequencer.set_range    → 设置播放范围
5. sequencer.describe     → 确认最终状态
```

## 提示

- 绑定 Actor 前确保 Actor 已在当前关卡中
- 帧数基于 Sequence 的帧率（通常 30fps），300 帧 = 10 秒
- 用 `describe` 随时检查绑定和轨道状态
