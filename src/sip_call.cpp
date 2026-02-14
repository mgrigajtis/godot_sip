#include "sip_call.h"

#include <godot_cpp/core/class_db.hpp>

namespace godot {

SIPCall::SIPCall() = default;

int64_t SIPCall::get_id() const {
  return id;
}

int64_t SIPCall::get_state() const {
  return state;
}

String SIPCall::get_remote_uri() const {
  return remote_uri;
}

void SIPCall::_set_internal(int64_t p_id, CallState p_state, const String &p_remote_uri) {
  id = p_id;
  state = p_state;
  remote_uri = p_remote_uri;
}

void SIPCall::_bind_methods() {
  ClassDB::bind_method(D_METHOD("get_id"), &SIPCall::get_id);
  ClassDB::bind_method(D_METHOD("get_state"), &SIPCall::get_state);
  ClassDB::bind_method(D_METHOD("get_remote_uri"), &SIPCall::get_remote_uri);

  BIND_CONSTANT(CALL_STATE_IDLE);
  BIND_CONSTANT(CALL_STATE_RINGING);
  BIND_CONSTANT(CALL_STATE_CONNECTING);
  BIND_CONSTANT(CALL_STATE_ACTIVE);
  BIND_CONSTANT(CALL_STATE_HELD);
  BIND_CONSTANT(CALL_STATE_ENDED);
  BIND_CONSTANT(CALL_STATE_FAILED);
}

} // namespace godot
