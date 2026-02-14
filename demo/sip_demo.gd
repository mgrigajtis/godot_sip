extends Control

const USER_AGENT := "GodotSIP/0.1"
const REG_STATE_UNREGISTERED := 0
const REG_STATE_REGISTERING := 1
const REG_STATE_REGISTERED := 2
const REG_STATE_FAILED := 3
const CALL_STATE_IDLE := 0
const CALL_STATE_RINGING := 1
const CALL_STATE_CONNECTING := 2
const CALL_STATE_ACTIVE := 3
const CALL_STATE_HELD := 4
const CALL_STATE_ENDED := 5
const CALL_STATE_FAILED := 6
const TRANSPORT_UDP := 0
const TRANSPORT_TCP := 1
const TRANSPORT_TLS := 2

@onready var sip_uri_edit: LineEdit = %SipUriEdit
@onready var username_edit: LineEdit = %UsernameEdit
@onready var password_edit: LineEdit = %PasswordEdit
@onready var registrar_edit: LineEdit = %RegistrarEdit
@onready var destination_edit: LineEdit = %DestinationEdit
@onready var registration_label: Label = %RegistrationStateLabel
@onready var input_devices_option: OptionButton = %InputDevicesOption
@onready var output_devices_option: OptionButton = %OutputDevicesOption
@onready var log_output: RichTextLabel = %LogOutput

var sip_client
var active_call = null
var calls_by_id: Dictionary = {}
var sip_initialized := false

func _ready() -> void:
  sip_client = ClassDB.instantiate("SIPClient")
  if sip_client == null:
    _log("Failed to instantiate SIPClient. Check extension load paths.")
    return
  add_child(sip_client)

  sip_client.registration_state_changed.connect(_on_registration_state_changed)
  sip_client.incoming_call.connect(_on_incoming_call)
  sip_client.call_state_changed.connect(_on_call_state_changed)
  _set_registration_label(REG_STATE_UNREGISTERED)
  _log("SIP client ready. Transport is configured when you press Register.")

func _on_register_pressed() -> void:
  var registrar = registrar_edit.text.strip_edges()
  _log("Initializing SIP transport using registrar: %s" % registrar)
  if not _ensure_initialized_for_registrar(registrar):
    _log("Failed to initialize SIP transport. %s" % sip_client.get_last_error())
    return

  var reg_ok = sip_client.register_account(
    sip_uri_edit.text.strip_edges(),
    username_edit.text.strip_edges(),
    password_edit.text,
    registrar
  )
  if not reg_ok:
    _log("Registration request failed to submit.")

func _on_unregister_pressed() -> void:
  sip_client.unregister_account()
  _log("Unregister requested.")

func _on_call_pressed() -> void:
  var destination = destination_edit.text.strip_edges()
  if destination.is_empty():
    _log("Destination URI is required.")
    return

  var call = sip_client.make_call(destination)
  if call == null:
    _log("Call creation failed.")
    return

  active_call = call
  calls_by_id[call.get_id()] = call
  _log("Calling %s (id=%d)." % [call.get_remote_uri(), call.get_id()])

func _on_answer_pressed() -> void:
  if active_call == null:
    _log("No active call to answer.")
    return
  sip_client.answer_call(active_call)

func _on_reject_pressed() -> void:
  if active_call == null:
    _log("No active call to reject.")
    return
  sip_client.reject_call(active_call)

func _on_hangup_pressed() -> void:
  if active_call == null:
    _log("No active call to hang up.")
    return
  sip_client.hangup_call(active_call)

func _on_refresh_devices_pressed() -> void:
  if not sip_initialized:
    _log("Initialize/register first to list audio devices.")
    return
  _refresh_audio_devices()

func _on_input_devices_option_item_selected(index: int) -> void:
  var name = input_devices_option.get_item_text(index)
  var ok = sip_client.select_audio_input_device(name)
  _log("Input device %s: %s" % [name, "ok" if ok else "failed"])

func _on_output_devices_option_item_selected(index: int) -> void:
  var name = output_devices_option.get_item_text(index)
  var ok = sip_client.select_audio_output_device(name)
  _log("Output device %s: %s" % [name, "ok" if ok else "failed"])

func _on_registration_state_changed(state: int) -> void:
  _set_registration_label(state)
  _log("Registration state -> %s" % _registration_state_name(state))

func _on_incoming_call(call) -> void:
  active_call = call
  calls_by_id[call.get_id()] = call
  _log("Incoming call from %s (id=%d)." % [call.get_remote_uri(), call.get_id()])

func _on_call_state_changed(call, state: int) -> void:
  calls_by_id[call.get_id()] = call
  _log("Call %d -> %s" % [call.get_id(), _call_state_name(state)])

  if state == CALL_STATE_ENDED or state == CALL_STATE_FAILED:
    calls_by_id.erase(call.get_id())
    if active_call != null and active_call.get_id() == call.get_id():
      active_call = null

func _refresh_audio_devices() -> void:
  input_devices_option.clear()
  output_devices_option.clear()

  var input_devices = sip_client.get_audio_input_devices()
  for i in input_devices.size():
    input_devices_option.add_item(input_devices[i])

  var output_devices = sip_client.get_audio_output_devices()
  for i in output_devices.size():
    output_devices_option.add_item(output_devices[i])

  if input_devices.size() > 0:
    input_devices_option.select(0)
  if output_devices.size() > 0:
    output_devices_option.select(0)

  _log("Audio devices refreshed. inputs=%d outputs=%d" % [input_devices.size(), output_devices.size()])

func _ensure_initialized_for_registrar(registrar: String) -> bool:
  if sip_initialized:
    return true

  var transport := TRANSPORT_UDP
  var default_port := 0
  var verify_tls := true
  var registrar_lc := registrar.to_lower()

  if registrar_lc.begins_with("sips:"):
    transport = TRANSPORT_TLS
    default_port = 5061
  elif registrar_lc.begins_with("sip:"):
    transport = TRANSPORT_TCP
    default_port = 5060

  var port := _extract_registrar_port(registrar, default_port)
  if not sip_client.configure_transport(transport, port, verify_tls):
    _log("Transport configure failed: %s" % sip_client.get_last_error())
    return false

  if not sip_client.initialize(USER_AGENT):
    _log("SIP initialize failed: %s" % sip_client.get_last_error())
    return false

  sip_initialized = true
  _log("SIP initialized with %s transport on port %d." % [_transport_name(transport), port])
  _refresh_audio_devices()
  return true

func _extract_registrar_port(registrar: String, default_port: int) -> int:
  var addr = registrar.strip_edges()

  var scheme_idx = addr.find(":")
  if scheme_idx != -1:
    addr = addr.substr(scheme_idx + 1)
  if addr.begins_with("//"):
    addr = addr.substr(2)

  var at_idx = addr.rfind("@")
  if at_idx != -1:
    addr = addr.substr(at_idx + 1)

  var param_idx = addr.find(";")
  if param_idx != -1:
    addr = addr.substr(0, param_idx)

  var query_idx = addr.find("?")
  if query_idx != -1:
    addr = addr.substr(0, query_idx)

  var colon_idx = addr.rfind(":")
  if colon_idx == -1 or colon_idx >= addr.length() - 1:
    return default_port

  var maybe_port = addr.substr(colon_idx + 1)
  if maybe_port.is_valid_int():
    var parsed = int(maybe_port)
    if parsed > 0 and parsed <= 65535:
      return parsed
  return default_port

func _transport_name(transport: int) -> String:
  match transport:
    TRANSPORT_UDP:
      return "UDP"
    TRANSPORT_TCP:
      return "TCP"
    TRANSPORT_TLS:
      return "TLS"
    _:
      return "Unknown"

func _set_registration_label(state: int) -> void:
  registration_label.text = "Registration: %s" % _registration_state_name(state)

func _registration_state_name(state: int) -> String:
  match state:
    REG_STATE_UNREGISTERED:
      return "Unregistered"
    REG_STATE_REGISTERING:
      return "Registering"
    REG_STATE_REGISTERED:
      return "Registered"
    REG_STATE_FAILED:
      return "Failed"
    _:
      return "Unknown"

func _call_state_name(state: int) -> String:
  match state:
    CALL_STATE_IDLE:
      return "Idle"
    CALL_STATE_RINGING:
      return "Ringing"
    CALL_STATE_CONNECTING:
      return "Connecting"
    CALL_STATE_ACTIVE:
      return "Active"
    CALL_STATE_HELD:
      return "Held"
    CALL_STATE_ENDED:
      return "Ended"
    CALL_STATE_FAILED:
      return "Failed"
    _:
      return "Unknown"

func _log(message: String) -> void:
  log_output.append_text("[%s] %s\n" % [Time.get_time_string_from_system(), message])
