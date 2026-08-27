add_executable(test_particle
    ${SOURCE_DIR}/renderergl1/tests/test_particle.cpp
    ${SOURCE_DIR}/renderergl1/tr_ghost.cpp
    ${SOURCE_DIR}/qcommon/q_math.c
    ${SOURCE_DIR}/qcommon/q_shared.c
    ${SOURCE_DIR}/corepp/str.cpp
)

target_include_directories(test_particle PRIVATE ${SDL2_INCLUDE_DIRS})
target_link_libraries(test_particle INTERFACE testing)
add_test(NAME test_particle COMMAND test_particle)
set_tests_properties(test_particle PROPERTIES TIMEOUT 15)
