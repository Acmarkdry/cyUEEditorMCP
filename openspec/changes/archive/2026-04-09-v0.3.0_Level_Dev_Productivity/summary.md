# v0.3.0: Level & Developer Productivity

**Archived:** 2026-04-09
**Status:** ✅ Complete — compiled successfully on UE 5.6

## Overview

Added 30+ new MCP actions across 8 functional modules to enhance editor automation capabilities.

## Modules Implemented

### 1. Asset Management (`AssetManagementActions.h/cpp`)
- `duplicate_asset` — Duplicate assets with new name/path
- `delete_asset` — Delete assets with confirmation
- `move_asset` — Move/rename assets
- `fix_redirectors` — Fix asset redirectors

### 2. Reflection (`ReflectionActions.h/cpp`)
- `get_class_properties` — Query C++ class properties via reflection
- `get_class_functions` — Query C++ class functions via reflection

### 3. Content Browser (`ContentBrowserActions.h/cpp`)
- `browse_folder` — List assets in a content browser folder
- `get_asset_info` — Get detailed asset metadata

### 4. Sequencer (`SequencerActions.h/cpp`)
- `add_keyframe` — Add keyframes to sequencer tracks
- `add_camera_cut` — Add camera cut tracks/sections

### 5. Level/World (`ExtendedActions.h/cpp` — extended)
- `load_level` — Load map files programmatically
- `get_level_info` — Query current level metadata
- `get_actor_bounds` — Get actor bounding box info

### 6. BP Debugging (`DebugActions.h/cpp`)
- `set_breakpoint` — Set/toggle/remove Blueprint breakpoints
- `list_breakpoints` — List all breakpoints across blueprints
- `get_watch_values` — Get variable values during PIE
- `debug_step` — Experimental debug stepping

### 7. Niagara (`NiagaraActions.h/cpp` — extended)
- `add_niagara_emitter` — Add emitters to Niagara systems
- `set_niagara_module_param` — Set Niagara module parameters

### 8. Live Coding (`LiveCodingActions.h/cpp`)
- `trigger_live_coding` — Trigger Live Coding compilation
- `get_live_coding_status` — Query compilation status

## Key Technical Notes

- Uses `FindFirstObject<UClass>` instead of deprecated `ANY_PACKAGE`
- Uses `FKismetDebugUtilities::CreateBreakpoint` + `FindBreakpointForNode` (UE 5.6 API)
- `FEditorFileUtils::LoadMap` returns `bool` in UE 5.6
- `ILiveCodingModule::Compile()` / `IsCompiling()` for live coding
- Added `LiveCoding` module dependency in `Build.cs`
- All 30+ handlers registered in `MCPBridge.cpp`
