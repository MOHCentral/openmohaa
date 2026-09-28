#
# Unit tests
#

add_executable(test_lz77
    ${CMAKE_SOURCE_DIR}/code/corepp/tests/test_lz77.cpp
    ${CMAKE_SOURCE_DIR}/code/corepp/lz77.cpp
    ${CMAKE_SOURCE_DIR}/code/qcommon/q_shared.c
    ${CMAKE_SOURCE_DIR}/code/qcommon/common_light.c
)

#target_link_libraries(test_lz77 INTERFACE testing)
add_test(NAME test_lz77 COMMAND test_lz77)
set_tests_properties(test_lz77 PROPERTIES TIMEOUT 15)
