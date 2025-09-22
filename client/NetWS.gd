# NetWS.gd — Godot 4.x (no addon needed)
extends Node

var ws: WebSocketPeer = WebSocketPeer.new()
var connected := false

var my_id := ""
var actors := {}  # id -> {x, y, mounted}

signal spawned(id)
signal despawned(id)
signal state_updated()

func _ready():
	var err := ws.connect_to_url("ws://127.0.0.1:2570")
	if err != OK:
		push_error("WS connect error: %s" % err)
		return
	set_process(true)

func _process(_dt):
	# Drive the socket
	ws.poll()

	match ws.get_ready_state():
		WebSocketPeer.STATE_OPEN:
			if not connected:
				connected = true
				print("[ws] OPEN")
			# Read all incoming packets
			while ws.get_available_packet_count() > 0:
				var pkt: PackedByteArray = ws.get_packet()
				var txt := pkt.get_string_from_utf8()
				var msg = JSON.parse_string(txt)
				if msg == null: continue
				_handle_msg(msg)

		WebSocketPeer.STATE_CLOSING:
			# optional: show a spinner / disable input
			pass
		WebSocketPeer.STATE_CLOSED:
			if connected:
				connected = false
				push_warning("[ws] CLOSED: %s (%s)" % [ws.get_close_code(), ws.get_close_reason()])
			set_process(false)  # stop polling; add reconnect logic if you want

func _handle_msg(msg: Dictionary) -> void:
	match msg.get("type", ""):
		"hello":
			my_id = str(msg.id)
		"snapshot":
			actors.clear()
			for a in msg.actors:
				actors[str(a.id)] = {"x": float(a.x), "y": float(a.y), "mounted": bool(a.mounted)}
			emit_signal("state_updated")
		"spawn":
			var a = msg.actor
			actors[str(a.id)] = {"x": float(a.x), "y": float(a.y), "mounted": bool(a.mounted)}
			emit_signal("spawned", str(a.id))
		"despawn":
			actors.erase(str(msg.id))
			emit_signal("despawned", str(msg.id))
		"state":
			for a in msg.actors:
				var id := str(a.id)
				if not actors.has(id):
					actors[id] = {"x": 0.0, "y": 0.0, "mounted": false}
				actors[id].x = float(a.x)
				actors[id].y = float(a.y)
				actors[id].mounted = bool(a.mounted)
			emit_signal("state_updated")
		"damage":
			print("[DMG]", msg)

# --- client → server helpers (send JSON strings) ---
func send_move(x: float, y: float) -> void:
	if ws.get_ready_state() == WebSocketPeer.STATE_OPEN:
		ws.send_text(JSON.stringify({"type": "move", "x": x, "y": y}))

func set_mounted(flag: bool) -> void:
	if ws.get_ready_state() == WebSocketPeer.STATE_OPEN:
		ws.send_text(JSON.stringify({"type": "mount", "mounted": flag}))

func send_cast(ability_id: String, target_id := "") -> void:
	if ws.get_ready_state() == WebSocketPeer.STATE_OPEN:
		ws.send_text(JSON.stringify({"type": "cast", "ability": ability_id, "target": target_id}))
