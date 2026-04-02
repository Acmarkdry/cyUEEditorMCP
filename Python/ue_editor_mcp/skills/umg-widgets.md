# UMG Widgets & MVVM — Workflow Tips

## Widget Creation Flow

```
ue_batch(actions=[
  {action_id: "widget.create", params: {widget_name: "WBP_HUD"}},
  {action_id: "widget.add_component", params: {widget_name: "WBP_HUD", component_type: "CanvasPanel", component_name: "RootCanvas"}},
  {action_id: "widget.add_component", params: {widget_name: "WBP_HUD", component_type: "TextBlock", component_name: "ScoreText", text: "Score: 0", font_size: 24}},
  {action_id: "widget.add_component", params: {widget_name: "WBP_HUD", component_type: "Button", component_name: "RestartBtn"}},
  {action_id: "widget.add_child", params: {widget_name: "WBP_HUD", child: "ScoreText", parent: "RootCanvas"}},
  {action_id: "widget.add_child", params: {widget_name: "WBP_HUD", child: "RestartBtn", parent: "RootCanvas"}},
  {action_id: "blueprint.compile", params: {blueprint_name: "WBP_HUD"}}
])
```

## Supported Component Types (24)

TextBlock, Button, Image, Border, Overlay, HorizontalBox, VerticalBox,
Slider, ProgressBar, SizeBox, ScaleBox, CanvasPanel, ComboBox, CheckBox,
SpinBox, EditableTextBox, ScrollBox, WidgetSwitcher, BackgroundBlur,
UniformGridPanel, Spacer, RichTextBlock, WrapBox, CircularThrobber

## MVVM Workflow

```
ue_batch(actions=[
  {action_id: "widget.mvvm_add_viewmodel", params: {widget_name: "WBP_HUD", viewmodel_class: "StatusViewModel", viewmodel_name: "StatusVM", creation_type: "CreateInstance"}},
  {action_id: "widget.mvvm_add_binding", params: {widget_name: "WBP_HUD", viewmodel_name: "StatusVM", source_property: "HealthPercent", destination_widget: "HealthBar", destination_property: "Percent", binding_mode: "OneWayToDestination"}}
])
```

## Key Patterns

- Components are added to root by default — use `widget.add_child` to build hierarchy
- `widget.reparent` moves multiple widgets into a container at once
- `widget.get_tree` returns full hierarchy with slot info and render transforms
- `widget.set_properties` handles slot, visibility, alignment, and padding in one call
- Binding modes: OneTimeToDestination, OneWayToDestination, TwoWay, OneTimeToSource, OneWayToSource

## Enhanced Input System

```
ue_batch(actions=[
  {action_id: "input.create_action", params: {name: "IA_Move", value_type: "Axis2D"}},
  {action_id: "input.create_mapping_context", params: {name: "IMC_Default"}},
  {action_id: "input.add_key_mapping", params: {context_name: "IMC_Default", action_name: "IA_Move", key: "W", modifiers: ["SwizzleYXZ"]}},
  {action_id: "input.add_key_mapping", params: {context_name: "IMC_Default", action_name: "IA_Move", key: "S", modifiers: ["Negate", "SwizzleYXZ"]}}
])
```
