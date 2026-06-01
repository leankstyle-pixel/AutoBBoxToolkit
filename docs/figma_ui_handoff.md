# AutoBBox Figma UI Handoff

## Goal

This document defines the full UI scope that should be redesigned in Figma for the AutoBBox plugin.
The target is not only dialog beautification, but a unified desktop-plugin visual system covering:

- Ribbon command entry points
- Ribbon option checkboxes
- Modal dialogs
- Table/list editing patterns
- Message and empty states
- Icon style

## Source of Truth

- Command registration: `F:\\claude\\003\src\autobbox_toolkit.cpp`
- Chinese UI copy: `F:\\claude\\003\text\chinese_cn\autobbox_msg.txt`
- Dialog resources:
  - `F:\\claude\\003\resource\autobbox_param_tool.res`
  - `F:\\claude\\003\resource\autobbox_bom_value_edit.res`
  - `F:\\claude\\003\resource\autobbox_delete_opts.res`
  - `F:\\claude\\003\resource\autobbox_dwg3_pick.res`
  - `F:\\claude\\003\resource\autobbox_rel_add.res`
  - `F:\\claude\\003\resource\autobbox_split_pick.res`
- Existing icon assets:
  - `F:\\claude\\003\text\resource\autobbox_size.png`
  - `F:\\claude\\003\text\resource\autobbox_volume.png`
  - `F:\\claude\\003\text\resource\autobbox_iso.png`
  - `F:\\claude\\003\text\resource\autobbox_dwg3.png`
  - `F:\\claude\\003\text\resource\autobbox_param_tool.png`
  - `F:\\claude\\003\text\resource\autobbox_parts.png`
  - `F:\\claude\\003\text\resource\autobbox_asm.png`
  - `F:\\claude\\003\text\resource\autobbox_surface.png`
  - `F:\\claude\\003\text\resource\autobbox_curve.png`
  - `F:\\claude\\003\text\resource\autobbox_recalc.png`
  - `F:\\claude\\003\text\resource\autobbox_top2.png`

## Design Scope

### 1. Ribbon Layer

Figma should include one page for the Ribbon entry layer.

#### Primary commands

- `算尺寸`
- `算体积`
- `建轴测`
- `删参数`
- `建视图`
- `拆实例`
- `BOM清单`

#### Secondary commands

- `清理关系式`
- `添加关系式`

#### Global option toggles

- `零件`
- `组件`
- `含曲面`
- `含曲线`
- `重算覆盖`
- `仅二层`

#### Ribbon deliverables

- Default state
- Hover state
- Pressed state
- Disabled state
- Checked and unchecked states for options
- Small and large icon compositions
- Text-first layout and icon-first layout alternatives

### 2. Modal/Dialog Layer

Figma should include one page per dialog, plus a shared component library.

#### Screen A: BOM 清单主界面

Resource: `F:\\claude\\003\resource\autobbox_param_tool.res`

Purpose:

- Preview BOM rows
- Preview available parameters
- Add/remove displayed parameter columns
- Refresh
- Draft-edit values
- Push drafts back to model
- Export CSV

Main regions:

- Summary bar
- Available parameter area
- BOM table area
- Bottom action bar

Core controls:

- Left table with checkbox column
- Right data table with fixed columns
- Toolbar-style action buttons

Required states:

- Empty BOM
- Empty available params
- Long parameter names
- Mixed-type parameter column
- Read-only cell
- Modified draft cell
- Row selection
- Column selection
- Large data volume

Fixed columns:

- `序号`
- `模型名称`
- `数量`

Dynamic columns:

- User-selected parameter columns

Buttons:

- `+`
- `-`
- `刷新`
- `更新到模型`
- `导出Excel`
- `关闭`

#### Screen B: BOM 单元格值编辑弹窗

Resource: `F:\\claude\\003\resource\autobbox_bom_value_edit.res`

Purpose:

- Edit a single BOM draft value through a safe modal flow

Controls:

- Prompt text
- Type hint
- Single-line input
- `确定`
- `取消`

Required states:

- Editable existing value
- New parameter creation value
- Invalid input
- Long prompt text

#### Screen C: 删参数弹窗

Resource: `F:\\claude\\003\resource\autobbox_delete_opts.res`

Purpose:

- Bulk delete generated parameters

Controls:

- `Delete Size (BBOX_LXWXH)`
- `Delete Volume (BBOX_VOL_M3)`
- `OK`
- `Cancel`

Required states:

- None selected
- One selected
- Two selected

#### Screen D: 建视图弹窗

Resource: `F:\\claude\\003\resource\autobbox_dwg3_pick.res`

Purpose:

- Batch create drawing views for selected models

Controls:

- View selection matrix
- `Fast Mode`
- Model multi-select list
- `Select All`
- `Clear`
- `OK`
- `Cancel`

Views:

- `Back`
- `Top`
- `Right`
- `Front`
- `Left`
- `Bottom`
- `Isometric`

Required states:

- Recommended default combination
- Dense model list
- Empty candidate list
- Partial selection
- Full selection

#### Screen E: 添加关系式弹窗

Resource: `F:\\claude\\003\resource\autobbox_rel_add.res`

Purpose:

- Paste and submit relation text

Controls:

- Prompt label
- Large multi-line text area
- `OK`
- `Cancel`

Required states:

- Empty
- Multi-line pasted content
- Validation error

#### Screen F: 拆实例弹窗

Resource: `F:\\claude\\003\resource\autobbox_split_pick.res`

Purpose:

- Select instance models and split them into independent models

Controls:

- Model multi-select list
- `Replace current assembly reference`
- `Output to AB_SPLIT folder`
- `Reuse existing _SPLIT model`
- `Select All`
- `Clear`
- `OK`
- `Cancel`

Required states:

- Empty candidate list
- Single selection
- Multi selection
- Safe default state
- Destructive-risk confirmation style

### 3. Shared UI Components

Figma should create reusable desktop-plugin components for:

- Ribbon command button
- Ribbon checkbox option
- Primary button
- Secondary button
- Dangerous button
- Checkbox
- Single-line input
- Multi-line text area
- Simple list
- Data table
- Table cell badge for draft/readonly/mixed
- Summary info bar
- Empty state
- Warning state
- Success/info message modal

## Interaction Guidance

The plugin runs inside Creo and should feel like a serious engineering tool, not a web dashboard.
The Figma direction should prioritize:

- Dense information layout
- High scan efficiency
- Clear affordance for editable vs read-only content
- Strong hierarchy for primary actions
- Stable desktop-style spacing
- Consistent keyboard-friendly focus states

Avoid:

- Mobile-style oversized controls
- Decorative gradients that hurt readability
- Overly rounded consumer UI
- Hidden actions that reduce discoverability

## Figma Page Structure

Recommended Figma file structure:

1. `00 Cover`
2. `01 Design Tokens`
3. `02 Ribbon`
4. `03 Dialog - BOM`
5. `04 Dialog - BOM Value Edit`
6. `05 Dialog - Delete Params`
7. `06 Dialog - Create Views`
8. `07 Dialog - Add Relations`
9. `08 Dialog - Split Instances`
10. `09 Shared Components`
11. `10 States and Edge Cases`
12. `11 Dev Handoff`

## Design Tokens To Define

At minimum, Figma should define:

- Typography scale
- Button heights
- Table row heights
- Checkbox sizes
- Modal width rules
- Icon sizes
- Spacing scale
- Border radius policy
- Border colors
- Background tiers
- Text color hierarchy
- Status colors for info, warning, success, error

## Implementation Notes For Later Dev Handoff

These constraints should be visible in Figma annotations:

- UI tech is Creo Toolkit dialog resources, not HTML/CSS
- Layout is grid-oriented and control-based
- Complex custom painting should be avoided unless strictly necessary
- Table interactions are constrained by native Toolkit controls
- Current BOM editing uses modal value editing instead of inline spreadsheet editing

## What Figma Should Output

Design should provide:

- One polished Ribbon concept
- One full set of modal screens
- All core states
- Icon refresh proposal
- An annotation page marking:
  - control type
  - intended behavior
  - priority
  - implementation risk

## Recommended First Priority

If design bandwidth is limited, do the following first:

1. Ribbon command/option layer
2. BOM 清单主界面
3. BOM 单元格值编辑弹窗
4. 建视图弹窗
5. 其余工具弹窗统一皮肤
