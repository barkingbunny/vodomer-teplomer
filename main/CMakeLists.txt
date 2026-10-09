idf_component_register(
    SRCS
        main.cpp
        clock.cpp
        rtc_state.cpp
        ds18b20.cpp
        power.cpp
        display.cpp
        diag.cpp
        timebase.cpp
        persist.cpp
        store.cpp
        net.cpp
        upload.cpp
        portal.cpp
    INCLUDE_DIRS "."
    PRIV_REQUIRES
        esp_driver_gpio
        esp_adc
        esp_hw_support
        esp_rom
        esp_system
        esp_timer
        nvs_flash
        esp_wifi
        esp_netif
        esp_event
        esp_http_client
        esp_http_server
        lwip
)

# Cas buildu -> odhad realneho casu do NTP (timebase.h). Hlavicka se generuje
# pri kazdem buildu do build adresare; prelozi se jen timebase.cpp.
set(VT_GEN_DIR ${CMAKE_BINARY_DIR}/vt_gen)
add_custom_target(vt_buildtime
    COMMAND ${CMAKE_COMMAND} -DOUT=${VT_GEN_DIR}/vt_buildtime.h
            -P ${CMAKE_CURRENT_LIST_DIR}/../tools/buildtime.cmake
    BYPRODUCTS ${VT_GEN_DIR}/vt_buildtime.h
    VERBATIM)
add_dependencies(${COMPONENT_LIB} vt_buildtime)
target_include_directories(${COMPONENT_LIB} PRIVATE ${VT_GEN_DIR})

# Kanal a Write key ThingSpeaku (mimo repozitar) -> TS_CHANNEL_ID, TS_WRITE_KEY.
include(${CMAKE_CURRENT_LIST_DIR}/../tools/secrets.cmake)
vt_add_thingspeak_secrets(${COMPONENT_LIB} ${VT_GEN_DIR})
