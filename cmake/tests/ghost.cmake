#
# Unit tests
#

add_executable(test_ghost
    ${SOURCE_DIR}/renderergl1/tests/test_ghost.cpp
    ${SOURCE_DIR}/renderergl1/tr_ghost.cpp
    ${SOURCE_DIR}/corepp/str.cpp
)

target_include_directories(test_ghost PRIVATE
    ${SOURCE_DIR}/qcommon
    ${SOURCE_DIR}/renderercommon
    ${SOURCE_DIR}/corepp
    ${SOURCE_DIR}/fgame
    ${SOURCE_DIR}/renderergl1
    ${SOURCE_DIR}/thirdparty/SDL2-2.32.8/include
)


add_test(NAME test_ghost COMMAND test_ghost)
set_tests_properties(test_ghost PROPERTIES TIMEOUT 15)
