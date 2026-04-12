if(NOT USE_MQTT)
    return()
endif()

# MQTT uses a built-in lightweight client (no external dependency)
# Just define the compile flag for the game module
set(MQTT_DEFINITIONS USE_MQTT)

# Add to game module definitions (basegame/missionpack)
list(APPEND GAME_DEFINITIONS ${MQTT_DEFINITIONS})
