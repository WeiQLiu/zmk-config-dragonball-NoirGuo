# Add test source file to build
zephyr_library_sources_ifdef(CONFIG_APP_DIR config/app/src/dongle_layer_test.c)
