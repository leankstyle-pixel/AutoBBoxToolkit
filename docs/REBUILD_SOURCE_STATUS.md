# AutoBBoxToolkit Commit-Ready Status

- Status date: `2026-04-07`
- Workspace: `F:\\claude\\003`
- Verified DLL output: `F:\\claude\\003\deploy\AutoBBoxToolkit\autobbox_toolkit.dll`
- Latest verified action: `scripts/build_autobbox.ps1` completed successfully and produced the runtime DLL

## 1. Current State

This workspace is now in a commit-ready rebuild state.

The original single-file plugin implementation has been recovered into a layered source tree with a thin plugin entry shell, modular command orchestration, isolated Creo API wrappers, and separated UI/business logic modules.

The main plugin entry file is now:

- `src/autobbox_toolkit.cpp`

It is reduced to approximately `252` lines and is responsible mainly for:

- holding process-wide runtime flags and option state
- wiring runtime bridges and command callbacks
- forwarding `user_initialize` and `user_terminate` into the modular `main` layer

At this stage, further mechanical splitting of the entry file is not recommended. The higher-value work is now repository hygiene, state/documentation cleanup, and incremental hardening.

## 2. Module Boundaries

### Source layout

- `src/autobbox_toolkit.cpp`
  - thin Creo plugin shell
- `src/main`
  - command registration, callback wiring, runtime bridge, plugin bootstrap, command dispatch
- `src/application`
  - feature workflows and business logic
- `src/creo`
  - Creo Toolkit wrapper/helper logic
- `src/ui`
  - native dialog/controller logic
- `src/common`
  - path, logging, string, and utility helpers

### Header layout

- `include/autobbox/main`
- `include/autobbox/application`
- `include/autobbox/creo`
- `include/autobbox/ui`
- `include/autobbox/common`
- `include/autobbox/core`

### Boundary summary

- `main` is the composition/orchestration layer
- `application` owns feature use cases
- `creo` owns low-level Toolkit-facing helpers
- `ui` owns dialog lifecycle and user interaction
- `core` owns shared data structures and DTO-like feature state
- `common` owns cross-cutting non-domain utilities

### Key modularized command paths

- Main run pipeline:
  - `src/main/plugin_main_run.cpp`
  - `src/application/model_run_tasks.cpp`
- Drawing view pipeline:
  - `src/main/plugin_drawing3.cpp`
  - `src/application/drawing3_views.cpp`
  - `src/ui/drawing3_dialog.cpp`
- Split pipeline:
  - `src/main/plugin_split.cpp`
  - `src/application/split_instances.cpp`
  - `src/ui/split_dialog.cpp`
- Relations pipeline:
  - `src/main/plugin_relations.cpp`
  - `src/application/relations.cpp`
  - `src/ui/relations_text_dialog.cpp`
- BOM / parameter tool pipeline:
  - `src/main/plugin_param_tool.cpp`
  - `src/application/bom_actions.cpp`
  - `src/application/bom_state.cpp`
  - `src/application/bom_update.cpp`
  - `src/application/bom_export.cpp`
  - `src/application/param_tool.cpp`
  - `src/ui/bom_dialog.cpp`
  - `src/ui/param_add_dialog.cpp`

## 3. Implemented Functional Scope

### Registered commands

- `AutoBBox.Run`
- `AutoBBox.RunVolume`
- `AutoBBox.CreateIso`
- `AutoBBox.DeleteParams`
- `AutoBBox.CreateDwg3Views`
- `AutoBBox.SplitInstances`
- `AutoBBox.CleanRelations`
- `AutoBBox.AddRelations`
- `AutoBBox.ParamTool`

### Registered options

- `AutoBBox.Option.Parts`
- `AutoBBox.Option.Assemblies`
- `AutoBBox.Option.Surface`
- `AutoBBox.Option.Curve`
- `AutoBBox.Option.Recompute`
- `AutoBBox.Option.TopLevelOnly`

### Recovered / rebuilt feature behavior

- ribbon loading from `toolkitribbonui.rbn`
- size parameter write:
  - `BBOX_LXWXH` written as integer `LxWxH`
- volume parameter write:
  - `BBOX_VOL_M3`
- legacy size parameter cleanup:
  - `BBOX_L`
  - `BBOX_W`
  - `BBOX_H`
  - `BBOX_MAX`
- family table support via immediate generic family-table APIs
- generic skip rule for `UPRIGHT_POST`
- top-level-only target filtering
- isometric view creation:
  - `auto_ISOMETRIC`
- startup/report logging:
  - `autobbox_startup.log`
  - `autobbox_report.txt`
- drawing3 candidate selection, view generation, reuse, and annotation flow
- split-instance workflow
- relations clean/add workflow
- BOM preview / column add-remove / draft edit / update / export workflow

## 4. Build, Runtime, and Backup

### Build

Build with:

```powershell
powershell -ExecutionPolicy Bypass -File F:\\claude\\003\scripts\build_autobbox.ps1
```

Current build script behavior:

- configures and builds under `build\autobbox`
- outputs `autobbox_toolkit.dll` to `deploy\AutoBBoxToolkit`
- mirrors resource and message assets into:
  - `deploy\AutoBBoxToolkit`
  - `runtime\AutoBBoxToolkit`

### Backup

Backup with:

```powershell
powershell -ExecutionPolicy Bypass -File F:\\claude\\003\scripts\backup_autobbox.ps1
```

Current backup script behavior:

- creates plugin runtime backup zip in `backup\`
- creates source backup zip in `backup\`
- excludes local/generated directories from source backup:
  - `.git`
  - `archive`
  - `backup`
  - `build`
  - `deploy`
  - `package`
  - `runtime`

## 5. Repository Boundary

### Intended commit content

The repository should retain:

- source code under `src\` and `include\`
- build/config scripts under `scripts\`
- source resources under `resource\`, `text\`, `ribbon\`
- source/status documents under `docs\`
- recovery/reference manifests that are intentionally part of source history

### Ignored local/generated content

Current `.gitignore` excludes:

- `build/`
- `deploy/`
- `runtime/`
- `package/`
- `backup/`
- `archive/`
- `*.log`
- `*.bak`
- `*.tmp`
- `autobbox_report.txt`
- local scratch capture png at repository root

### Important source-vs-output note

`deploy/` and `runtime/` are treated as generated runtime mirrors, not source-of-truth directories.

This means:

- runtime packaging content should be produced by scripts
- source edits should be made in source/resource directories, not inside generated runtime mirrors

## 6. Source-of-Truth Guidance

At the current stage, the intended editable source roots are:

- `src/`
- `include/`
- `resource/`
- `text/`
- `ribbon/`
- root `autobbox_msg.txt`

`deploy/` and `runtime/` should remain generated outputs.

`project/` currently still contains mirrored UI/resource content and should be treated carefully. If kept, it should ideally become script-generated rather than manually co-maintained.

## 7. Known Structural Observations

### What is in good shape

- plugin entry is thin and no longer owns core workflows
- command orchestration is separated from feature logic
- Creo wrapper logic is no longer duplicated across features
- UI dialog logic is no longer embedded in the entry file
- `src/main` and `include/autobbox/main` boundaries are aligned
- `CMakeLists.txt` currently matches the existing `src/*.cpp` set

### What is still worth improving later

- `main` layer still uses some static runtime state internally
- runtime/resource duplication still exists between source trees and mirrored trees
- large feature files still exist in places such as BOM UI and drawing3 workflow
- automated regression coverage is still minimal

## 8. Post-Commit Optimization Priorities

### P0: recommended immediately after initial source commit

1. Freeze the source-of-truth resource directories and stop manual dual maintenance.
2. Keep `deploy/` and `runtime/` as generated-only outputs.
3. Preserve this document as the main commit-ready project status entry.

### P1: high-value next engineering work

1. Reduce static state in `src/main` by introducing a clearer runtime context object.
2. Convert duplicated resource mirrors such as `project/` into script-generated outputs if possible.
3. Add a documented smoke-test checklist for:
   - `Run`
   - `RunVolume`
   - `CreateIso`
   - `CreateDwg3Views`
   - `SplitInstances`
   - `CleanRelations`
   - `AddRelations`
   - `ParamTool`

### P2: later, feature-driven refactor work

1. Split large files only when modifying those features again.
2. Add tests around pure logic in BOM/parameter parsing and state-building flows.
3. Normalize encoding expectations for message/resource/script files.

## 9. Commit Recommendation

This workspace is suitable for an initial rebuilt-source commit with the following positioning:

- source rebuild completed
- modular command/layout refactor completed
- entry shell reduced to thin bootstrap
- build verified
- runtime and backup boundaries now scriptable and repository-safe
