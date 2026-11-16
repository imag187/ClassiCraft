extends Node2D

const PIXEL_SIZE := Vector2i(16, 8)
const MAP_WIDTH := 520.0
const MAP_HEIGHT := 460.0

var origin := Vector2(284, 105)
var tile_size := Vector2(26, 13)
var artwork: Dictionary = {}
var inventory: Array[Dictionary] = []

@onready var net = get_node("/root/NetWS")
var target_id := ""
var status := "Connecting..."

func _ready() -> void:
	texture_filter = CanvasItem.TEXTURE_FILTER_NEAREST
	net.world_loaded.connect(_load_artwork)
	net.item_collected.connect(_on_item_collected)
	net.state_updated.connect(queue_redraw)
	net.spawned.connect(func(_id): queue_redraw())
	net.despawned.connect(func(_id): queue_redraw())
	net.connection_changed.connect(_on_connection_changed)
	net.damage_received.connect(_on_damage)

func _load_artwork() -> void:
	artwork.clear()
	inventory.clear()
	var extent: float = maxf(1.0, float(net.dungeon.width) + float(net.dungeon.height))
	tile_size = Vector2(2.0, 1.0) * minf(MAP_WIDTH / extent, MAP_HEIGHT * 2.0 / extent)
	origin.x = 284.0 - (float(net.dungeon.width) - float(net.dungeon.height)) * tile_size.x / 4.0
	for tile in net.dungeon.get("tiles", []):
		if tile.pixels.size() != PIXEL_SIZE.x * PIXEL_SIZE.y:
			continue
		var pixels := PackedByteArray()
		for packed in tile.pixels:
			# Studio stores ImGui colors as 0xAABBGGRR.
			var rgba := int(packed)
			for shift in [0, 8, 16, 24]:
				pixels.append((rgba >> shift) & 255)
		var art := Image.create_from_data(PIXEL_SIZE.x, PIXEL_SIZE.y, false, Image.FORMAT_RGBA8, pixels)
		artwork[tile.id] = ImageTexture.create_from_image(art)

func _on_item_collected(item: Dictionary) -> void:
	inventory.append(item)
	status = "Picked up %s." % item.name
	queue_redraw()

func _on_connection_changed(message: String) -> void:
	status = message
	queue_redraw()

func _on_damage(message: Dictionary) -> void:
	if message.src == net.my_id:
		status = "Smite dealt %s damage." % message.amt
	elif message.dst == net.my_id:
		status = "Smite hit you for %s." % message.amt
	queue_redraw()

func _draw() -> void:
	if not is_instance_valid(net):
		return
	var font := ThemeDB.fallback_font
	draw_string(font, Vector2(24, 26), "ClassiCraft / " + str(net.dungeon.get("name", "Test dungeon")), HORIZONTAL_ALIGNMENT_LEFT, -1, 20)
	draw_string(font, Vector2(24, 47), "Click to move   •   Right-click to target   •   Space to smite")
	draw_string(font, Vector2(24, 67), "Hold Shift to sprint   •   E to pick up")
	for cell in net.dungeon.get("cells", []):
		var point := _screen_position(Vector2(cell.x + 0.5, cell.y + 0.5))
		var texture: Texture2D = artwork.get(cell.tile)
		if texture:
			var tint := Color(0.46, 0.46, 0.46) if cell.collision else Color.WHITE
			draw_texture_rect(texture, Rect2(point - tile_size / 2.0, tile_size), false, tint)
		if cell.collision:
			draw_polyline(PackedVector2Array([
				point + Vector2(-tile_size.x / 2, 0), point + Vector2(0, -tile_size.y / 2),
				point + Vector2(tile_size.x / 2, 0), point + Vector2(0, tile_size.y / 2),
				point + Vector2(-tile_size.x / 2, 0)
			]), Color("#26313a"))
	for object in net.dungeon.get("objects", []):
		var point := _screen_position(Vector2(object.x + 0.5, object.y + 0.5))
		var object_art: Texture2D = artwork.get(object.get("artwork", ""))
		if object_art:
			var size := Vector2(32, 16)
			draw_texture_rect(object_art, Rect2(point - size / 2.0, size), false)
			draw_string(font, point + Vector2(17, -5), object.name)
			if target_id == object.id:
				draw_arc(point, 12, 0, TAU, 24, Color("#ffbc70"), 2)
			continue
		match object.kind:
			"item":
				draw_circle(point, 5, Color("#ffd372"))
				draw_string(font, point + Vector2(8, -5), object.name)
			"enemy":
				var color := Color("#ffbc70") if target_id == object.id else Color("#ce7c57")
				draw_circle(point, 9, color)
				draw_string(font, point + Vector2(14, 5), object.name)
			"object":
				draw_rect(Rect2(point - Vector2.ONE * 12, Vector2.ONE * 24), Color("#777f8b"))
			"spawn":
				draw_arc(point, 10, 0, TAU, 24, Color("#89a698"), 2)
	for id in net.actors:
		var actor = net.actors[id]
		var color := Color("#84e1a3") if id == net.my_id else Color("#92beff")
		if id == target_id:
			color = Color("#ffbc70")
		var point := _screen_position(Vector2(actor.x, actor.y))
		draw_circle(point, 8, color)
		if actor.mounted:
			draw_arc(point, 11, 0, TAU, 24, color, 2)
	draw_string(font, Vector2(24, 570), status)
	var names := PackedStringArray()
	for item in inventory:
		names.append(item.name)
	draw_string(font, Vector2(24, 602), "Inventory: " + (", ".join(names) if not names.is_empty() else "empty"))

func _screen_position(tile: Vector2) -> Vector2:
		return origin + Vector2((tile.x - tile.y) * tile_size.x, (tile.x + tile.y) * tile_size.y) / 2.0

func _world_position(screen: Vector2) -> Vector2:
	var relative := (screen - origin) / tile_size
	return Vector2(relative.y + relative.x, relative.y - relative.x)

func _unhandled_input(event: InputEvent) -> void:
	if not net.connected or net.dungeon.is_empty():
		return
	if event is InputEventMouseButton and event.pressed:
		var point := _world_position(get_local_mouse_position())
		if event.button_index == MOUSE_BUTTON_LEFT:
			if Rect2(0, 0, net.dungeon.width, net.dungeon.height).has_point(point):
				net.send_move(point.x, point.y)
		elif event.button_index == MOUSE_BUTTON_RIGHT:
			_select_target(point)
	if event is InputEventKey and not event.echo:
		if event.keycode == KEY_SHIFT:
			net.set_mounted(event.pressed)
		elif event.keycode == KEY_E and event.pressed:
			_pick_up_nearest()
		elif event.keycode == KEY_SPACE and event.pressed and not target_id.is_empty():
			net.send_cast("smite", target_id)

func _pick_up_nearest() -> void:
	var actor = net.actors.get(net.my_id)
	if actor == null:
		return
	var nearest := ""
	var best := 1.5
	for object in net.dungeon.get("objects", []):
		if object.kind == "item":
			var distance := Vector2(actor.x, actor.y).distance_to(Vector2(object.x + 0.5, object.y + 0.5))
			if distance <= best:
				best = distance
				nearest = object.id
	if nearest.is_empty():
		status = "Move closer to an item."
		queue_redraw()
	else:
		net.pick_up(nearest)

func _select_target(point: Vector2) -> void:
	target_id = ""
	var best := 0.55
	for object in net.dungeon.get("objects", []):
		if object.kind == "enemy":
			var distance := point.distance_to(Vector2(object.x + 0.5, object.y + 0.5))
			if distance < best:
				best = distance
				target_id = object.id
	for id in net.actors:
		if id == net.my_id:
			continue
		var actor = net.actors[id]
		var distance := point.distance_to(Vector2(actor.x, actor.y))
		if distance < best:
			best = distance
			target_id = id
	queue_redraw()

func _notification(what: int) -> void:
	if what == NOTIFICATION_APPLICATION_FOCUS_OUT and is_instance_valid(net):
		net.set_mounted(false)
