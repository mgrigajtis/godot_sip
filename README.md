# Godot SIP GDExtension (Godot 4.x)

This repository scaffolds a GDExtension that will embed SIP calling via PJSIP.

## Status
- GDExtension skeleton is in place.
- PJSIP (`pjsua`) integration is wired for init, registration, calls, and audio device selection.
- Godot-facing API is active (`SIPClient`, `SIPCall`) with async signals from native callbacks.

## Build prerequisites
- Godot 4.x headers via `godot-cpp`.
- PJSIP built for your target platform(s).
- CMake 3.22+ and a C++17 compiler.

## Quick build (all platforms)
1. Build `godot-cpp` for your target platform and note its path.
2. Build PJSIP and note its root path (contains `pjlib/include`).
3. Configure + build this project:

```sh
cmake -S . -B build \
  -DGODOT_CPP_PATH=/path/to/godot-cpp \
  -DPJSIP_PATH=/path/to/pjsip
cmake --build build --config Debug
```

The build outputs to `build/bin/` with Godot-style names like:

- `godot_sip.windows.debug.x86_64.dll`
- `godot_sip.linux.release.x86_64.so`
- `godot_sip.macos.release.arm64.dylib`

Copy the resulting library to your Godot project `bin/` folder and add `godot_sip.gdextension` to the project root.

## Godot API
- `SIPClient`
  - `initialize(user_agent: String) -> bool`
  - `register_account(sip_uri, username, password, registrar) -> bool`
  - `make_call(destination_uri: String) -> SIPCall`
  - `hangup_call(call: SIPCall)`
  - `answer_call(call: SIPCall)`
  - `reject_call(call: SIPCall)`
  - `get_audio_input_devices() -> PackedStringArray`
  - `get_audio_output_devices() -> PackedStringArray`
  - `select_audio_input_device(name: String) -> bool`
  - `select_audio_output_device(name: String) -> bool`
  - `registration_state_changed(state)` signal
  - `incoming_call(call)` signal
  - `call_state_changed(call, state)` signal

## Demo Scene
- Open `demo/sip_demo.tscn` in Godot.
- Fill account fields, then click `Register`.
- Enter destination URI and click `Call`.
- Use `Answer`, `Reject`, `Hang Up` for active/incoming calls.
- Pick audio devices from the dropdowns or refresh with `Refresh Audio Devices`.

## Next steps
- Add call hold/resume, transfer, and DTMF APIs.
- Add transport options (TCP/TLS, custom SIP port, ICE/STUN/TURN config).
- Add platform packaging scripts to copy the right `.dll/.so/.dylib` into a Godot project automatically.
