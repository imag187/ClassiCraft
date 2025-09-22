# res://scripts/DotRenderer.gd
extends Node2D

@onready var net := get_node_or_null("/root/NetWS")  # Autoload from earlier
var tile_px := 32.0
var target_id := ""

func _ready():
	if net == null:
		push_error("Autoload 'NetWS' missing. Add res://NetWS.gd as NetWS."); return
	# redraw whenever state changes
	net.state_updated.connect(func(): queue_redraw())
	net.spawned.connect(func(_id): queue_redraw())
	net.despawned.connect(func(_id): queue_redraw())
	
	print("[DotRenderer] ready. Root node name:", name)

func _draw():
	if net == null: return
	for id in net.actors.keys():
		var a = net.actors[id]
		var p = Vector2(a.x * tile_px, a.y * tile_px)
		var col := Color(0.9,0.95,1)        # others
		if id == net.my_id: col = Color(0.5,1,0.6)     # you = green-ish
		if id == target_id: col = Color(1,0.8,0.5)     # target = orange
		draw_circle(p, 8.0, col)
	print("[DotRenderer] drawing frame… (actors:", net and net.actors.size() or -1, ")")

func _unhandled_input(e):
	if net == null: return
	if e is InputEventMouseButton and e.pressed:
		if e.button_index == MOUSE_BUTTON_LEFT:
			var pos = get_global_mouse_position()
			net.send_move(pos.x / tile_px, pos.y / tile_px)
		elif e.button_index == MOUSE_BUTTON_RIGHT:
			# pick nearest actor as hard target
			var nearest := ""; var best := 1e9
			for id in net.actors.keys():
				var p = Vector2(net.actors[id].x * tile_px, net.actors[id].y * tile_px)
				var d = p.distance_to(get_global_mouse_position())
				if d < best: best = d; nearest = id
			target_id = nearest
			queue_redraw()
	if e.is_action_pressed("mount_toggle"):  net.set_mounted(true)
	if e.is_action_released("mount_toggle"): net.set_mounted(false)
	if e.is_action_pressed("cast_1"):        net.send_cast("smite", target_id)
