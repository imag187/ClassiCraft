# res://scripts/TestController.gd
extends Node2D

@onready var net := get_node_or_null("/root/NetWS")
@onready var actors_layer := Node2D.new()
var tile_px := 32.0
var sprites := {} # id -> Sprite2D
var target_id := ""

func _ready():
	if net == null:
		push_error("Autoload 'NetWS' missing. Add res://NetWS.gd as NetWS."); return
	add_child(actors_layer)
	net.spawned.connect(_on_spawned)
	net.despawned.connect(_on_despawned)
	net.state_updated.connect(_on_state)

func _on_spawned(id): _ensure_sprite(id)
func _on_despawned(id):
	if sprites.has(id): sprites[id].queue_free(); sprites.erase(id)
func _on_state():
	for id in net.actors.keys():
		_ensure_sprite(id)
		var s: Sprite2D = sprites[id]
		var a = net.actors[id]
		s.position = Vector2(a.x * tile_px, a.y * tile_px)
		s.modulate = Color(1,1,1,1) if id != target_id else Color(1,0.8,0.5,1)

func _ensure_sprite(id:String):
	if sprites.has(id): return
	var s := Sprite2D.new()
	if ResourceLoader.exists("res://assets/hero_32.png"):
		s.texture = load("res://assets/hero_32.png")
	actors_layer.add_child(s)
	sprites[id] = s

func _unhandled_input(e):
	if net == null: return
	if e is InputEventMouseButton and e.pressed:
		if e.button_index == MOUSE_BUTTON_LEFT:
			var pos = get_global_mouse_position()
			net.send_move(pos.x / tile_px, pos.y / tile_px)
		elif e.button_index == MOUSE_BUTTON_RIGHT:
			var nearest := ""; var best := 1e9
			for id in net.actors.keys():
				var p = Vector2(net.actors[id].x * tile_px, net.actors[id].y * tile_px)
				var d = p.distance_to(get_global_mouse_position())
				if d < best: best = d; nearest = id
			target_id = nearest
	if e.is_action_pressed("mount_toggle"):  net.set_mounted(true)
	if e.is_action_released("mount_toggle"): net.set_mounted(false)
	if e.is_action_pressed("cast_1"):        net.send_cast("smite", target_id)
