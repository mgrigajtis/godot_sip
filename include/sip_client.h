#pragma once

#include <godot_cpp/classes/node.hpp>
#include <godot_cpp/core/binder_common.hpp>
#include <godot_cpp/variant/packed_string_array.hpp>
#include <unordered_map>

#include "sip_call.h"

namespace godot {

class SIPClient : public Node {
  GDCLASS(SIPClient, Node)

public:
  enum RegistrationState {
    REG_STATE_UNREGISTERED = 0,
    REG_STATE_REGISTERING,
    REG_STATE_REGISTERED,
    REG_STATE_FAILED
  };

  enum TransportType {
    TRANSPORT_UDP = 0,
    TRANSPORT_TCP = 1,
    TRANSPORT_TLS = 2
  };

  SIPClient();
  ~SIPClient();

  bool configure_transport(int64_t p_transport_type, int64_t p_port, bool p_verify_tls);
  bool initialize(const String &p_user_agent);
  void shutdown();

  bool register_account(const String &p_sip_uri, const String &p_username, const String &p_password, const String &p_registrar);
  void unregister_account();

  Ref<SIPCall> make_call(const String &p_destination_uri);
  void hangup_call(const Ref<SIPCall> &p_call);
  void answer_call(const Ref<SIPCall> &p_call);
  void reject_call(const Ref<SIPCall> &p_call);

  PackedStringArray get_audio_input_devices() const;
  PackedStringArray get_audio_output_devices() const;
  bool select_audio_input_device(const String &p_device_name);
  bool select_audio_output_device(const String &p_device_name);

  int64_t get_registration_state() const;
  String get_last_error() const;

protected:
  static void _bind_methods();
  void _notification(int p_what);

private:
  Ref<SIPCall> get_or_create_call(int64_t p_id, const String &p_remote_uri);
  SIPCall::CallState to_call_state(int64_t p_wrapper_state) const;

  RegistrationState registration_state = REG_STATE_UNREGISTERED;
  std::unordered_map<int64_t, Ref<SIPCall>> calls;
};

} // namespace godot
