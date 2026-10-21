# Out-of-tree ZMK module living inside the config repository.
#
# The ZMK build workflow passes this repository as ZMK_EXTRA_MODULES when it
# finds zephyr/module.yml, so this CMakeLists.txt is processed for every build
# (dongle, left half and right half). Only the central/dongle build enables the
# processor through Kconfig.

if(CONFIG_ZMK_INPUT_PROCESSOR_MOUSE_LAYER_GATE)
  target_sources(app PRIVATE src/input_processor_mouse_layer_gate.c)
endif()

if(CONFIG_ZMK_KEY_STATUS_OVERLAY)
  target_sources(app PRIVATE
    src/key_status_overlay.c
    src/keycode_name.c
  )
endif()
