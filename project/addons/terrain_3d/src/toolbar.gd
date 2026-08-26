# Copyright © 2023-2026 Cory Petkovsek, Roope Palmroos, and Contributors.
# Toolbar for Terrain3D
extends VFlowContainer

signal tool_changed(p_tool: Terrain3DEditor.Tool, p_operation: Terrain3DEditor.Operation)

const ICON_REGION_ADD: String = "res://addons/terrain_3d/icons/region_add.svg"
const ICON_REGION_REMOVE: String = "res://addons/terrain_3d/icons/region_remove.svg"
const ICON_HEIGHT_ADD: String = "res://addons/terrain_3d/icons/height_add.svg"
const ICON_HEIGHT_SUB: String = "res://addons/terrain_3d/icons/height_sub.svg"
const ICON_HEIGHT_FLAT: String = "res://addons/terrain_3d/icons/height_flat.svg"
const ICON_HEIGHT_SLOPE: String = "res://addons/terrain_3d/icons/height_slope.svg"
const ICON_HEIGHT_SMOOTH: String = "res://addons/terrain_3d/icons/height_smooth.svg"
const ICON_PAINT_TEXTURE: String = "res://addons/terrain_3d/icons/texture_paint.svg"
const ICON_SPRAY_TEXTURE: String = "res://addons/terrain_3d/icons/texture_spray.svg"
const ICON_COLOR: String = "res://addons/terrain_3d/icons/color_paint.svg"
const ICON_WETNESS: String = "res://addons/terrain_3d/icons/wetness.svg"
const ICON_AUTOSHADER: String = "res://addons/terrain_3d/icons/autoshader.svg"
const ICON_HOLES: String = "res://addons/terrain_3d/icons/holes.svg"
const ICON_NAVIGATION: String = "res://addons/terrain_3d/icons/navigation.svg"
const ICON_INSTANCER: String = "res://addons/terrain_3d/icons/multimesh.svg"

var add_tool_group: ButtonGroup = ButtonGroup.new()
var sub_tool_group: ButtonGroup = ButtonGroup.new()
var buttons: Dictionary
var _allowed_tools := PackedInt32Array()
var _showing_add_buttons := true


func _init() -> void:
	set_custom_minimum_size(Vector2(20, 0))


func _ready() -> void:
	add_tool_group.pressed.connect(_on_tool_selected)
	sub_tool_group.pressed.connect(_on_tool_selected)

	add_tool_button({ "tool":Terrain3DEditor.REGION, 
		"add_text":"Add Region (E)", "add_op":Terrain3DEditor.ADD, "add_icon":ICON_REGION_ADD,
		"sub_text":"Remove Region", "sub_op":Terrain3DEditor.SUBTRACT, "sub_icon":ICON_REGION_REMOVE })
	
	add_child(HSeparator.new())
	
	add_tool_button({ "tool":Terrain3DEditor.SCULPT, 
		"add_text":"Raise (R)", "add_op":Terrain3DEditor.ADD, "add_icon":ICON_HEIGHT_ADD,
		"sub_text":"Lower (R)", "sub_op":Terrain3DEditor.SUBTRACT, "sub_icon":ICON_HEIGHT_SUB })

	add_tool_button({ "tool":Terrain3DEditor.SCULPT, 
		"add_text":"Smooth (Shift)", "add_op":Terrain3DEditor.AVERAGE, "add_icon":ICON_HEIGHT_SMOOTH })

	add_tool_button({ "tool":Terrain3DEditor.HEIGHT, 
		"add_text":"Height (H)", "add_op":Terrain3DEditor.ADD, "add_icon":ICON_HEIGHT_FLAT,
		"sub_text":"Height (H)", "sub_op":Terrain3DEditor.SUBTRACT, "sub_icon":ICON_HEIGHT_FLAT })

	add_tool_button({ "tool":Terrain3DEditor.SCULPT, 
		"add_text":"Slope (S)", "add_op":Terrain3DEditor.GRADIENT, "add_icon":ICON_HEIGHT_SLOPE })

	add_child(HSeparator.new())

	add_tool_button({ "tool":Terrain3DEditor.TEXTURE, 
		"add_text":"Paint Texture (B)", "add_op":Terrain3DEditor.REPLACE, "add_icon":ICON_PAINT_TEXTURE })

	add_tool_button({ "tool":Terrain3DEditor.TEXTURE, 
		"add_text":"Spray Texture (V)", "add_op":Terrain3DEditor.ADD, "add_icon":ICON_SPRAY_TEXTURE })

	add_tool_button({ "tool":Terrain3DEditor.AUTOSHADER,
		"add_text":"Paint Autoshader (A)", "add_op":Terrain3DEditor.ADD, "add_icon":ICON_AUTOSHADER,
		"sub_text":"Disable Autoshader (A)", "sub_op":Terrain3DEditor.SUBTRACT })

	add_child(HSeparator.new())

	add_tool_button({ "tool":Terrain3DEditor.COLOR,
		"add_text":"Paint Color (C)", "add_op":Terrain3DEditor.ADD, "add_icon":ICON_COLOR,
		"sub_text":"Remove Color (C)", "sub_op":Terrain3DEditor.SUBTRACT })
	
	add_tool_button({ "tool":Terrain3DEditor.ROUGHNESS,
		"add_text":"Paint Wetness (W)", "add_op":Terrain3DEditor.ADD, "add_icon":ICON_WETNESS,
		"sub_text":"Remove Wetness (W)", "sub_op":Terrain3DEditor.SUBTRACT })

	add_child(HSeparator.new())

	add_tool_button({ "tool":Terrain3DEditor.HOLES,
		"add_text":"Add Holes (X)", "add_op":Terrain3DEditor.ADD, "add_icon":ICON_HOLES,
		"sub_text":"Remove Holes (X)", "sub_op":Terrain3DEditor.SUBTRACT })

	add_tool_button({ "tool":Terrain3DEditor.NAVIGATION,
		"add_text":"Paint Navigable Area (N)", "add_op":Terrain3DEditor.ADD, "add_icon":ICON_NAVIGATION,
		"sub_text":"Remove Navigable Area (N)", "sub_op":Terrain3DEditor.SUBTRACT })

	add_tool_button({ "tool":Terrain3DEditor.INSTANCER,
		"add_text":"Instance Meshes (I)", "add_op":Terrain3DEditor.ADD, "add_icon":ICON_INSTANCER,
		"sub_text":"Remove Meshes (I)", "sub_op":Terrain3DEditor.SUBTRACT })

	# Select first button
	var buttons: Array[BaseButton] = add_tool_group.get_buttons()
	buttons[0].set_pressed(true)
	show_add_buttons(true)


func add_tool_button(p_params: Dictionary) -> void:
	# Additive button
	var button := Button.new()
	var name_str: String = p_params.get("add_text", "blank").get_slice('(', 0).to_pascal_case()
	button.set_name(name_str)
	button.set_meta("Tool", p_params.get("tool", 0))
	button.set_meta("Operation", p_params.get("add_op", 0))
	button.set_meta("ID", add_tool_group.get_buttons().size() + 1)
	button.set_tooltip_text(p_params.get("add_text", "blank"))
	button.set_button_icon(load(p_params.get("add_icon")))
	button.set_flat(true)
	button.set_toggle_mode(true)
	button.set_h_size_flags(SIZE_SHRINK_END)
	button.set_button_group(p_params.get("group", add_tool_group))
	add_child(button, true)
	buttons[button.get_name()] = button

	# Subtractive button
	var button2: Button
	if p_params.has("sub_text"):
		button2 = Button.new()
		name_str = p_params.get("sub_text", "blank").get_slice('(', 0).to_pascal_case()
		button2.set_name(name_str)
		button2.set_meta("Tool", p_params.get("tool", 0))
		button2.set_meta("Operation", p_params.get("sub_op", 0))
		button2.set_meta("ID", button.get_meta("ID"))
		button2.set_tooltip_text(p_params.get("sub_text", "blank"))
		button2.set_button_icon(load(p_params.get("sub_icon", p_params.get("add_icon"))))
		button2.set_flat(true)
		button2.set_toggle_mode(true)
		button2.set_h_size_flags(SIZE_SHRINK_END)
	else:
		button2 = button.duplicate()
	button2.set_button_group(p_params.get("group", sub_tool_group))
	add_child(button2, true)
	buttons[button2.get_name()] = button


func get_button(p_name: String) -> Button:
	return buttons.get(p_name, null)


func set_allowed_tools(tools: PackedInt32Array) -> void:
	_allowed_tools = tools.duplicate()
	_update_button_visibility()
	var selected := add_tool_group.get_pressed_button()
	if selected == null or not is_tool_allowed(selected.get_meta("Tool", Terrain3DEditor.TOOL_MAX)):
		_select_first_allowed_tool()


func is_tool_allowed(tool: int) -> bool:
	return _allowed_tools.is_empty() or _allowed_tools.has(tool)


func show_add_buttons(p_enable: bool) -> void:
	_showing_add_buttons = p_enable
	_update_button_visibility()


func _update_button_visibility() -> void:
	for button in add_tool_group.get_buttons():
		button.visible = _showing_add_buttons and is_tool_allowed(button.get_meta("Tool"))
	for button in sub_tool_group.get_buttons():
		button.visible = not _showing_add_buttons and is_tool_allowed(button.get_meta("Tool"))
	_update_separator_visibility()


func _update_separator_visibility() -> void:
	if _allowed_tools.is_empty():
		for child in get_children():
			if child is HSeparator:
				child.visible = true
		return
	var seen_visible_button := false
	var pending_separators: Array[Control] = []
	for child in get_children():
		if child is HSeparator:
			pending_separators.append(child)
		elif child is BaseButton and child.visible:
			for separator in pending_separators:
				separator.visible = seen_visible_button
			pending_separators.clear()
			seen_visible_button = true
	for separator in pending_separators:
		separator.visible = false


func _select_first_allowed_tool() -> void:
	for button in add_tool_group.get_buttons():
		if is_tool_allowed(button.get_meta("Tool", Terrain3DEditor.TOOL_MAX)):
			select_tool_button(button.name)
			return


func select_tool_button(button_name: String) -> bool:
	var button := get_button(button_name)
	if button == null:
		return false
	return _select_tool_button(button)


func select_tool(tool: int, operation: int) -> bool:
	for button in add_tool_group.get_buttons() + sub_tool_group.get_buttons():
		if button.get_meta("Tool") == tool and button.get_meta("Operation") == operation:
			return _select_tool_button(button)
	return false


func _select_tool_button(button: BaseButton) -> bool:
	if not is_tool_allowed(button.get_meta("Tool")):
		return false
	for grouped_button in button.get_button_group().get_buttons():
		grouped_button.set_pressed_no_signal(grouped_button == button)
	_on_tool_selected(button)
	return true


func _on_tool_selected(p_button: BaseButton) -> void:
	if not is_tool_allowed(p_button.get_meta("Tool", Terrain3DEditor.TOOL_MAX)):
		return
	# Select same tool on negative bar
	var group: ButtonGroup = p_button.get_button_group()
	var change_group: ButtonGroup = add_tool_group if group == sub_tool_group else sub_tool_group
	var id: int = p_button.get_meta("ID", -2)
	for button in change_group.get_buttons():
		button.set_pressed_no_signal(button.get_meta("ID", -1) == id)
	emit_signal("tool_changed", p_button.get_meta("Tool", Terrain3DEditor.TOOL_MAX), p_button.get_meta("Operation", Terrain3DEditor.OP_MAX))
