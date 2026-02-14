#pragma once

#include <godot_cpp/variant/packed_string_array.hpp>
#include <godot_cpp/variant/string.hpp>
#include <vector>

namespace godot {

struct PJSIPCallInfo {
  int64_t id = -1;
  String remote_uri;
};

struct PJSIPEvent {
  enum Type {
    EVENT_INCOMING_CALL = 0,
    EVENT_CALL_STATE_CHANGED,
    EVENT_REGISTRATION_STATE_CHANGED
  };

  Type type = EVENT_CALL_STATE_CHANGED;
  int64_t call_id = -1;
  int64_t state = 0;
  String remote_uri;
};

class PJSIPWrapper {
public:
  enum TransportType {
    TRANSPORT_UDP = 0,
    TRANSPORT_TCP = 1,
    TRANSPORT_TLS = 2
  };

  bool configure_transport(int64_t p_transport_type, int64_t p_port, bool p_verify_tls);
  bool initialize(const String &p_user_agent);
  void shutdown();

  bool register_account(const String &p_sip_uri, const String &p_username, const String &p_password, const String &p_registrar);
  void unregister_account();

  PJSIPCallInfo make_call(const String &p_destination_uri);
  void hangup_call(int64_t p_call_id);
  void answer_call(int64_t p_call_id);
  void reject_call(int64_t p_call_id);

  PackedStringArray get_audio_input_devices() const;
  PackedStringArray get_audio_output_devices() const;
  bool select_audio_input_device(const String &p_device_name);
  bool select_audio_output_device(const String &p_device_name);

  std::vector<PJSIPEvent> consume_events();

  bool is_available() const;
  String get_last_error() const;

private:
  bool initialized = false;
};

} // namespace godot
