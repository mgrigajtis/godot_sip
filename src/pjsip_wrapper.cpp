#include "pjsip_wrapper.h"

#include <godot_cpp/variant/utility_functions.hpp>

#include <chrono>
#include <mutex>
#include <string>
#include <unordered_map>

#ifdef GODOT_SIP_HAS_PJSIP
#include <pjsua-lib/pjsua.h>
#endif

namespace godot {

#ifdef GODOT_SIP_HAS_PJSIP
namespace {

struct WrapperRuntime {
  std::mutex mutex;
  std::vector<PJSIPEvent> events;
  pjsua_acc_id account_id = PJSUA_INVALID_ID;
  int capture_dev = PJMEDIA_AUD_DEFAULT_CAPTURE_DEV;
  int playback_dev = PJMEDIA_AUD_DEFAULT_PLAYBACK_DEV;
  int transport_type = PJSIP_TRANSPORT_UDP;
  int transport_port = 5060;
  bool transport_port_explicit = false;
  pjsua_transport_id transport_id = PJSUA_INVALID_ID;
  bool verify_tls = true;
  std::string stun_server;
  std::string user_agent;
  std::string last_error;
  bool started = false;
  std::unordered_map<int, std::chrono::steady_clock::time_point> active_calls_for_stats;
};

WrapperRuntime g_runtime;

bool apply_audio_devices();

String pj_to_string(const pj_str_t &p_value) {
  if (p_value.ptr == nullptr || p_value.slen <= 0) {
    return String();
  }
  return String::utf8(p_value.ptr, static_cast<int>(p_value.slen));
}

String transport_name(int p_transport_type) {
  switch (p_transport_type) {
    case PJSIP_TRANSPORT_UDP:
      return "UDP";
    case PJSIP_TRANSPORT_TCP:
      return "TCP";
    case PJSIP_TRANSPORT_TLS:
      return "TLS";
    default:
      return "Unknown";
  }
}

int64_t map_call_state(pjsip_inv_state p_state, int p_last_status_code) {
  switch (p_state) {
    case PJSIP_INV_STATE_CALLING:
      return 2; // CALL_STATE_CONNECTING
    case PJSIP_INV_STATE_INCOMING:
      return 1; // CALL_STATE_RINGING
    case PJSIP_INV_STATE_EARLY:
      return 1; // CALL_STATE_RINGING
    case PJSIP_INV_STATE_CONNECTING:
      return 2; // CALL_STATE_CONNECTING
    case PJSIP_INV_STATE_CONFIRMED:
      return 3; // CALL_STATE_ACTIVE
    case PJSIP_INV_STATE_DISCONNECTED:
      return (p_last_status_code >= 400) ? 6 : 5; // FAILED or ENDED
    default:
      return 0; // CALL_STATE_IDLE
  }
}

void push_event(const PJSIPEvent &p_event) {
  std::lock_guard<std::mutex> lock(g_runtime.mutex);
  g_runtime.events.push_back(p_event);
}

String get_call_remote_uri(pjsua_call_id p_call_id) {
  pjsua_call_info info;
  if (pjsua_call_get_info(p_call_id, &info) != PJ_SUCCESS) {
    return String();
  }
  return pj_to_string(info.remote_info);
}

void on_incoming_call(pjsua_acc_id p_acc_id, pjsua_call_id p_call_id, pjsip_rx_data *p_rdata) {
  (void)p_acc_id;
  (void)p_rdata;

  PJSIPEvent event;
  event.type = PJSIPEvent::EVENT_INCOMING_CALL;
  event.call_id = p_call_id;
  event.state = 1; // CALL_STATE_RINGING
  event.remote_uri = get_call_remote_uri(p_call_id);
  push_event(event);
}

void on_call_state(pjsua_call_id p_call_id, pjsip_event *p_event) {
  (void)p_event;

  pjsua_call_info info;
  if (pjsua_call_get_info(p_call_id, &info) != PJ_SUCCESS) {
    return;
  }

  PJSIPEvent event;
  event.type = PJSIPEvent::EVENT_CALL_STATE_CHANGED;
  event.call_id = p_call_id;
  event.state = map_call_state(info.state, info.last_status);
  event.remote_uri = pj_to_string(info.remote_info);
  push_event(event);

  if (info.state == PJSIP_INV_STATE_CONFIRMED) {
    std::lock_guard<std::mutex> lock(g_runtime.mutex);
    g_runtime.active_calls_for_stats[p_call_id] = std::chrono::steady_clock::time_point::min();
  } else if (info.state == PJSIP_INV_STATE_DISCONNECTED) {
    std::lock_guard<std::mutex> lock(g_runtime.mutex);
    g_runtime.active_calls_for_stats.erase(p_call_id);
  }
}

void on_call_media_state(pjsua_call_id p_call_id) {
  if (!apply_audio_devices()) {
    UtilityFunctions::push_warning("Failed to apply audio devices when call media became active.");
  }
  if (!pjsua_snd_is_active()) {
    if (pjsua_set_snd_dev(PJMEDIA_AUD_DEFAULT_CAPTURE_DEV, PJMEDIA_AUD_DEFAULT_PLAYBACK_DEV) == PJ_SUCCESS) {
      g_runtime.capture_dev = PJMEDIA_AUD_DEFAULT_CAPTURE_DEV;
      g_runtime.playback_dev = PJMEDIA_AUD_DEFAULT_PLAYBACK_DEV;
    }
  }

  pjsua_call_info info;
  if (pjsua_call_get_info(p_call_id, &info) != PJ_SUCCESS) {
    return;
  }

  bool connected_media = false;
  for (unsigned i = 0; i < info.media_cnt; ++i) {
    if (info.media[i].type != PJMEDIA_TYPE_AUDIO) {
      continue;
    }

    if (info.media[i].status != PJSUA_CALL_MEDIA_ACTIVE &&
        info.media[i].status != PJSUA_CALL_MEDIA_REMOTE_HOLD) {
      continue;
    }

    pjsua_conf_port_id slot = info.media[i].stream.aud.conf_slot;
    if (slot == PJSUA_INVALID_ID) {
      continue;
    }

    pjsua_conf_adjust_tx_level(slot, 1.0f);
    pjsua_conf_adjust_rx_level(slot, 1.0f);
    pjsua_conf_adjust_tx_level(0, 1.0f);
    pjsua_conf_adjust_rx_level(0, 1.0f);

    pj_status_t status = pjsua_conf_connect(slot, 0);
    if (status != PJ_SUCCESS) {
      UtilityFunctions::push_warning(vformat("pjsua_conf_connect(call->sound) failed: %d", static_cast<int64_t>(status)));
      continue;
    }

    status = pjsua_conf_connect(0, slot);
    if (status != PJ_SUCCESS) {
      UtilityFunctions::push_warning(vformat("pjsua_conf_connect(sound->call) failed: %d", static_cast<int64_t>(status)));
      continue;
    }

    connected_media = true;
  }

  // Some builds report invalid per-media conf slot; use call-level slot as fallback.
  if (!connected_media) {
    pjsua_conf_port_id slot = pjsua_call_get_conf_port(p_call_id);
    if (slot == PJSUA_INVALID_ID) {
      UtilityFunctions::push_warning("Call media is active but no valid conference slot was found.");
      return;
    }

    pjsua_conf_adjust_tx_level(slot, 1.0f);
    pjsua_conf_adjust_rx_level(slot, 1.0f);
    pjsua_conf_adjust_tx_level(0, 1.0f);
    pjsua_conf_adjust_rx_level(0, 1.0f);

    pj_status_t status = pjsua_conf_connect(slot, 0);
    if (status != PJ_SUCCESS) {
      UtilityFunctions::push_warning(vformat("pjsua_conf_connect(fallback call->sound) failed: %d", static_cast<int64_t>(status)));
      return;
    }

    status = pjsua_conf_connect(0, slot);
    if (status != PJ_SUCCESS) {
      UtilityFunctions::push_warning(vformat("pjsua_conf_connect(fallback sound->call) failed: %d", static_cast<int64_t>(status)));
      return;
    }
  }

  if (!pjsua_snd_is_active()) {
    UtilityFunctions::push_warning("Call media connected but sound device remains inactive.");
  }
}

void on_reg_state(pjsua_acc_id p_acc_id) {
  pjsua_acc_info info;
  if (pjsua_acc_get_info(p_acc_id, &info) != PJ_SUCCESS) {
    return;
  }

  PJSIPEvent event;
  event.type = PJSIPEvent::EVENT_REGISTRATION_STATE_CHANGED;

  if (info.expires <= 0) {
    event.state = 0; // UNREGISTERED
  } else if (info.status >= 200 && info.status < 300) {
    event.state = 2; // REGISTERED
  } else if (info.status < 200) {
    event.state = 1; // REGISTERING
  } else {
    event.state = 3; // FAILED
  }

  push_event(event);
}

bool apply_audio_devices() {
  pj_status_t status = pjsua_set_snd_dev(g_runtime.capture_dev, g_runtime.playback_dev);
  if (status == PJ_SUCCESS) {
    if (!pjsua_snd_is_active()) {
      UtilityFunctions::push_warning("Audio device selection succeeded but sound device is inactive.");
    }
    return true;
  }

  // Fallback to current system default devices.
  status = pjsua_set_snd_dev(PJMEDIA_AUD_DEFAULT_CAPTURE_DEV, PJMEDIA_AUD_DEFAULT_PLAYBACK_DEV);
  if (status == PJ_SUCCESS) {
    g_runtime.capture_dev = PJMEDIA_AUD_DEFAULT_CAPTURE_DEV;
    g_runtime.playback_dev = PJMEDIA_AUD_DEFAULT_PLAYBACK_DEV;
    if (!pjsua_snd_is_active()) {
      UtilityFunctions::push_warning("Default audio device selection succeeded but sound device is inactive.");
    }
    return true;
  }

  return false;
}

void set_last_error(const String &p_error) {
  g_runtime.last_error = p_error.utf8().get_data();
  UtilityFunctions::push_warning(p_error);
}

} // namespace
#endif

bool PJSIPWrapper::configure_transport(int64_t p_transport_type, int64_t p_port, bool p_verify_tls) {
#ifdef GODOT_SIP_HAS_PJSIP
  if (initialized) {
    set_last_error("Cannot change SIP transport after initialize().");
    return false;
  }

  int transport = PJSIP_TRANSPORT_UDP;
  int port = 0;
  switch (p_transport_type) {
    case TRANSPORT_UDP:
      transport = PJSIP_TRANSPORT_UDP;
      port = 0;
      break;
    case TRANSPORT_TCP:
      transport = PJSIP_TRANSPORT_TCP;
      port = 5060;
      break;
    case TRANSPORT_TLS:
#if !PJ_HAS_SSL_SOCK
      set_last_error("TLS transport requested but pjproject was built without SSL/TLS support (PJ_HAS_SSL_SOCK=0).");
      return false;
#else
      transport = PJSIP_TRANSPORT_TLS;
      port = 5061;
      break;
#endif
    default:
      set_last_error("Unsupported transport type.");
      return false;
  }

  if (p_port > 0 && p_port <= 65535) {
    port = static_cast<int>(p_port);
  }

  g_runtime.transport_type = transport;
  g_runtime.transport_port = port;
  g_runtime.transport_port_explicit = (p_port > 0 && p_port <= 65535);
  g_runtime.verify_tls = p_verify_tls;
  g_runtime.last_error.clear();
  return true;
#else
  (void)p_transport_type;
  (void)p_port;
  (void)p_verify_tls;
  return false;
#endif
}

bool PJSIPWrapper::initialize(const String &p_user_agent) {
#ifdef GODOT_SIP_HAS_PJSIP
  if (initialized) {
    return true;
  }
  g_runtime.last_error.clear();

  pj_status_t status = pjsua_create();
  if (status != PJ_SUCCESS) {
    set_last_error(vformat("pjsua_create failed: %d", static_cast<int64_t>(status)));
    return false;
  }

  pjsua_config config;
  pjsua_config_default(&config);
  config.cb.on_incoming_call = &on_incoming_call;
  config.cb.on_call_state = &on_call_state;
  config.cb.on_call_media_state = &on_call_media_state;
  config.cb.on_reg_state = &on_reg_state;

  g_runtime.user_agent = p_user_agent.utf8().get_data();
  pj_str_t ua = pj_str(const_cast<char *>(g_runtime.user_agent.c_str()));
  config.user_agent = ua;
  if (!g_runtime.stun_server.empty()) {
    config.stun_srv_cnt = 1;
    config.stun_srv[0] = pj_str(const_cast<char *>(g_runtime.stun_server.c_str()));
  }
  config.nat_type_in_sdp = 1;

  pjsua_logging_config logging_config;
  pjsua_logging_config_default(&logging_config);
  logging_config.console_level = 4;

  pjsua_media_config media_config;
  pjsua_media_config_default(&media_config);
  media_config.ec_tail_len = 200;
  media_config.enable_ice = PJ_TRUE;
  media_config.snd_auto_close_time = -1;

  status = pjsua_init(&config, &logging_config, &media_config);
  if (status != PJ_SUCCESS) {
    pjsua_destroy();
    set_last_error(vformat("pjsua_init failed: %d", static_cast<int64_t>(status)));
    return false;
  }

  pjsua_transport_config transport_config;
  pjsua_transport_config_default(&transport_config);
  transport_config.port = g_runtime.transport_port;
  if (g_runtime.transport_type == PJSIP_TRANSPORT_TLS) {
    transport_config.tls_setting.verify_server = g_runtime.verify_tls ? PJ_TRUE : PJ_FALSE;
    transport_config.tls_setting.verify_client = PJ_FALSE;
  }

  pjsua_transport_id transport_id = PJSUA_INVALID_ID;
  status = pjsua_transport_create(static_cast<pjsip_transport_type_e>(g_runtime.transport_type), &transport_config, &transport_id);
  if (status != PJ_SUCCESS) {
    // If no explicit local port was requested, retry with ephemeral port.
    if (!g_runtime.transport_port_explicit && g_runtime.transport_port != 0) {
      transport_config.port = 0;
      status = pjsua_transport_create(static_cast<pjsip_transport_type_e>(g_runtime.transport_type), &transport_config, &transport_id);
      if (status == PJ_SUCCESS) {
        g_runtime.transport_port = 0;
      }
    }
  }
  if (status != PJ_SUCCESS) {
    pjsua_destroy();
    set_last_error(vformat(
        "pjsua_transport_create failed: transport=%s port=%d status=%d",
        transport_name(g_runtime.transport_type),
        static_cast<int64_t>(transport_config.port),
        static_cast<int64_t>(status)));
    return false;
  }
  g_runtime.transport_id = transport_id;

  if (g_runtime.transport_id != PJSUA_INVALID_ID) {
    pjsua_transport_info tinfo;
    if (pjsua_transport_get_info(g_runtime.transport_id, &tinfo) == PJ_SUCCESS) {
      String host = pj_to_string(tinfo.local_name.host);
      UtilityFunctions::print(vformat("[SIP] transport id=%d type=%s published=%s:%d",
                                      static_cast<int64_t>(g_runtime.transport_id),
                                      transport_name(g_runtime.transport_type),
                                      host,
                                      static_cast<int64_t>(tinfo.local_name.port)));
    }
  }

  status = pjsua_start();
  if (status != PJ_SUCCESS) {
    pjsua_destroy();
    set_last_error(vformat("pjsua_start failed: %d", static_cast<int64_t>(status)));
    return false;
  }

  if (!apply_audio_devices()) {
    UtilityFunctions::push_warning("Failed to open/apply audio devices during SIP initialization.");
  }

  g_runtime.started = true;
  g_runtime.last_error.clear();
  initialized = true;
  return true;
#else
  UtilityFunctions::push_warning("PJSIP not available. Set PJSIP_PATH to enable SIP.");
  (void)p_user_agent;
  initialized = false;
  return false;
#endif
}

void PJSIPWrapper::shutdown() {
#ifdef GODOT_SIP_HAS_PJSIP
  if (!initialized) {
    return;
  }

  if (g_runtime.account_id != PJSUA_INVALID_ID) {
    pjsua_acc_set_registration(g_runtime.account_id, PJ_FALSE);
    pjsua_acc_del(g_runtime.account_id);
    g_runtime.account_id = PJSUA_INVALID_ID;
  }

  pjsua_destroy();

  {
    std::lock_guard<std::mutex> lock(g_runtime.mutex);
    g_runtime.events.clear();
  }

  g_runtime.started = false;
  g_runtime.transport_id = PJSUA_INVALID_ID;
#endif
  initialized = false;
}

bool PJSIPWrapper::register_account(const String &p_sip_uri, const String &p_username, const String &p_password, const String &p_registrar) {
#ifdef GODOT_SIP_HAS_PJSIP
  if (!initialized) {
    return false;
  }

  if (g_runtime.account_id != PJSUA_INVALID_ID) {
    pjsua_acc_set_registration(g_runtime.account_id, PJ_FALSE);
    pjsua_acc_del(g_runtime.account_id);
    g_runtime.account_id = PJSUA_INVALID_ID;
  }

  pjsua_acc_config config;
  pjsua_acc_config_default(&config);

  CharString sip_uri_utf8 = p_sip_uri.utf8();
  CharString username_utf8 = p_username.utf8();
  CharString password_utf8 = p_password.utf8();
  CharString registrar_utf8 = p_registrar.utf8();

  pj_str_t id_uri = pj_str(const_cast<char *>(sip_uri_utf8.get_data()));
  pj_str_t reg_uri = pj_str(const_cast<char *>(registrar_utf8.get_data()));

  config.id = id_uri;
  config.reg_uri = reg_uri;
  if (g_runtime.transport_id != PJSUA_INVALID_ID) {
    config.transport_id = g_runtime.transport_id;
  }
  config.allow_contact_rewrite = PJ_TRUE;
  config.allow_via_rewrite = PJ_TRUE;
  config.contact_rewrite_method = PJSUA_CONTACT_REWRITE_METHOD;
  config.sip_stun_use = g_runtime.stun_server.empty() ? PJSUA_STUN_USE_DISABLED : PJSUA_STUN_USE_DEFAULT;
  config.media_stun_use = g_runtime.stun_server.empty() ? PJSUA_STUN_USE_DISABLED : PJSUA_STUN_USE_DEFAULT;
  config.ka_interval = 30;
  config.ice_cfg_use = PJSUA_ICE_CONFIG_USE_DEFAULT;
  // Use a stable RTP port range so firewall rules can be explicit.
  config.rtp_cfg.port = 4000;
  config.rtp_cfg.port_range = 100;
  config.cred_count = 1;
  config.cred_info[0].realm = pj_str(const_cast<char *>("*"));
  config.cred_info[0].scheme = pj_str(const_cast<char *>("digest"));
  config.cred_info[0].username = pj_str(const_cast<char *>(username_utf8.get_data()));
  config.cred_info[0].data_type = PJSIP_CRED_DATA_PLAIN_PASSWD;
  config.cred_info[0].data = pj_str(const_cast<char *>(password_utf8.get_data()));

  pj_status_t status = pjsua_acc_add(&config, PJ_TRUE, &g_runtime.account_id);
  if (status != PJ_SUCCESS) {
    g_runtime.account_id = PJSUA_INVALID_ID;
    UtilityFunctions::push_warning("Failed to register account with pjsua_acc_add.");
    return false;
  }

  PJSIPEvent event;
  event.type = PJSIPEvent::EVENT_REGISTRATION_STATE_CHANGED;
  event.state = 1; // REGISTERING
  push_event(event);

  return true;
#else
  (void)p_sip_uri;
  (void)p_username;
  (void)p_password;
  (void)p_registrar;
  return false;
#endif
}

void PJSIPWrapper::unregister_account() {
#ifdef GODOT_SIP_HAS_PJSIP
  if (g_runtime.account_id == PJSUA_INVALID_ID) {
    return;
  }

  pjsua_acc_set_registration(g_runtime.account_id, PJ_FALSE);
  pjsua_acc_del(g_runtime.account_id);
  g_runtime.account_id = PJSUA_INVALID_ID;

  PJSIPEvent event;
  event.type = PJSIPEvent::EVENT_REGISTRATION_STATE_CHANGED;
  event.state = 0; // UNREGISTERED
  push_event(event);
#endif
}

PJSIPCallInfo PJSIPWrapper::make_call(const String &p_destination_uri) {
#ifdef GODOT_SIP_HAS_PJSIP
  PJSIPCallInfo info;
  if (!initialized || g_runtime.account_id == PJSUA_INVALID_ID) {
    return info;
  }

  CharString destination_utf8 = p_destination_uri.utf8();
  pj_str_t destination = pj_str(const_cast<char *>(destination_utf8.get_data()));

  pjsua_call_id call_id = PJSUA_INVALID_ID;
  pj_status_t status = pjsua_call_make_call(g_runtime.account_id, &destination, 0, nullptr, nullptr, &call_id);
  if (status != PJ_SUCCESS) {
    return info;
  }

  info.id = call_id;
  info.remote_uri = p_destination_uri;
  return info;
#else
  (void)p_destination_uri;
  return {};
#endif
}

void PJSIPWrapper::hangup_call(int64_t p_call_id) {
#ifdef GODOT_SIP_HAS_PJSIP
  pjsua_call_hangup(static_cast<pjsua_call_id>(p_call_id), 0, nullptr, nullptr);
#else
  (void)p_call_id;
#endif
}

void PJSIPWrapper::answer_call(int64_t p_call_id) {
#ifdef GODOT_SIP_HAS_PJSIP
  pjsua_call_answer(static_cast<pjsua_call_id>(p_call_id), 200, nullptr, nullptr);
#else
  (void)p_call_id;
#endif
}

void PJSIPWrapper::reject_call(int64_t p_call_id) {
#ifdef GODOT_SIP_HAS_PJSIP
  pjsua_call_answer(static_cast<pjsua_call_id>(p_call_id), 486, nullptr, nullptr);
#else
  (void)p_call_id;
#endif
}

PackedStringArray PJSIPWrapper::get_audio_input_devices() const {
#ifdef GODOT_SIP_HAS_PJSIP
  PackedStringArray devices;
  if (!initialized) {
    return devices;
  }

  unsigned int count = 64;
  pjmedia_aud_dev_info infos[64];
  if (pjsua_enum_aud_devs(infos, &count) != PJ_SUCCESS) {
    return devices;
  }

  for (unsigned int i = 0; i < count; ++i) {
    if (infos[i].input_count > 0) {
      devices.append(String(infos[i].name));
    }
  }

  return devices;
#else
  return {};
#endif
}

PackedStringArray PJSIPWrapper::get_audio_output_devices() const {
#ifdef GODOT_SIP_HAS_PJSIP
  PackedStringArray devices;
  if (!initialized) {
    return devices;
  }

  unsigned int count = 64;
  pjmedia_aud_dev_info infos[64];
  if (pjsua_enum_aud_devs(infos, &count) != PJ_SUCCESS) {
    return devices;
  }

  for (unsigned int i = 0; i < count; ++i) {
    if (infos[i].output_count > 0) {
      devices.append(String(infos[i].name));
    }
  }

  return devices;
#else
  return {};
#endif
}

bool PJSIPWrapper::select_audio_input_device(const String &p_device_name) {
#ifdef GODOT_SIP_HAS_PJSIP
  if (!initialized) {
    return false;
  }

  unsigned int count = 64;
  pjmedia_aud_dev_info infos[64];
  if (pjsua_enum_aud_devs(infos, &count) != PJ_SUCCESS) {
    return false;
  }

  for (unsigned int i = 0; i < count; ++i) {
    if (infos[i].input_count > 0 && p_device_name == String(infos[i].name)) {
      g_runtime.capture_dev = static_cast<int>(i);
      return apply_audio_devices();
    }
  }
  return false;
#else
  (void)p_device_name;
  return false;
#endif
}

bool PJSIPWrapper::select_audio_output_device(const String &p_device_name) {
#ifdef GODOT_SIP_HAS_PJSIP
  if (!initialized) {
    return false;
  }

  unsigned int count = 64;
  pjmedia_aud_dev_info infos[64];
  if (pjsua_enum_aud_devs(infos, &count) != PJ_SUCCESS) {
    return false;
  }

  for (unsigned int i = 0; i < count; ++i) {
    if (infos[i].output_count > 0 && p_device_name == String(infos[i].name)) {
      g_runtime.playback_dev = static_cast<int>(i);
      return apply_audio_devices();
    }
  }
  return false;
#else
  (void)p_device_name;
  return false;
#endif
}

std::vector<PJSIPEvent> PJSIPWrapper::consume_events() {
#ifdef GODOT_SIP_HAS_PJSIP
  std::vector<pjsua_call_id> calls_to_log;
  {
    auto now = std::chrono::steady_clock::now();
    std::lock_guard<std::mutex> lock(g_runtime.mutex);
    for (auto &entry : g_runtime.active_calls_for_stats) {
      if (entry.second == std::chrono::steady_clock::time_point::min() ||
          now - entry.second >= std::chrono::seconds(1)) {
        entry.second = now;
        calls_to_log.push_back(static_cast<pjsua_call_id>(entry.first));
      }
    }
  }

  for (pjsua_call_id call_id : calls_to_log) {
    pjsua_call_info call_info;
    if (pjsua_call_get_info(call_id, &call_info) != PJ_SUCCESS) {
      continue;
    }

    int audio_media_idx = -1;
    for (unsigned i = 0; i < call_info.media_cnt; ++i) {
      if (call_info.media[i].type == PJMEDIA_TYPE_AUDIO) {
        audio_media_idx = static_cast<int>(i);
        break;
      }
    }

    if (audio_media_idx < 0) {
      UtilityFunctions::print(vformat("[SIP] call %d has no audio media stream.", static_cast<int64_t>(call_id)));
      continue;
    }

    pjsua_stream_stat stat;
    pj_bzero(&stat, sizeof(stat));
    pjsua_stream_info sinfo;
    pj_bzero(&sinfo, sizeof(sinfo));
    pj_status_t info_status = pjsua_call_get_stream_info(call_id, static_cast<unsigned>(audio_media_idx), &sinfo);
    pj_status_t stat_status = pjsua_call_get_stream_stat(call_id, static_cast<unsigned>(audio_media_idx), &stat);
    if (stat_status != PJ_SUCCESS) {
      UtilityFunctions::print(vformat("[SIP] call %d stream stat failed: %d", static_cast<int64_t>(call_id), static_cast<int64_t>(stat_status)));
      continue;
    }

    char remote_rtp[128] = {0};
    if (info_status == PJ_SUCCESS && sinfo.type == PJMEDIA_TYPE_AUDIO) {
      pj_sockaddr_print(&sinfo.info.aud.rem_addr, remote_rtp, sizeof(remote_rtp), 3);
    }

    UtilityFunctions::print(vformat(
        "[SIP] call %d RX packets=%d RX bytes=%d sound_active=%s remote_rtp=%s",
        static_cast<int64_t>(call_id),
        static_cast<int64_t>(stat.rtcp.rx.pkt),
        static_cast<int64_t>(stat.rtcp.rx.bytes),
        pjsua_snd_is_active() ? "yes" : "no",
        String::utf8(remote_rtp)));
  }

  std::vector<PJSIPEvent> out;
  std::lock_guard<std::mutex> lock(g_runtime.mutex);
  out.swap(g_runtime.events);
  return out;
#else
  return {};
#endif
}

bool PJSIPWrapper::is_available() const {
#ifdef GODOT_SIP_HAS_PJSIP
  return true;
#else
  return false;
#endif
}

String PJSIPWrapper::get_last_error() const {
#ifdef GODOT_SIP_HAS_PJSIP
  return String::utf8(g_runtime.last_error.c_str());
#else
  return "PJSIP not available in this build.";
#endif
}

} // namespace godot
