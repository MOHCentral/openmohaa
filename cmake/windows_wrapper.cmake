# Optional Windows GitHub update wrapper.
# When enabled, the engine is named openmohaa_game.exe and openmohaa.exe
# stays a thin launcher that checks official releases, then starts the engine.

if(NOT WIN32 OR NOT BUILD_GITHUB_UPDATE_WRAPPER)
    return()
endif()

if(NOT BUILD_CLIENT)
    message(WARNING "BUILD_GITHUB_UPDATE_WRAPPER requires BUILD_CLIENT")
    return()
endif()

if(NOT TARGET ${CLIENT_NAME})
    message(FATAL_ERROR "BUILD_GITHUB_UPDATE_WRAPPER: client target ${CLIENT_NAME} is missing")
endif()

# Keep the wrapper as openmohaa.exe. Official zip payloads also contain
# openmohaa.exe; launch_openmohaa.ps1 must copy that onto openmohaa_game.exe.
set_target_properties(${CLIENT_NAME} PROPERTIES
    OUTPUT_NAME "openmohaa_game"
    DEBUG_POSTFIX ""
)

add_executable(openmohaa_github_wrapper ${CMAKE_SOURCE_DIR}/misc/windows/openmohaa_wrapper.c)
set_target_properties(openmohaa_github_wrapper PROPERTIES
    OUTPUT_NAME "openmohaa"
    DEBUG_POSTFIX ""
)
target_compile_definitions(openmohaa_github_wrapper PRIVATE UNICODE _UNICODE)
target_link_libraries(openmohaa_github_wrapper PRIVATE user32)

if(MSVC)
    target_link_options(openmohaa_github_wrapper PRIVATE "/SUBSYSTEM:CONSOLE")
elseif(MINGW)
    target_link_options(openmohaa_github_wrapper PRIVATE -municode)
endif()

include(utils/set_output_dirs)
set_output_dirs(openmohaa_github_wrapper)

install(TARGETS openmohaa_github_wrapper DESTINATION ${INSTALL_BINDIR_FULL})
install(FILES ${CMAKE_SOURCE_DIR}/misc/windows/launch_openmohaa.ps1
    DESTINATION ${INSTALL_BINDIR_FULL})

if(MSVC)
    install(FILES $<TARGET_PDB_FILE:openmohaa_github_wrapper>
        DESTINATION ${INSTALL_BINDIR_FULL} OPTIONAL)
endif()
