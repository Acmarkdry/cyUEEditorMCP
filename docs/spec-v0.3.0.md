# UECliTool v0.3.0 功能规格说明书

> **版本**: v0.3.0  
> **日期**: 2026-04-09  
> **作者**: AI Spec (review by yangskin)  
> **目标引擎**: Unreal Engine 5.6  
> **发布策略**: 一次性全部实现，一个大版本发布  
> **文档语言**: 中文撰写，代码注释英文  

---

## 目录

1. [概述](#1-概述)
2. [功能模块一：资产管理增强 (asset.*)](#2-功能模块一资产管理增强)
3. [功能模块二：C++ 类反射查询 (reflection.*)](#3-功能模块二c-类反射查询)
4. [功能模块三：Content Browser 增强 (browser.*)](#4-功能模块三content-browser-增强)
5. [功能模块四：Sequencer 增强 (sequencer.*)](#5-功能模块四sequencer-增强)
6. [功能模块五：Level/World 增强 (level.*)](#6-功能模块五levelworld-增强)
7. [功能模块六：蓝图调试支持 (debug.*)](#7-功能模块六蓝图调试支持)
8. [功能模块七：Niagara 增强 (niagara.*)](#8-功能模块七niagara-增强)
9. [功能模块八：响应格式化层 (Response Formatter)](#9-功能模块八响应格式化层)
10. [功能模块九：Live Coding 支持 (livecoding.*)](#10-功能模块九live-coding-支持)
11. [测试计划](#11-测试计划)
12. [文档更新计划](#12-文档更新计划)
13. [实现路线图](#13-实现路线图)
14. [风险与缓解](#14-风险与缓解)

---

## 1. 概述

### 1.1 背景

UECliTool v0.2.0 已实现 200 个命令，覆盖 17 个领域。通过对日常 UE 开发工作流的分析，
识别出 7 个高价值功能缺口 + 2 个架构级优化。本次 v0.3.0 将一次性补齐这些功能。

### 1.2 新增命令/功能概览

| 模块 | 新增命令数 | 领域 | 类型 |
|------|-----------|------|------|
| 资产管理增强 | 4 | `asset.*` | C++ Action |
| C++ 类反射查询 | 3 | `reflection.*` | C++ Action |
| Content Browser 增强 | 3 | `browser.*` | C++ Action |
| Sequencer 增强 | 5 | `sequencer.*` | C++ Action |
| Level/World 增强 | 4 | `level.*` | C++ Action |
| 蓝图调试支持 | 4 | `debug.*` | C++ Action |
| Niagara 增强 | 4 | `niagara.*` | C++ Action |
| 响应格式化层 | — | Python 中间层 | 架构优化 |
| Live Coding 支持 | 3 | `livecoding.*` | C++ Action |
| **合计** | **30 命令 + 1 架构优化** | | |

### 1.3 设计原则

- **CLI-in, CLI-out**: 输入为 CLI 语法，输出也采用紧凑的 CLI 风格文本（非 JSON）
- **只读无副作用**: 查询类命令标记 `RequiresSave() = false`
- **事务包装**: 所有写操作包装在 UE Transaction 中，支持 Undo
- **渐进式返回**: 支持 `detail_level` 或 `compact` 参数控制输出大小
- **错误安全**: 返回结构化错误信息，不崩溃编辑器
- **Token 节约**: Python 中间层格式化响应，写操作返回一行摘要，查询操作返回表格

---

## 2. 功能模块一：资产管理增强

### 2.1 动机

当前只有 `list_assets`、`rename_assets`，缺少复制、删除、移动等核心资产生命周期操作。
用户需要频繁在 Content Browser 中手动操作，无法自动化批量资产管理。

### 2.2 新增命令

#### 2.2.1 `duplicate_asset` (asset.duplicate)

**描述**: 复制一个或多个资产到指定路径。

```
duplicate_asset /Game/Blueprints/BP_Player --destination /Game/Blueprints/Backup
duplicate_asset /Game/Blueprints/BP_Player --new_name BP_Player_Copy
```

**参数**:

| 参数 | 类型 | 必填 | 说明 |
|------|------|------|------|
| `asset_path` | string | ✅ | 源资产路径 |
| `destination` | string | ❌ | 目标目录（默认同目录） |
| `new_name` | string | ❌ | 新名称（默认原名_Copy） |
| `items` | array | ❌ | 批量模式：`[{asset_path, destination, new_name}]` |

**CLI 响应示例**:
```
✓ duplicated BP_Player → /Game/Blueprints/Backup/BP_Player (Blueprint)
```

批量模式:
```
✓ duplicated 3 assets:
  BP_Player  → /Game/Backup/BP_Player
  BP_Enemy   → /Game/Backup/BP_Enemy
  BP_Item    → /Game/Backup/BP_Item
```

**C++ 实现要点**:
- 使用 `UEditorAssetLibrary::DuplicateAsset()` 或 `AssetTools.DuplicateAsset()`
- 支持单个和批量模式（`items[]`）
- 自动处理名称冲突（追加 `_1`, `_2` 后缀）
- 包装在 FScopedTransaction 中

#### 2.2.2 `delete_asset` (asset.delete)

**描述**: 安全删除资产，先检查引用关系。

```
delete_asset /Game/Blueprints/BP_Old
delete_asset /Game/Blueprints/BP_Old --force --fix_redirectors
```

**参数**:

| 参数 | 类型 | 必填 | 说明 |
|------|------|------|------|
| `asset_path` | string | ✅ | 要删除的资产路径 |
| `force` | bool | ❌ | 强制删除（忽略引用检查），默认 false |
| `fix_redirectors` | bool | ❌ | 删除后修复重定向器，默认 true |
| `items` | array | ❌ | 批量模式 |

**CLI 响应示例**:
```
✓ deleted /Game/Blueprints/BP_Old (1 redirector fixed)
```

被引用时拒绝删除:
```
✗ cannot delete /Game/Blueprints/BP_Old — referenced by:
  /Game/Maps/Level_01 (Hard)
  /Game/Blueprints/BP_Enemy (Hard)
  hint: use --force to override
```

**C++ 实现要点**:
- 先调用 `AssetRegistry.GetReferencers()` 检查引用
- 有引用时默认拒绝删除，返回引用者列表
- `force=true` 时使用 `ObjectTools::DeleteAssets()` 强制删除
- 删除后可选调用 `FixupReferencers()` 修复重定向器

#### 2.2.3 `move_asset` (asset.move)

**描述**: 移动资产到新目录（相当于 Content Browser 的拖拽移动）。

```
move_asset /Game/Blueprints/BP_Player --destination /Game/Characters
```

**参数**:

| 参数 | 类型 | 必填 | 说明 |
|------|------|------|------|
| `asset_path` | string | ✅ | 源资产路径 |
| `destination` | string | ✅ | 目标目录 |
| `items` | array | ❌ | 批量模式 |

**C++ 实现要点**:
- 使用 `UEditorAssetLibrary::RenameAsset()` (移动 = 改路径)
- 自动创建目标目录（如不存在）
- 自动修复重定向器

#### 2.2.4 `fix_redirectors` (asset.fix_redirectors)

**描述**: 扫描并修复指定目录下的所有重定向器。

```
fix_redirectors /Game/Blueprints
fix_redirectors /Game --recursive
```

**参数**:

| 参数 | 类型 | 必填 | 说明 |
|------|------|------|------|
| `path` | string | ✅ | 要扫描的内容路径 |
| `recursive` | bool | ❌ | 是否递归子目录，默认 true |

**C++ 实现要点**:
- 使用 `AssetRegistry` 搜索 `UObjectRedirector` 类型
- 调用 `AssetToolsModule.FixupReferencers()` 修复
- 返回修复数量和失败列表

---

## 3. 功能模块二：C++ 类反射查询

### 3.1 动机

AI 在操作蓝图时经常需要知道"某个类有哪些属性"、"哪些类继承自 Actor"，
目前只能通过 `exec_python` 间接实现，且输出未结构化。
专用反射查询命令可以大幅提升 AI 的类型理解能力。

### 3.2 新增命令

#### 3.2.1 `list_classes` (reflection.list_classes)

**描述**: 列出继承自某基类的所有 UClass。

```
list_classes Actor
list_classes ActorComponent --include_abstract --max_depth 2
```

**参数**:

| 参数 | 类型 | 必填 | 说明 |
|------|------|------|------|
| `base_class` | string | ✅ | 基类名称 |
| `include_abstract` | bool | ❌ | 包含抽象类，默认 false |
| `include_native_only` | bool | ❌ | 只包含 C++ 类，默认 false |
| `max_depth` | int | ❌ | 最大继承深度，默认 -1 (无限) |
| `name_filter` | string | ❌ | 名称过滤（支持通配符） |

**CLI 响应示例**:
```
Actor subclasses (42 total, depth≤2):
  [1] StaticMeshActor         /Script/Engine.StaticMeshActor
  [1] SkeletalMeshActor       /Script/Engine.SkeletalMeshActor
  [1] Character               /Script/Engine.Character
  [2]   PlayerCharacter       /Script/MyGame.PlayerCharacter
  ...
```

**C++ 实现要点**:
- 使用 `GetDerivedClasses()` 或遍历 `TObjectIterator<UClass>`
- 通过 `Class->IsChildOf()` 和 `HasAnyClassFlags(CLASS_Abstract)` 过滤
- 限制返回数量防止过大响应

#### 3.2.2 `get_class_properties` (reflection.get_class_properties)

**描述**: 获取一个 UClass 的所有 UPROPERTY 列表。

```
get_class_properties Character
get_class_properties BP_Player --include_inherited --category Movement
```

**参数**:

| 参数 | 类型 | 必填 | 说明 |
|------|------|------|------|
| `class_name` | string | ✅ | 类名或蓝图名 |
| `include_inherited` | bool | ❌ | 包含继承属性，默认 false |
| `category` | string | ❌ | 按分类过滤 |
| `include_meta` | bool | ❌ | 包含元数据，默认 false |

**CLI 响应示例**:
```
Character properties (15 total):
  NAME                TYPE      CATEGORY    FLAGS                          DEFAULT  OWNER
  MaxWalkSpeed        Float     Movement    EditAnywhere|BlueprintReadWrite 600.0   CharacterMovementComponent
  JumpZVelocity       Float     Movement    EditAnywhere|BlueprintReadWrite 420.0   CharacterMovementComponent
  bIsCrouched         Bool      Movement    BlueprintReadOnly              false    Character
  ...
```

**C++ 实现要点**:
- 使用 `TFieldIterator<FProperty>` 遍历属性
- 通过 `Property->HasAnyPropertyFlags()` 获取标志
- 支持 Blueprint GeneratedClass 的属性查询
- 通过 `Property->GetMetaData()` 获取元数据

#### 3.2.3 `get_class_functions` (reflection.get_class_functions)

**描述**: 获取一个 UClass 的所有 UFUNCTION 列表。

```
get_class_functions Character
get_class_functions BP_Player --include_inherited --callable_only
```

**参数**:

| 参数 | 类型 | 必填 | 说明 |
|------|------|------|------|
| `class_name` | string | ✅ | 类名或蓝图名 |
| `include_inherited` | bool | ❌ | 包含继承函数，默认 false |
| `callable_only` | bool | ❌ | 只返回可在蓝图中调用的，默认 false |
| `name_filter` | string | ❌ | 名称过滤 |

**CLI 响应示例**:
```
Character functions (8 callable):
  NAME            RETURN  PARAMS                FLAGS                   OWNER
  Jump            void    ()                    BlueprintCallable|Native Character
  StopJumping     void    ()                    BlueprintCallable|Native Character
  Crouch          void    (bClientSimulation)   BlueprintCallable|Native Character
  LaunchCharacter void    (LaunchVelocity,...)  BlueprintCallable|Native Character
  ...
```

**C++ 实现要点**:
- 使用 `TFieldIterator<UFunction>` 遍历
- 通过 `FUNC_BlueprintCallable` 等标志过滤
- 解析函数参数：遍历 `TFieldIterator<FProperty>(Function)`

---

## 4. 功能模块三：Content Browser 增强

### 4.1 动机

AI 需要理解资产之间的依赖关系以做出正确决策（如删除前检查引用），
也需要主动创建文件夹来组织资产，以及验证资产完整性。

### 4.2 新增命令

#### 4.2.1 `create_folder` (browser.create_folder)

**描述**: 在 Content Browser 中创建文件夹。

```
create_folder /Game/Characters/Enemies
```

**参数**:

| 参数 | 类型 | 必填 | 说明 |
|------|------|------|------|
| `path` | string | ✅ | 要创建的目录路径 |

**C++ 实现要点**:
- 使用 `IFileManager::Get().MakeDirectory()` 创建磁盘目录
- 调用 `FAssetRegistryModule::AssetCreated()` 通知 AssetRegistry
- 支持递归创建多级目录

#### 4.2.2 `get_asset_references` (browser.get_asset_references)

**描述**: 查询资产的引用和被引用关系。

```
get_asset_references /Game/Blueprints/BP_Player
get_asset_references /Game/Blueprints/BP_Player --direction both --recursive
```

**参数**:

| 参数 | 类型 | 必填 | 说明 |
|------|------|------|------|
| `asset_path` | string | ✅ | 资产路径 |
| `direction` | string | ❌ | `dependencies` / `referencers` / `both`，默认 `both` |
| `recursive` | bool | ❌ | 递归查询，默认 false |
| `max_depth` | int | ❌ | 递归最大深度，默认 3 |

**CLI 响应示例**:
```
/Game/Blueprints/BP_Player references:

  DEPENDENCIES (3):
    /Game/Meshes/SK_Player          Hard
    /Game/Animations/ABP_Player     Hard
    /Game/Textures/T_PlayerIcon     Soft

  REFERENCERS (2):
    /Game/Maps/Level_01             Hard
    /Game/Blueprints/BP_GameMode    Hard
```

**C++ 实现要点**:
- 使用 `AssetRegistry.GetDependencies()` 和 `GetReferencers()`
- 区分 Hard/Soft/SearchableName 引用类型
- 递归模式需要防止循环引用（visited set）

#### 4.2.3 `validate_assets` (browser.validate_assets)

**描述**: 验证资产完整性（缺失引用、损坏包等）。

```
validate_assets /Game/Blueprints
validate_assets /Game --recursive --fix
```

**参数**:

| 参数 | 类型 | 必填 | 说明 |
|------|------|------|------|
| `path` | string | ✅ | 要验证的路径 |
| `recursive` | bool | ❌ | 递归，默认 true |
| `fix` | bool | ❌ | 尝试自动修复，默认 false |

**CLI 响应示例**:
```
✓ validated /Game/Blueprints — 42 scanned, 2 issues:
  [ERROR] /Game/BP_Old — missing dep: /Game/Meshes/SM_Deleted
  [WARN]  /Game/BP_Legacy — deprecated parent class

healthy: pass
```
```
✓ validated /Game — 120 scanned, 0 issues. all clean
```

**C++ 实现要点**:
- 遍历 AssetRegistry，检查每个资产的依赖是否存在
- 检查包文件是否可加载（`FPackageName::DoesPackageExist()`）
- `fix` 模式下尝试清除断裂引用

---

## 5. 功能模块四：Sequencer 增强

### 5.1 动机

当前 Sequencer 只有 5 个基础命令（create, describe, add_possessable, add_track, set_range），
无法进行实际的动画编辑（添加关键帧、相机剪辑等），使得过场动画创建工作流不完整。

### 5.2 新增命令

#### 5.2.1 `add_sequencer_keyframe` (sequencer.add_keyframe)

**描述**: 向 Sequencer 轨道添加关键帧。

```
add_sequencer_keyframe --sequence_path /Game/Cinematics/LS_Intro --binding_name PlayerChar --track_type Transform --frame 0 --value "(X=0,Y=0,Z=100)"
add_sequencer_keyframe --sequence_path /Game/Cinematics/LS_Intro --binding_name PlayerChar --track_type Float --channel Opacity --frame 30 --value 1.0
```

**参数**:

| 参数 | 类型 | 必填 | 说明 |
|------|------|------|------|
| `sequence_path` | string | ✅ | LevelSequence 资产路径 |
| `binding_name` | string | ✅ | 绑定名称（Possessable/Spawnable） |
| `track_type` | string | ✅ | 轨道类型：Transform / Float / Bool / Event |
| `channel` | string | ❌ | 通道名（如 Location.X, Rotation.Yaw） |
| `frame` | int | ✅ | 帧号 |
| `value` | string | ✅ | 值（自动解析类型） |
| `interpolation` | string | ❌ | 插值类型：Linear / Cubic / Constant，默认 Cubic |

**C++ 实现要点**:
- 通过 `UMovieScene` 定位 Binding → Section → Channel
- 使用 `FMovieSceneChannelProxy` 写入关键帧
- 支持 Transform（9 通道）、Float、Bool 类型
- 帧号转换使用 `FFrameNumber`（考虑 TickResolution）

#### 5.2.2 `set_sequencer_keyframe` (sequencer.set_keyframe)

**描述**: 修改已有关键帧的值或插值类型。

```
set_sequencer_keyframe --sequence_path /Game/Cinematics/LS_Intro --binding_name PlayerChar --track_type Transform --frame 0 --value "(X=100,Y=0,Z=100)"
```

**参数**: 同 `add_sequencer_keyframe`，额外支持 `--delete` 标志删除关键帧。

**C++ 实现要点**:
- 查找最近的关键帧（容差 ±1 帧）
- 修改 value 或删除

#### 5.2.3 `add_camera_cut_track` (sequencer.add_camera_cut)

**描述**: 添加 Camera Cut 轨道并绑定相机 Actor。

```
add_camera_cut_track --sequence_path /Game/Cinematics/LS_Intro --camera_name CineCam_1 --start_frame 0 --end_frame 150
```

**参数**:

| 参数 | 类型 | 必填 | 说明 |
|------|------|------|------|
| `sequence_path` | string | ✅ | LevelSequence 路径 |
| `camera_name` | string | ✅ | 关卡中的相机 Actor 名称 |
| `start_frame` | int | ❌ | 起始帧，默认 0 |
| `end_frame` | int | ❌ | 结束帧，默认序列结尾 |

**C++ 实现要点**:
- 使用 `UMovieScene::AddCameraCutTrack()`
- 创建 `UMovieSceneCameraCutSection` 并设置 CameraBindingID
- 自动 Possessable 绑定相机

#### 5.2.4 `add_sequencer_spawnable` (sequencer.add_spawnable)

**描述**: 添加 Spawnable 对象到序列（序列拥有此对象的生命周期）。

```
add_sequencer_spawnable --sequence_path /Game/Cinematics/LS_Intro --blueprint /Game/Blueprints/BP_FXActor --name ExplosionFX
```

**参数**:

| 参数 | 类型 | 必填 | 说明 |
|------|------|------|------|
| `sequence_path` | string | ✅ | LevelSequence 路径 |
| `blueprint` | string | ✅ | 蓝图资产路径或 Actor 类名 |
| `name` | string | ❌ | 显示名称 |

**C++ 实现要点**:
- 使用 `MovieScene->AddSpawnable()` 
- 设置 `FMovieSceneSpawnable` 的模板对象

#### 5.2.5 `play_sequence_preview` (sequencer.play_preview)

**描述**: 在编辑器中预览播放序列（非 PIE）。

```
play_sequence_preview --sequence_path /Game/Cinematics/LS_Intro
play_sequence_preview --sequence_path /Game/Cinematics/LS_Intro --from_frame 30
```

**参数**:

| 参数 | 类型 | 必填 | 说明 |
|------|------|------|------|
| `sequence_path` | string | ✅ | LevelSequence 路径 |
| `from_frame` | int | ❌ | 从指定帧开始，默认 0 |
| `action` | string | ❌ | `play` / `pause` / `stop`，默认 `play` |

**C++ 实现要点**:
- 通过 `ISequencer` 接口控制播放
- 需要先打开 Sequencer 编辑器窗口
- 使用 `SetPlaybackStatus()` 和 `SetGlobalTime()`

---

## 6. 功能模块五：Level/World 增强

### 6.1 动机

当前只有 `list_sublevels` 和 `get_world_settings`（只读），
无法执行关卡加载、子关卡创建、世界设置修改等操作。

### 6.2 新增命令

#### 6.2.1 `load_level` (level.load_level)

**描述**: 在编辑器中打开/切换到指定关卡。

```
load_level /Game/Maps/Level_02
```

**参数**:

| 参数 | 类型 | 必填 | 说明 |
|------|------|------|------|
| `level_path` | string | ✅ | 关卡资产路径 |
| `save_current` | bool | ❌ | 切换前保存当前关卡，默认 true |

**C++ 实现要点**:
- 使用 `FEditorFileUtils::LoadMap()` 或 `UEditorLevelUtils::LoadLevel()`
- 先检查当前关卡是否有未保存修改
- 需要在 GameThread 上执行

#### 6.2.2 `create_sublevel` (level.create_sublevel)

**描述**: 创建流式子关卡。

```
create_sublevel Sublevel_Gameplay --streaming_method Blueprint
```

**参数**:

| 参数 | 类型 | 必填 | 说明 |
|------|------|------|------|
| `sublevel_name` | string | ✅ | 子关卡名称 |
| `streaming_method` | string | ❌ | `Blueprint` / `AlwaysLoaded`，默认 `Blueprint` |
| `package_path` | string | ❌ | 保存路径（默认同持久关卡目录） |

**C++ 实现要点**:
- 使用 `UEditorLevelUtils::CreateNewStreamingLevel()`
- 设置 `StreamingMethod` 属性
- 返回创建的 Level 信息

#### 6.2.3 `set_world_settings` (level.set_world_settings)

**描述**: 修改 World Settings（当前只有 get，补充 set）。

```
set_world_settings --kill_z -10000 --gravity -980
set_world_settings --game_mode /Game/Blueprints/BP_GameMode
```

**参数**:

| 参数 | 类型 | 必填 | 说明 |
|------|------|------|------|
| `gravity` | float | ❌ | 全局重力值 |
| `kill_z` | float | ❌ | KillZ 高度 |
| `game_mode` | string | ❌ | GameMode 类路径 |
| `nav_data_class` | string | ❌ | 导航数据类 |

**C++ 实现要点**:
- 获取 `GWorld->GetWorldSettings()` 
- 逐一设置请求的属性
- 包装在 FScopedTransaction 中
- 标记关卡已修改

#### 6.2.4 `get_level_bounds` (level.get_level_bounds)

**描述**: 获取当前关卡的边界信息。

```
get_level_bounds
get_level_bounds --include_sublevels
```

**参数**:

| 参数 | 类型 | 必填 | 说明 |
|------|------|------|------|
| `include_sublevels` | bool | ❌ | 包含子关卡，默认 false |

**CLI 响应示例**:
```
level bounds (142 actors):
  origin: (0, 0, 0)
  extent: (5000, 5000, 1000)
  min: (-5000, -5000, -1000)  max: (5000, 5000, 1000)
```

**C++ 实现要点**:
- 遍历所有 Actor 计算 AABB 包围盒
- 或使用 `ALevelBounds` Actor
- 子关卡模式需要遍历 `StreamingLevels`

---

## 7. 功能模块六：蓝图调试支持

### 7.1 动机

配合现有的 `start_pie` / `stop_pie` / `take_pie_screenshot`，
补充蓝图断点和运行时变量监控能力，形成完整的 AI 辅助调试工作流。
AI 可以帮用户设置断点 → 启动 PIE → 观察运行时变量值 → 自动诊断问题。

### 7.2 新增命令

#### 7.2.1 `set_breakpoint` (debug.set_breakpoint)

**描述**: 在蓝图节点上设置/移除断点。

```
set_breakpoint BP_Player --node_id 4A3B2C1D --enabled
set_breakpoint BP_Player --node_id 4A3B2C1D --remove
```

**参数**:

| 参数 | 类型 | 必填 | 说明 |
|------|------|------|------|
| `blueprint_name` | string | ✅ | 蓝图名称 |
| `node_id` | string | ✅ | 节点 GUID |
| `enabled` | bool | ❌ | 启用/禁用，默认 true |
| `remove` | bool | ❌ | 移除断点，默认 false |
| `graph_name` | string | ❌ | 图名称（歧义时使用） |

**C++ 实现要点**:
- 使用 `FKismetDebugUtilities::SetBreakpointEnabled()` / `RemoveBreakpoint()`
- 定位节点使用已有的 `FindNode(Graph, NodeId)` 
- 返回当前蓝图所有断点列表

#### 7.2.2 `list_breakpoints` (debug.list_breakpoints)

**描述**: 列出蓝图中的所有断点。

```
list_breakpoints BP_Player
list_breakpoints  # 列出所有蓝图的断点
```

**参数**:

| 参数 | 类型 | 必填 | 说明 |
|------|------|------|------|
| `blueprint_name` | string | ❌ | 蓝图名称（省略则列出全部） |

**CLI 响应示例**:
```
breakpoints (3 total):
  BP_Player  EventGraph  4A3B2C1D  "Branch"       enabled   (100,200)
  BP_Player  EventGraph  5B4C3D2E  "Print String" enabled   (300,200)
  BP_Enemy   EventGraph  6C5D4E3F  "Set Health"   disabled  (150,400)
```

**C++ 实现要点**:
- 使用 `FKismetDebugUtilities::GetAllBreakpoints()` 或遍历图节点检查 `bHasBreakpoint`
- 返回人类可读的节点标题以便 AI 理解上下文

#### 7.2.3 `get_watch_values` (debug.get_watch_values)

**描述**: PIE 运行时获取蓝图变量的当前值（需要 PIE 正在运行）。

```
get_watch_values BP_Player
get_watch_values BP_Player --variable_name Health
```

**参数**:

| 参数 | 类型 | 必填 | 说明 |
|------|------|------|------|
| `blueprint_name` | string | ✅ | 蓝图名称 |
| `variable_name` | string | ❌ | 特定变量名（省略则返回所有） |
| `instance_index` | int | ❌ | 多实例时指定索引，默认 0 |

**CLI 响应示例**:
```
BP_Player_C_0 watch (PIE running):
  Health     Float    87.5
  IsAlive    Boolean  true
  Stamina    Float    42.0
  Score      Integer  1500
```

PIE 未运行时:
```
✗ PIE not running — start with: start_pie
```

**C++ 实现要点**:
- 需要 PIE 正在运行（先检查 `GEditor->IsPlayingSessionInEditor()`）
- 通过 `FKismetDebugUtilities` 获取调试对象
- 使用蓝图反射读取 CDO 或实例的变量值
- 局限性：只能读取 `UPROPERTY` 标记的变量

#### 7.2.4 `debug_step` (debug.step)

**描述**: 调试步进控制（当断点命中时）。

```
debug_step --action continue
debug_step --action step_over
debug_step --action step_into
```

**参数**:

| 参数 | 类型 | 必填 | 说明 |
|------|------|------|------|
| `action` | string | ✅ | `continue` / `step_over` / `step_into` / `step_out` |

**C++ 实现要点**:
- 使用 `FKismetDebugUtilities` 的调试控制 API
- 需要确认当前是否在断点处停留
- 这是高风险操作，可能导致编辑器卡死，需要超时保护

> ⚠️ **风险提示**: 蓝图调试步进在 MCP 远程控制模式下可能不稳定，
> 因为调试暂停会阻塞 GameThread，而 MCP 的 TCP 服务也运行在 GameThread。
> 建议初期版本只实现 `set_breakpoint` + `list_breakpoints` + `get_watch_values`，
> `debug_step` 标记为 **实验性功能**。

---

## 8. 功能模块七：Niagara 增强

### 8.1 动机

当前 Niagara 只有 7 个基础命令，缺少模块级别的增删操作和渲染器配置，
无法精细控制粒子效果。

### 8.2 新增命令

#### 8.2.1 `add_niagara_module` (niagara.add_module)

**描述**: 向 Niagara Emitter 的指定阶段添加模块。

```
add_niagara_module --system_path /Game/FX/NS_Fire --emitter_name Sparks --stage Spawn --module_name "Spawn Rate"
```

**参数**:

| 参数 | 类型 | 必填 | 说明 |
|------|------|------|------|
| `system_path` | string | ✅ | Niagara System 路径 |
| `emitter_name` | string | ✅ | 发射器名称 |
| `stage` | string | ✅ | `Spawn` / `Update` / `Render` |
| `module_name` | string | ✅ | 模块名称或脚本路径 |
| `insert_index` | int | ❌ | 插入位置，默认末尾 |

**C++ 实现要点**:
- 定位 Emitter → Stage → 通过 `FNiagaraStackGraphUtilities` 添加模块
- 模块名称需要映射到实际的 `UNiagaraScript` 资产
- 添加后需要重新编译

#### 8.2.2 `remove_niagara_module` (niagara.remove_module)

**描述**: 从 Niagara Emitter 中移除指定模块。

```
remove_niagara_module --system_path /Game/FX/NS_Fire --emitter_name Sparks --stage Update --module_name "Gravity Force"
```

**参数**:

| 参数 | 类型 | 必填 | 说明 |
|------|------|------|------|
| `system_path` | string | ✅ | Niagara System 路径 |
| `emitter_name` | string | ✅ | 发射器名称 |
| `stage` | string | ✅ | 阶段 |
| `module_name` | string | ✅ | 模块名称 |

**C++ 实现要点**:
- 查找模块节点并移除
- 断开相关连接
- 重新编译

#### 8.2.3 `set_niagara_renderer` (niagara.set_renderer)

**描述**: 配置 Niagara Emitter 的渲染器属性。

```
set_niagara_renderer --system_path /Game/FX/NS_Fire --emitter_name Sparks --renderer_type Sprite --material /Game/Materials/M_Particle
```

**参数**:

| 参数 | 类型 | 必填 | 说明 |
|------|------|------|------|
| `system_path` | string | ✅ | Niagara System 路径 |
| `emitter_name` | string | ✅ | 发射器名称 |
| `renderer_type` | string | ✅ | `Sprite` / `Mesh` / `Ribbon` / `Light` |
| `material` | string | ❌ | 材质路径 |
| `mesh` | string | ❌ | 网格体路径（Mesh 渲染器） |
| `properties` | object | ❌ | 额外属性 |

**C++ 实现要点**:
- 查找或创建 `UNiagaraRendererProperties` 子类
- 设置材质、网格体等属性
- 支持 Sprite (`UNiagaraSpriteRendererProperties`)、Mesh、Ribbon、Light

#### 8.2.4 `describe_niagara_emitter` (niagara.describe_emitter)

**描述**: 详细描述单个 Emitter 的完整结构。

```
describe_niagara_emitter --system_path /Game/FX/NS_Fire --emitter_name Sparks
```

**参数**:

| 参数 | 类型 | 必填 | 说明 |
|------|------|------|------|
| `system_path` | string | ✅ | Niagara System 路径 |
| `emitter_name` | string | ✅ | 发射器名称 |

**CLI 响应示例**:
```
emitter "Sparks" (CPU):

  SPAWN:
    Spawn Rate                Rate=50.0

  UPDATE:
    Gravity Force             Gravity=(0, 0, -980)
    Drag                      Drag=1.0

  RENDERERS:
    Sprite                    material=/Game/Materials/M_Particle

  PARAMETERS:
    Particles.Lifetime  Float   2.0
    Particles.Color     LinearColor (1,1,1,1)
```

**C++ 实现要点**:
- 比现有 `describe_niagara_system` 更深入，聚焦单个 Emitter
- 遍历所有 Stage 的 Module Stack
- 提取每个 Module 的参数值
- 列出渲染器配置

---

## 9. 功能模块八：响应格式化层 (Response Formatter)

### 9.1 动机

当前架构是 **CLI-in, JSON-out**：LLM 以 CLI 文本发送命令，但返回值是完整的
`json.dumps(indent=2)` JSON。这导致：

1. **Token 浪费**: 一个简单的"创建蓝图成功"消息占 8 行 JSON (~30 tokens)，
   而 CLI 风格只需 1 行 (~8 tokens)，**节省 ~70%**
2. **信噪比低**: `"success": true` 等冗余字段对 LLM 没有信息量
3. **不对称体验**: 输入精简（CLI），输出臃肿（JSON），风格不一致

### 9.2 设计方案

#### 9.2.1 架构位置

改动**完全在 Python 中间层** (`server.py`)，C++ 端零改动。

```
C++ FJsonObject → TCP → Python dict ──→ ResponseFormatter ──→ CLI text → LLM
                                          ↑ 新增这一层
```

#### 9.2.2 核心接口

**文件**: `Python/ue_cli_tool/formatter.py` (新增)

```python
class ResponseFormatter:
    """Transform C++ JSON responses into compact CLI-style text."""

    def format(self, action_id: str, result: dict, is_batch: bool = False) -> str:
        """Format a single command result."""
        if not result.get("success"):
            return self._format_error(action_id, result)

        # Look up domain-specific formatter
        domain = action_id.split(".", 1)[0] if "." in action_id else "general"
        formatter = self._formatters.get(action_id) or self._domain_formatters.get(domain)
        if formatter:
            return formatter(result)
        return self._format_default(action_id, result)

    def format_batch(self, results: list[dict], commands: list) -> str:
        """Format batch execution results."""
        lines = []
        ok = sum(1 for r in results if r.get("success"))
        lines.append(f"batch: {ok}/{len(results)} succeeded")
        for i, r in enumerate(results):
            cli_line = r.get("_cli_line", f"command_{i}")
            status = "✓" if r.get("success") else "✗"
            summary = self._one_line_summary(r)
            lines.append(f"  {status} {cli_line} → {summary}")
        return "\n".join(lines)
```

#### 9.2.3 格式化规则

| 命令类型 | 格式 | 示例 |
|---------|------|------|
| **写操作成功** | `✓ <verb> <target> → <path> [extras]` | `✓ created BP_Player → /Game/BP/BP_Player` |
| **写操作失败** | `✗ <error_message> [hint]` | `✗ Blueprint 'BP_X' not found` |
| **查询-列表** | 表头 + 对齐列 | 见 `list_classes` 示例 |
| **查询-详情** | 分段缩进文本 | 见 `describe_emitter` 示例 |
| **批量操作** | 摘要行 + 每条结果 | `batch: 5/5 succeeded` |

#### 9.2.4 默认兜底格式化

对于没有专用 formatter 的命令，使用**紧凑 JSON**（单行、无缩进、去除 `success: true`）：

```python
def _format_default(self, action_id: str, result: dict) -> str:
    """Fallback: compact single-line JSON without success field."""
    clean = {k: v for k, v in result.items()
             if k not in ("success", "_cli_line")}
    if len(clean) == 1:
        # Single-value result: just return the value
        return str(next(iter(clean.values())))
    return json.dumps(clean, ensure_ascii=False, separators=(",", ":"))
```

#### 9.2.5 server.py 集成点

修改 `server.py` 第 222-232 行：

```python
# Before (当前)
text = json.dumps(safe_result, indent=2, ensure_ascii=False)

# After (v0.3.0)
from .formatter import get_formatter
_formatter = get_formatter()

# 单命令
text = _formatter.format(action_id, safe_result)

# 批量命令
text = _formatter.format_batch(batch_results, parsed.commands)
```

#### 9.2.6 兼容性保证

- C++ 返回的 JSON 结构**不变**，formatter 是纯输出层
- 错误响应保留 JSON 格式（方便 LLM 结构化解析错误）
- 现有 200 个命令通过 `_format_default` 自动获得紧凑输出
- 新命令逐步补充专用 formatter

### 9.3 Token 节省估算

| 场景 | JSON (tokens) | CLI (tokens) | 节省 |
|------|-------------|------------|------|
| 创建蓝图 | ~30 | ~8 | 73% |
| list_assets (10项) | ~120 | ~40 | 67% |
| describe_graph | ~500 | ~200 | 60% |
| batch 5条命令 | ~150 | ~30 | 80% |
| 错误响应 | ~20 | ~15 | 25% |

---

## 10. 功能模块九：Live Coding 支持

### 10.1 动机

UE5 的 Live Coding 允许在编辑器运行时热重载 C++ 代码。当前 AI 无法触发编译、
检查编译状态或处理编译错误，导致 C++ 开发工作流中断。

AI 辅助场景：
- 用户修改了 C++ 代码 → AI 触发 Live Coding → 检查编译结果 → 报告错误
- AI 检测到蓝图引用的 C++ 类接口变化 → 建议重新编译
- 编译后自动验证相关蓝图的编译状态

### 10.2 新增命令

#### 10.2.1 `trigger_live_coding` (livecoding.compile)

**描述**: 触发 Live Coding 编译（等同于 Ctrl+Alt+F11）。

```
trigger_live_coding
trigger_live_coding --wait
```

**参数**:

| 参数 | 类型 | 必填 | 说明 |
|------|------|------|------|
| `wait` | bool | ❌ | 等待编译完成再返回，默认 false |
| `timeout` | int | ❌ | 等待超时秒数，默认 60 |

**CLI 响应示例**:

异步模式 (`--wait false`):
```
✓ live coding compile triggered
```

同步模式 (`--wait`):
```
✓ live coding compile succeeded (3.2s, 12 modules patched)
```

编译失败:
```
✗ live coding compile failed (2 errors):
  MyCharacter.cpp(42): error C2065: 'UndeclaredVar': undeclared identifier
  MyCharacter.cpp(58): error C2228: left of '.Health' must have class/struct/union
```

**C++ 实现要点**:
- 使用 `ILiveCodingModule::Get().EnableByDefault()` 确保 Live Coding 启用
- 调用 `ILiveCodingModule::Get().Compile()` 触发编译
- `wait` 模式使用 `OnPatchComplete` 委托等待完成
- 捕获编译输出日志返回错误信息
- 超时保护避免永久阻塞

#### 10.2.2 `get_live_coding_status` (livecoding.status)

**描述**: 查询 Live Coding 当前状态。

```
get_live_coding_status
```

**参数**: 无

**CLI 响应示例**:
```
live coding: enabled, idle
  last compile: 14:32:05 (succeeded, 3.2s)
  modules patched: 12
  pending changes: yes (3 files modified)
```
```
live coding: enabled, compiling...
  started: 14:35:12
  elapsed: 2.1s
```
```
live coding: disabled
  hint: enable in Editor Preferences → Live Coding
```

**C++ 实现要点**:
- 查询 `ILiveCodingModule::Get().IsEnabledByDefault()`
- 查询编译状态：`IsCompiling()`, 上次编译结果
- 检测是否有未编译的修改（通过文件时间戳比对）

#### 10.2.3 `enable_live_coding` (livecoding.enable)

**描述**: 启用或禁用 Live Coding。

```
enable_live_coding
enable_live_coding --disable
```

**参数**:

| 参数 | 类型 | 必填 | 说明 |
|------|------|------|------|
| `disable` | bool | ❌ | 传入则禁用 Live Coding，默认 false (启用) |

**CLI 响应示例**:
```
✓ live coding enabled
```
```
✓ live coding disabled
```

**C++ 实现要点**:
- 调用 `ILiveCodingModule::Get().EnableByDefault(true/false)`
- 需要包含 `LiveCoding` 模块依赖

---

## 11. 测试计划

### 9.1 测试层级

本项目采用三层测试策略：

```
┌─────────────────────────────────────────────┐
│  Layer 3: E2E Runtime Tests                  │
│  (test_runtime_e2e.py — 需要 UE Editor 运行) │
├─────────────────────────────────────────────┤
│  Layer 2: UE Automation Tests               │
│  (C++ — 编辑器内运行)                        │
├─────────────────────────────────────────────┤
│  Layer 1: Python Unit Tests                  │
│  (test_*.py — 纯 Python，无需 UE)            │
└─────────────────────────────────────────────┘
```

### 9.2 Layer 1: Python 单元测试

**文件**: `tests/test_v030_registry.py`

测试新增命令的注册表定义正确性（不需要 UE 运行）。

```python
# 测试用例列表
class TestV030Registry:
    """Verify all v0.3.0 actions are registered correctly."""

    # ── asset.* ──────────────────────────────────────────
    def test_asset_duplicate_registered(self):
        """asset.duplicate action exists with correct schema."""

    def test_asset_delete_registered(self):
        """asset.delete action exists, has 'force' optional param."""

    def test_asset_move_registered(self):
        """asset.move requires asset_path and destination."""

    def test_asset_fix_redirectors_registered(self):
        """asset.fix_redirectors requires path."""

    # ── reflection.* ─────────────────────────────────────
    def test_reflection_list_classes_registered(self):
        """reflection.list_classes requires base_class."""

    def test_reflection_get_class_properties_registered(self):
        """reflection.get_class_properties requires class_name."""

    def test_reflection_get_class_functions_registered(self):
        """reflection.get_class_functions requires class_name."""

    # ── browser.* ────────────────────────────────────────
    def test_browser_create_folder_registered(self):
        """browser.create_folder requires path."""

    def test_browser_get_asset_references_registered(self):
        """browser.get_asset_references requires asset_path."""

    def test_browser_validate_assets_registered(self):
        """browser.validate_assets requires path."""

    # ── sequencer.* (new) ────────────────────────────────
    def test_sequencer_add_keyframe_registered(self):
        """sequencer.add_keyframe requires sequence_path, binding_name, track_type, frame, value."""

    def test_sequencer_set_keyframe_registered(self):
        """sequencer.set_keyframe schema."""

    def test_sequencer_add_camera_cut_registered(self):
        """sequencer.add_camera_cut requires sequence_path, camera_name."""

    def test_sequencer_add_spawnable_registered(self):
        """sequencer.add_spawnable requires sequence_path, blueprint."""

    def test_sequencer_play_preview_registered(self):
        """sequencer.play_preview requires sequence_path."""

    # ── level.* (new) ────────────────────────────────────
    def test_level_load_level_registered(self):
        """level.load_level requires level_path."""

    def test_level_create_sublevel_registered(self):
        """level.create_sublevel requires sublevel_name."""

    def test_level_set_world_settings_registered(self):
        """level.set_world_settings has optional gravity/kill_z/game_mode."""

    def test_level_get_level_bounds_registered(self):
        """level.get_level_bounds registered as read-only."""

    # ── debug.* ──────────────────────────────────────────
    def test_debug_set_breakpoint_registered(self):
        """debug.set_breakpoint requires blueprint_name, node_id."""

    def test_debug_list_breakpoints_registered(self):
        """debug.list_breakpoints is read-only."""

    def test_debug_get_watch_values_registered(self):
        """debug.get_watch_values requires blueprint_name."""

    def test_debug_step_registered(self):
        """debug.step requires action param."""

    # ── niagara.* (new) ──────────────────────────────────
    def test_niagara_add_module_registered(self):
        """niagara.add_module requires system_path, emitter_name, stage, module_name."""

    def test_niagara_remove_module_registered(self):
        """niagara.remove_module schema."""

    def test_niagara_set_renderer_registered(self):
        """niagara.set_renderer requires renderer_type."""

    def test_niagara_describe_emitter_registered(self):
        """niagara.describe_emitter requires system_path, emitter_name."""

    # ── livecoding.* ─────────────────────────────────────
    def test_livecoding_compile_registered(self):
        """livecoding.compile registered with optional wait/timeout."""

    def test_livecoding_status_registered(self):
        """livecoding.status registered as read-only."""

    def test_livecoding_enable_registered(self):
        """livecoding.enable registered with optional disable flag."""

    # ── CLI parser integration ───────────────────────────
    def test_all_v030_commands_parseable(self):
        """Verify all new commands can be parsed by CliParser."""

    def test_positional_arg_mapping(self):
        """Verify positional args map correctly for new commands."""

    def test_batch_mode_with_new_commands(self):
        """Verify new commands work in multi-line batch mode."""


class TestResponseFormatter:
    """Verify ResponseFormatter transforms JSON to CLI text correctly."""

    def test_write_success_one_line(self):
        """Write ops produce single-line ✓ summary."""

    def test_write_error_with_hint(self):
        """Error responses include ✗ prefix and hint."""

    def test_query_list_as_table(self):
        """List queries produce aligned table output."""

    def test_query_detail_as_sections(self):
        """Detail queries produce sectioned text."""

    def test_batch_format(self):
        """Batch results produce summary + per-command lines."""

    def test_default_fallback_compact_json(self):
        """Unknown commands fall back to compact single-line JSON."""

    def test_success_field_stripped(self):
        """success:true is never present in CLI output."""

    def test_error_preserves_json(self):
        """Error responses keep structured JSON for LLM parsing."""
```

### 11.3 Layer 2: UE 自动化测试

**文件**: `Source/UECliTool/Private/Tests/` 目录下新增测试文件

使用 UE 的 `IMPLEMENT_SIMPLE_AUTOMATION_TEST` 宏。

```
测试文件规划：

Tests/
├── AssetActionsTest.cpp          # asset.duplicate/delete/move/fix_redirectors
├── ReflectionActionsTest.cpp     # reflection.list_classes/get_class_properties/functions
├── BrowserActionsTest.cpp        # browser.create_folder/get_references/validate
├── SequencerActionsTest.cpp      # sequencer 5 个新命令
├── LevelActionsTest.cpp          # level 4 个新命令
├── DebugActionsTest.cpp          # debug 4 个新命令
├── NiagaraActionsTest.cpp        # niagara 4 个新命令
└── LiveCodingActionsTest.cpp     # livecoding 3 个新命令
```

**每个测试文件的标准结构**:

```cpp
// AssetActionsTest.cpp (示例)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FDuplicateAssetTest,
    "UECliTool.Asset.DuplicateAsset",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

bool FDuplicateAssetTest::RunTest(const FString& Parameters)
{
    // 1. Setup: Create a test Blueprint
    // 2. Execute: Call duplicate_asset action
    // 3. Verify: Check new asset exists at destination
    // 4. Cleanup: Delete test assets
    return true;
}
```

**关键测试场景**:

| 模块 | 测试场景 | 类型 |
|------|---------|------|
| asset.duplicate | 复制蓝图到新目录 | 正常 |
| asset.duplicate | 名称冲突自动重命名 | 边界 |
| asset.duplicate | 批量复制 3 个资产 | 正常 |
| asset.delete | 安全删除无引用资产 | 正常 |
| asset.delete | 拒绝删除有引用资产 | 边界 |
| asset.delete | force 强制删除 | 正常 |
| asset.move | 移动到新目录 | 正常 |
| asset.move | 目标目录不存在自动创建 | 边界 |
| reflection.list_classes | 列出 Actor 子类 | 正常 |
| reflection.list_classes | max_depth 限制 | 边界 |
| reflection.get_class_properties | 查询 Character 属性 | 正常 |
| reflection.get_class_properties | 查询蓝图属性（include_inherited） | 正常 |
| reflection.get_class_functions | 查询可调用函数 | 正常 |
| browser.create_folder | 创建多级目录 | 正常 |
| browser.create_folder | 目录已存在 | 边界 |
| browser.get_asset_references | 查询依赖关系 | 正常 |
| browser.get_asset_references | 递归查询 | 正常 |
| browser.validate_assets | 扫描健康资产 | 正常 |
| sequencer.add_keyframe | Transform 关键帧 | 正常 |
| sequencer.add_keyframe | Float 关键帧 | 正常 |
| sequencer.add_camera_cut | 绑定相机 | 正常 |
| level.load_level | 加载有效关卡 | 正常 |
| level.load_level | 加载不存在的关卡 | 错误 |
| level.set_world_settings | 修改重力 | 正常 |
| level.get_level_bounds | 获取边界 | 正常 |
| debug.set_breakpoint | 设置断点 | 正常 |
| debug.set_breakpoint | 移除断点 | 正常 |
| debug.list_breakpoints | 列出断点 | 正常 |
| niagara.add_module | 添加 Spawn 模块 | 正常 |
| niagara.set_renderer | 设置 Sprite 渲染器 | 正常 |
| niagara.describe_emitter | 描述完整结构 | 正常 |
| livecoding.compile | 触发编译（异步模式） | 正常 |
| livecoding.compile | 等待编译完成 | 正常 |
| livecoding.compile | 编译超时 | 边界 |
| livecoding.status | 查询状态 | 正常 |
| livecoding.enable | 启用/禁用切换 | 正常 |

### 11.4 Layer 3: E2E Runtime Tests

**文件**: `tests/test_runtime_e2e.py` (追加新 category)

在现有 E2E 测试框架中追加新的测试类别：

```python
# 追加到 ALL_TESTS 列表

# ═══════════════════════════════════════════════════════
# CATEGORY: p10_asset — Asset Management (v0.3.0)
# ═══════════════════════════════════════════════════════
("duplicate_asset", "p10_asset", "duplicate_asset",
 {"asset_path": f"/Game/TestBP_{_TS}", "new_name": f"TestBP_{_TS}_Copy"},
 _v_ok_or_expected_error(["not found", "failed"])),

("delete_asset", "p10_asset", "delete_asset",
 {"asset_path": f"/Game/TestBP_{_TS}_Copy"},
 _v_ok_or_expected_error(["not found", "failed", "has references"])),

("move_asset", "p10_asset", "move_asset",
 {"asset_path": f"/Game/TestBP_{_TS}", "destination": "/Game/TestMoved"},
 _v_ok_or_expected_error(["not found", "failed"])),

("fix_redirectors", "p10_asset", "fix_redirectors",
 {"path": "/Game"},
 _v_ok),

# ═══════════════════════════════════════════════════════
# CATEGORY: p10_reflection — Class Reflection (v0.3.0)
# ═══════════════════════════════════════════════════════
("list_classes_actor", "p10_reflection", "list_classes",
 {"base_class": "Actor", "max_depth": 1},
 _v_ok),

("get_class_properties", "p10_reflection", "get_class_properties",
 {"class_name": "Character"},
 _v_ok),

("get_class_functions", "p10_reflection", "get_class_functions",
 {"class_name": "Character", "callable_only": True},
 _v_ok),

# ═══════════════════════════════════════════════════════
# CATEGORY: p10_browser — Content Browser (v0.3.0)
# ═══════════════════════════════════════════════════════
("create_folder", "p10_browser", "create_folder",
 {"path": f"/Game/TestFolder_{_TS}"},
 _v_ok_or_expected_error(["already exists"])),

("get_asset_references", "p10_browser", "get_asset_references",
 {"asset_path": "/Game/NonExistent"},
 _v_ok_or_expected_error(["not found"])),

("validate_assets", "p10_browser", "validate_assets",
 {"path": "/Game", "recursive": False},
 _v_ok),

# ═══════════════════════════════════════════════════════
# CATEGORY: p10_sequencer — Sequencer Enhanced (v0.3.0)
# ═══════════════════════════════════════════════════════
("add_sequencer_keyframe", "p10_sequencer", "add_sequencer_keyframe",
 {"sequence_path": f"/Game/LS_Test_{_TS}", "binding_name": "Test",
  "track_type": "Transform", "frame": 0, "value": "(X=0,Y=0,Z=0)"},
 _v_ok_or_expected_error(["not found", "no binding", "failed"])),

("add_camera_cut_track", "p10_sequencer", "add_camera_cut_track",
 {"sequence_path": f"/Game/LS_Test_{_TS}", "camera_name": "CineCam"},
 _v_ok_or_expected_error(["not found", "no camera", "failed"])),

# ═══════════════════════════════════════════════════════
# CATEGORY: p10_level — Level/World Enhanced (v0.3.0)
# ═══════════════════════════════════════════════════════
("get_level_bounds", "p10_level", "get_level_bounds", None, _v_ok),

("set_world_settings", "p10_level", "set_world_settings",
 {"gravity": -980.0},
 _v_ok),

# ═══════════════════════════════════════════════════════
# CATEGORY: p10_debug — Blueprint Debug (v0.3.0)
# ═══════════════════════════════════════════════════════
("list_breakpoints", "p10_debug", "list_breakpoints", None, _v_ok),

("set_breakpoint", "p10_debug", "set_breakpoint",
 {"blueprint_name": "NonExistent", "node_id": "00000000"},
 _v_ok_or_expected_error(["not found", "blueprint", "node"])),

# ═══════════════════════════════════════════════════════
# CATEGORY: p10_niagara — Niagara Enhanced (v0.3.0)
# ═══════════════════════════════════════════════════════
("describe_niagara_emitter", "p10_niagara", "describe_niagara_emitter",
 {"system_path": f"/Game/NS_Test_{_TS}", "emitter_name": "Default"},
 _v_ok_or_expected_error(["not found", "no emitter", "failed"])),

# ═══════════════════════════════════════════════════════
# CATEGORY: p10_livecoding — Live Coding (v0.3.0)
# ═══════════════════════════════════════════════════════
("get_live_coding_status", "p10_livecoding", "get_live_coding_status",
 None, _v_ok),

("trigger_live_coding", "p10_livecoding", "trigger_live_coding",
 {"wait": False},
 _v_ok_or_expected_error(["disabled", "not available"])),
```

### 11.5 测试覆盖率目标

| 层级 | 覆盖率目标 | 说明 |
|------|-----------|------|
| Layer 1 (Python) | 100% 注册表 | 所有 30 个新命令的 schema 验证 + formatter 测试 |
| Layer 2 (UE Automation) | ≥80% 功能 | 核心路径 + 主要边界条件 |
| Layer 3 (E2E) | ≥90% 命令 | 至少每个命令一个冒烟测试 |

---

## 12. 文档更新计划

### 12.1 新增 Skills 文件

| 文件 | 对应模块 | 内容 |
|------|---------|------|
| `Python/ue_cli_tool/skills/asset-management.md` | 资产管理 | 复制/删除/移动/重定向器修复工作流 |
| `Python/ue_cli_tool/skills/reflection.md` | 类反射 | 类查询/属性查询/函数查询使用指南 |
| `Python/ue_cli_tool/skills/content-browser.md` | Content Browser | 文件夹创建/引用查询/资产验证 |
| `Python/ue_cli_tool/skills/debug.md` | 蓝图调试 | 断点/监控/调试工作流 |
| `Python/ue_cli_tool/skills/livecoding.md` | Live Coding | 编译/状态查询/启用禁用工作流 |

**不需要新增 skills 的模块**：
- Sequencer → 更新现有 `sequencer.md`（如果存在）或合并到 `editor-level.md`
- Level → 更新现有 `editor-level.md`
- Niagara → 更新现有 `niagara.md`

### 12.2 Skills 文件模板

```markdown
# 资产管理工作流

## 快速开始：复制资产

​```
duplicate_asset /Game/Blueprints/BP_Player --destination /Game/Backup
​```

## 安全删除资产

先检查引用：
​```
get_asset_references /Game/Blueprints/BP_Old --direction referencers
​```

确认无引用后删除：
​```
delete_asset /Game/Blueprints/BP_Old
​```

## 批量移动资产

​```
move_asset /Game/OldFolder/BP_A --destination /Game/NewFolder
move_asset /Game/OldFolder/BP_B --destination /Game/NewFolder
fix_redirectors /Game/OldFolder
​```

## 提示

- 删除前务必检查引用关系，避免破坏项目
- 移动资产后建议运行 `fix_redirectors`
- 批量操作建议使用多行 CLI 模式
```

### 12.3 现有文档更新

| 文件 | 更新内容 |
|------|---------|
| `README.md` | 更新功能列表、命令数量（200→230），新增 CLI-out 说明 |
| `docs/actions.md` | 新增 asset/reflection/browser/debug/livecoding 域描述 |
| `docs/architecture.md` | 更新架构图（新增 ResponseFormatter 层 + Action 文件） |
| `docs/development.md` | 补充新模块的开发指南 + formatter 开发规范 |
| `CONTRIBUTING.md` | 更新测试要求（三层测试 + formatter 测试） |

### 12.4 注册表更新

**文件**: `Python/ue_cli_tool/registry/actions.py`

新增以下 ActionDef 列表：
- `_ASSET_ACTIONS` (4 个)
- `_REFLECTION_ACTIONS` (3 个)
- `_BROWSER_ACTIONS` (3 个)
- `_SEQUENCER_V2_ACTIONS` (5 个，追加到现有 `_SEQUENCER_ACTIONS`)
- `_LEVEL_V2_ACTIONS` (4 个，追加到现有 `_EXTENDED_ACTIONS`)
- `_DEBUG_ACTIONS` (4 个)
- `_NIAGARA_V2_ACTIONS` (4 个，追加到现有 `_NIAGARA_ACTIONS`)
- `_LIVECODING_ACTIONS` (3 个)

在 `register_all_actions()` 中注册。

---

## 13. 实现路线图

### 13.1 C++ Action 文件规划

| 新增文件 | 对应模块 | 预估代码量 |
|---------|---------|-----------|
| `Source/UECliTool/Private/Actions/AssetManagementActions.cpp` | asset.* | ~400 行 |
| `Source/UECliTool/Public/Actions/AssetManagementActions.h` | asset.* | ~100 行 |
| `Source/UECliTool/Private/Actions/ReflectionActions.cpp` | reflection.* | ~500 行 |
| `Source/UECliTool/Public/Actions/ReflectionActions.h` | reflection.* | ~80 行 |
| `Source/UECliTool/Private/Actions/ContentBrowserActions.cpp` | browser.* | ~350 行 |
| `Source/UECliTool/Public/Actions/ContentBrowserActions.h` | browser.* | ~80 行 |
| `Source/UECliTool/Private/Actions/DebugActions.cpp` | debug.* | ~400 行 |
| `Source/UECliTool/Public/Actions/DebugActions.h` | debug.* | ~80 行 |
| (追加) `SequencerActions.cpp` | sequencer.* 新命令 | +~500 行 |
| (追加) `ExtendedActions.cpp` | level.* 新命令 | +~300 行 |
| (追加) `NiagaraActions.cpp` | niagara.* 新命令 | +~400 行 |
| `Source/UECliTool/Private/Actions/LiveCodingActions.cpp` | livecoding.* | ~250 行 |
| `Source/UECliTool/Public/Actions/LiveCodingActions.h` | livecoding.* | ~60 行 |
| `Python/ue_cli_tool/formatter.py` | 响应格式化层 | ~400 行 |

**总预估**: ~3,500 行 C++, ~1,200 行 Python, ~600 行测试, ~500 行文档

### 13.2 Action 注册

所有新 Action 类需要在 `UEEditorMCPModule.cpp` 的 `RegisterActions()` 中注册：

```cpp
// v0.3.0 新增
RegisterAction(MakeShared<FDuplicateAssetAction>());
RegisterAction(MakeShared<FDeleteAssetAction>());
RegisterAction(MakeShared<FMoveAssetAction>());
RegisterAction(MakeShared<FFixRedirectorsAction>());
RegisterAction(MakeShared<FListClassesAction>());
RegisterAction(MakeShared<FGetClassPropertiesAction>());
RegisterAction(MakeShared<FGetClassFunctionsAction>());
RegisterAction(MakeShared<FCreateFolderAction>());
RegisterAction(MakeShared<FGetAssetReferencesAction>());
RegisterAction(MakeShared<FValidateAssetsAction>());
RegisterAction(MakeShared<FAddSequencerKeyframeAction>());
RegisterAction(MakeShared<FSetSequencerKeyframeAction>());
RegisterAction(MakeShared<FAddCameraCutTrackAction>());
RegisterAction(MakeShared<FAddSequencerSpawnableAction>());
RegisterAction(MakeShared<FPlaySequencePreviewAction>());
RegisterAction(MakeShared<FLoadLevelAction>());
RegisterAction(MakeShared<FCreateSublevelAction>());
RegisterAction(MakeShared<FSetWorldSettingsAction>());
RegisterAction(MakeShared<FGetLevelBoundsAction>());
RegisterAction(MakeShared<FSetBreakpointAction>());
RegisterAction(MakeShared<FListBreakpointsAction>());
RegisterAction(MakeShared<FGetWatchValuesAction>());
RegisterAction(MakeShared<FDebugStepAction>());
RegisterAction(MakeShared<FAddNiagaraModuleAction>());
RegisterAction(MakeShared<FRemoveNiagaraModuleAction>());
RegisterAction(MakeShared<FSetNiagaraRendererAction>());
RegisterAction(MakeShared<FDescribeNiagaraEmitterAction>());
// Live Coding
RegisterAction(MakeShared<FTriggerLiveCodingAction>());
RegisterAction(MakeShared<FGetLiveCodingStatusAction>());
RegisterAction(MakeShared<FEnableLiveCodingAction>());
```

### 13.3 Build.cs 依赖更新

`UECliTool.Build.cs` 可能需要新增模块依赖：

```csharp
// Sequencer 增强
"MovieScene",
"MovieSceneTracks",
"LevelSequence",
"Sequencer",                    // ISequencer 接口

// 蓝图调试
"KismetCompiler",               // FKismetDebugUtilities

// Niagara 增强（已有）
"Niagara",
"NiagaraCore",
"NiagaraEditor",                // 可能需要

// Asset Management
"ContentBrowser",               // Content Browser API
"UnrealEd",                     // AssetTools（已有）

// Live Coding
"LiveCoding",                   // ILiveCodingModule
```

### 13.4 版本号更新

| 文件 | 字段 | 旧值 | 新值 |
|------|------|------|------|
| `UECliTool.uplugin` | `VersionName` | `0.2.0` | `0.3.0` |
| `UECliTool.uplugin` | `Version` | `1` | `2` |
| `UECliTool.uplugin` | `IsBetaVersion` | `true` | `true` |

---

## 14. 风险与缓解

### 14.1 风险矩阵

| 风险 | 概率 | 影响 | 缓解措施 |
|------|------|------|---------|
| `debug.step` 阻塞 GameThread 导致 MCP 无响应 | 高 | 高 | 标记为实验性功能，添加超时保护，文档警告 |
| Sequencer 内部 API 在 UE5.6 有变更 | 中 | 中 | 使用稳定的 `UMovieScene` API，避免 Editor-only 接口 |
| Niagara 模块操作可能因模块图变化而失败 | 中 | 低 | 操作前验证模块存在性，返回详细错误信息 |
| `delete_asset` force 模式破坏项目 | 低 | 高 | 默认安全模式，force 需显式传参，返回引用者列表 |
| `load_level` 时未保存修改丢失 | 低 | 高 | 默认 `save_current=true`，切换前自动保存 |
| 反射查询返回数据量过大 | 中 | 中 | 添加 `max_results` 限制（默认 200），支持分页 |
| ResponseFormatter 破坏 LLM 解析 | 中 | 中 | 默认兜底用紧凑 JSON，错误响应保留 JSON，可逐步添加专用 formatter |
| Live Coding 编译阻塞 GameThread | 中 | 高 | `wait` 默认 false（异步），同步模式有 timeout 保护 |
| Live Coding 模块不可用（某些 UE 配置） | 低 | 低 | 先检查模块可用性，不可用时返回友好提示 |

### 14.2 兼容性说明

- 所有新增命令为纯新增（additive），不修改现有 200 个命令的行为
- 新增 C++ 文件不影响现有编译
- Python 注册表追加不影响现有命令的 CLI 解析
- E2E 测试追加新 category，不影响现有测试运行
- **ResponseFormatter** 对现有 200 个命令自动生效（通过 `_format_default` 紧凑化），
  不破坏 LLM 解析能力

### 14.3 回退策略

如果某个模块实现遇到严重问题：
1. 该模块的 Action 注册可以直接注释掉（不影响其他模块）
2. Python 注册表中对应 ActionDef 列表不注册即可
3. Skills 文件和文档独立，可单独移除
4. **ResponseFormatter** 可通过环境变量 `UE_CLI_RAW_JSON=1` 禁用，回退到原始 JSON

---

## 附录 A: 完整命令索引（v0.3.0 新增）

| # | 命令名 | Action ID | 类型 | RequiresSave |
|---|--------|-----------|------|-------------|
| 1 | `duplicate_asset` | `asset.duplicate` | write | ✅ |
| 2 | `delete_asset` | `asset.delete` | write | ✅ |
| 3 | `move_asset` | `asset.move` | write | ✅ |
| 4 | `fix_redirectors` | `asset.fix_redirectors` | write | ✅ |
| 5 | `list_classes` | `reflection.list_classes` | read | ❌ |
| 6 | `get_class_properties` | `reflection.get_class_properties` | read | ❌ |
| 7 | `get_class_functions` | `reflection.get_class_functions` | read | ❌ |
| 8 | `create_folder` | `browser.create_folder` | write | ✅ |
| 9 | `get_asset_references` | `browser.get_asset_references` | read | ❌ |
| 10 | `validate_assets` | `browser.validate_assets` | read | ❌ |
| 11 | `add_sequencer_keyframe` | `sequencer.add_keyframe` | write | ✅ |
| 12 | `set_sequencer_keyframe` | `sequencer.set_keyframe` | write | ✅ |
| 13 | `add_camera_cut_track` | `sequencer.add_camera_cut` | write | ✅ |
| 14 | `add_sequencer_spawnable` | `sequencer.add_spawnable` | write | ✅ |
| 15 | `play_sequence_preview` | `sequencer.play_preview` | read | ❌ |
| 16 | `load_level` | `level.load_level` | write | ✅ |
| 17 | `create_sublevel` | `level.create_sublevel` | write | ✅ |
| 18 | `set_world_settings` | `level.set_world_settings` | write | ✅ |
| 19 | `get_level_bounds` | `level.get_level_bounds` | read | ❌ |
| 20 | `set_breakpoint` | `debug.set_breakpoint` | write | ❌ |
| 21 | `list_breakpoints` | `debug.list_breakpoints` | read | ❌ |
| 22 | `get_watch_values` | `debug.get_watch_values` | read | ❌ |
| 23 | `debug_step` | `debug.step` | write | ❌ |
| 24 | `add_niagara_module` | `niagara.add_module` | write | ✅ |
| 25 | `remove_niagara_module` | `niagara.remove_module` | write | ✅ |
| 26 | `set_niagara_renderer` | `niagara.set_renderer` | write | ✅ |
| 27 | `describe_niagara_emitter` | `niagara.describe_emitter` | read | ❌ |
| 28 | `trigger_live_coding` | `livecoding.compile` | write | ❌ |
| 29 | `get_live_coding_status` | `livecoding.status` | read | ❌ |
| 30 | `enable_live_coding` | `livecoding.enable` | write | ❌ |
