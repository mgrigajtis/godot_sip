#include "sip_client.h"

#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

#include "pjsip_wrapper.h"

namespace godot {

static PJSIPWrapper g_pjsip;

SIPClient::SIPClient() = default;

SIPClient::~SIPClient() {
  shutdown();
}

bool SIPClient::configure_transport(int64_t p_transport_type, int64_t p_port, bool p_verify_tls) {
  return g_pjsip.configure_transport(p_transport_type, p_port, p_verify_tls);
}

bool SIPClient::initialize(const String &p_user_agent) {
  bool ok = g_pjsip.initialize(p_user_agent);
  if (!ok) {
    registration_state = REG_STATE_FAILED;
    return false;
  }

  registration_state = REG_STATE_UNREGISTERED;
  set_process(true);
  return true;
}

void SIPClient::shutdown() {
  set_process(false);
  g_pjsip.shutdown();
  calls.clear();
  registration_state = REG_STATE_UNREGISTERED;
}

bool SIPClient::register_account(const String &p_sip_uri, const String &p_username, const String &p_password, const String &p_registrar) {
  registration_state = REG_STATE_REGISTERING;
  emit_signal("registration_state_changed", registration_state);
  return g_pjsip.register_account(p_sip_uri, p_username, p_password, p_registrar);
}

void SIPClient::unregister_account() {
  g_pjsip.unregister_account();
  registration_state = REG_STATE_UNREGISTERED;
  emit_signal("registration_state_changed", registration_state);
}

Ref<SIPCall> SIPClient::make_call(const String &p_destination_uri) {
  PJSIPCallInfo info = g_pjsip.make_call(p_destination_uri);
  if (info.id < 0) {
    UtilityFunctions::push_warning("Failed to place call. Ensure account is registered.");
    return Ref<SIPCall>();
  }

  Ref<SIPCall> call = get_or_create_call(info.id, info.remote_uri);
  call->_set_internal(info.id, SIPCall::CALL_STATE_CONNECTING, info.remote_uri);
  emit_signal("call_state_changed", call, call->get_state());
  return call;
}

void SIPClient::hangup_call(const Ref<SIPCall> &p_call) {
  if (p_call.is_null()) {
    return;
  }
  g_pjsip.hangup_call(p_call->get_id());
}

void SIPClient::answer_call(const Ref<SIPCall> &p_call) {
  if (p_call.is_null()) {
    return;
  }
  g_pjsip.answer_call(p_call->get_id());
}

void SIPClient::reject_call(const Ref<SIPCall> &p_call) {
  if (p_call.is_null()) {
    return;
  }
  g_pjsip.reject_call(p_call->get_id());
}

PackedStringArray SIPClient::get_audio_input_devices() const {
  return g_pjsip.get_audio_input_devices();
}

PackedStringArray SIPClient::get_audio_output_devices() const {
  return g_pjsip.get_audio_output_devices();
}

bool SIPClient::select_audio_input_device(const String &p_device_name) {
  return g_pjsip.select_audio_input_device(p_device_name);
}

bool SIPClient::select_audio_output_device(const String &p_device_name) {
  return g_pjsip.select_audio_output_device(p_device_name);
}

int64_t SIPClient::get_registration_state() const {
  return registration_state;
}

String SIPClient::get_last_error() const {
  return g_pjsip.get_last_error();
}

void SIPClient::_notification(int p_what) {
  if (p_what != NOTIFICATION_PROCESS) {
    return;
  }

  std::vector<PJSIPEvent> events = g_pjsip.consume_events();
  for (const PJSIPEvent &event : events) {
    if (event.type == PJSIPEvent::EVENT_REGISTRATION_STATE_CHANGED) {
      registration_state = static_cast<RegistrationState>(event.state);
      emit_signal("registration_state_changed", registration_state);
      continue;
    }

    Ref<SIPCall> call = get_or_create_call(event.call_id, event.remote_uri);
    SIPCall::CallState call_state = to_call_state(event.state);
    call->_set_internal(event.call_id, call_state, event.remote_uri);

    if (event.type == PJSIPEvent::EVENT_INCOMING_CALL) {
      emit_signal("incoming_call", call);
    }

    emit_signal("call_state_changed", call, call_state);

    if (call_state == SIPCall::CALL_STATE_ENDED || call_state == SIPCall::CALL_STATE_FAILED) {
      calls.erase(event.call_id);
    }
  }
}

Ref<SIPCall> SIPClient::get_or_create_call(int64_t p_id, const String &p_remote_uri) {
  auto it = calls.find(p_id);
  if (it != calls.end()) {
    return it->second;
  }

  Ref<SIPCall> call;
  call.instantiate();
  call->_set_internal(p_id, SIPCall::CALL_STATE_IDLE, p_remote_uri);
  calls.emplace(p_id, call);
  return call;
}

SIPCall::CallState SIPClient::to_call_state(int64_t p_wrapper_state) const {
  switch (p_wrapper_state) {
    case 1:
      return SIPCall::CALL_STATE_RINGING;
    case 2:
      return SIPCall::CALL_STATE_CONNECTING;
    case 3:
      return SIPCall::CALL_STATE_ACTIVE;
    case 4:
      return SIPCall::CALL_STATE_HELD;
    case 5:
      return SIPCall::CALL_STATE_ENDED;
    case 6:
      return SIPCall::CALL_STATE_FAILED;
    default:
      return SIPCall::CALL_STATE_IDLE;
  }
}

void SIPClient::_bind_methods() {
  ClassDB::bind_method(D_METHOD("configure_transport", "transport_type", "port", "verify_tls"), &SIPClient::configure_transport, DEFVAL(0), DEFVAL(true));
  ClassDB::bind_method(D_METHOD("initialize", "user_agent"), &SIPClient::initialize);
  ClassDB::bind_method(D_METHOD("shutdown"), &SIPClient::shutdown);

  ClassDB::bind_method(D_METHOD("register_account", "sip_uri", "username", "password", "registrar"), &SIPClient::register_account);
  ClassDB::bind_method(D_METHOD("unregister_account"), &SIPClient::unregister_account);

  ClassDB::bind_method(D_METHOD("make_call", "destination_uri"), &SIPClient::make_call);
  ClassDB::bind_method(D_METHOD("hangup_call", "call"), &SIPClient::hangup_call);
  ClassDB::bind_method(D_METHOD("answer_call", "call"), &SIPClient::answer_call);
  ClassDB::bind_method(D_METHOD("reject_call", "call"), &SIPClient::reject_call);

  ClassDB::bind_method(D_METHOD("get_audio_input_devices"), &SIPClient::get_audio_input_devices);
  ClassDB::bind_method(D_METHOD("get_audio_output_devices"), &SIPClient::get_audio_output_devices);
  ClassDB::bind_method(D_METHOD("select_audio_input_device", "device_name"), &SIPClient::select_audio_input_device);
  ClassDB::bind_method(D_METHOD("select_audio_output_device", "device_name"), &SIPClient::select_audio_output_device);

  ClassDB::bind_method(D_METHOD("get_registration_state"), &SIPClient::get_registration_state);
  ClassDB::bind_method(D_METHOD("get_last_error"), &SIPClient::get_last_error);

  ADD_SIGNAL(MethodInfo("registration_state_changed", PropertyInfo(Variant::INT, "state")));
  ADD_SIGNAL(MethodInfo("incoming_call", PropertyInfo(Variant::OBJECT, "call", PROPERTY_HINT_RESOURCE_TYPE, "SIPCall")));
  ADD_SIGNAL(MethodInfo("call_state_changed",
                         PropertyInfo(Variant::OBJECT, "call", PROPERTY_HINT_RESOURCE_TYPE, "SIPCall"),
                         PropertyInfo(Variant::INT, "state")));

  BIND_CONSTANT(REG_STATE_UNREGISTERED);
  BIND_CONSTANT(REG_STATE_REGISTERING);
  BIND_CONSTANT(REG_STATE_REGISTERED);
  BIND_CONSTANT(REG_STATE_FAILED);
  BIND_CONSTANT(TRANSPORT_UDP);
  BIND_CONSTANT(TRANSPORT_TCP);
  BIND_CONSTANT(TRANSPORT_TLS);
}

} // namespace godot
