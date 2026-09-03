#
# Unit tests
#

add_executable(test_str
    ${SOURCE_DIR}/corepp/tests/test_str.cpp
    ${SOURCE_DIR}/corepp/str.cpp
    ${SOURCE_DIR}/qcommon/q_shared.c
    ${SOURCE_DIR}/qcommon/common_light.c
)

target_link_libraries(test_str INTERFACE testing)
add_test(NAME test_str COMMAND test_str)
set_tests_properties(test_str PROPERTIES TIMEOUT 15)
