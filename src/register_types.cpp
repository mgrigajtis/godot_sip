#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/core/defs.hpp>
#include <godot_cpp/godot.hpp>

#include "sip_client.h"
#include "sip_call.h"

using namespace godot;

void initialize_godot_sip_module(ModuleInitializationLevel p_level) {
  if (p_level != MODULE_INITIALIZATION_LEVEL_SCENE) {
    return;
  }

  ClassDB::register_class<SIPClient>();
  ClassDB::register_class<SIPCall>();
}

void uninitialize_godot_sip_module(ModuleInitializationLevel p_level) {
  if (p_level != MODULE_INITIALIZATION_LEVEL_SCENE) {
    return;
  }
}

extern "C" {

GDExtensionBool GDE_EXPORT godot_sip_library_init(GDExtensionInterfaceGetProcAddress p_get_proc_address,
                                                 GDExtensionClassLibraryPtr p_library,
                                                 GDExtensionInitialization *r_initialization) {
  GDExtensionBinding::InitObject init_obj(p_get_proc_address, p_library, r_initialization);
  init_obj.register_initializer(initialize_godot_sip_module);
  init_obj.register_terminator(uninitialize_godot_sip_module);
  init_obj.set_minimum_library_initialization_level(MODULE_INITIALIZATION_LEVEL_SCENE);
  return init_obj.init();
}

}
