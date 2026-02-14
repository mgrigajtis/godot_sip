#pragma once

#include <godot_cpp/classes/ref_counted.hpp>
#include <godot_cpp/core/binder_common.hpp>

namespace godot {

class SIPCall : public RefCounted {
  GDCLASS(SIPCall, RefCounted)

public:
  enum CallState {
    CALL_STATE_IDLE = 0,
    CALL_STATE_RINGING,
    CALL_STATE_CONNECTING,
    CALL_STATE_ACTIVE,
    CALL_STATE_HELD,
    CALL_STATE_ENDED,
    CALL_STATE_FAILED
  };

  SIPCall();

  int64_t get_id() const;
  int64_t get_state() const;
  String get_remote_uri() const;

  void _set_internal(int64_t p_id, CallState p_state, const String &p_remote_uri);

protected:
  static void _bind_methods();

private:
  int64_t id = -1;
  CallState state = CALL_STATE_IDLE;
  String remote_uri;
};

} // namespace godot