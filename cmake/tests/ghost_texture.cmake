add_executable(test_ghost_texture
    ${SOURCE_DIR}/renderergl1/tests/test_ghost_texture.cpp
    ${SOURCE_DIR}/qcommon/q_math.c
    ${SOURCE_DIR}/corepp/str.cpp
    ${SOURCE_DIR}/corepp/mem_tempalloc.cpp
    ${SOURCE_DIR}/corepp/mem_blockalloc.cpp
)
target_include_directories(test_ghost_texture PRIVATE
    ${SOURCE_DIR}/qcommon
    ${SOURCE_DIR}/renderercommon
    ${SOURCE_DIR}/corepp
    ${SOURCE_DIR}/fgame
    ${SOURCE_DIR}/renderergl1
    ${SOURCE_DIR}/thirdparty/SDL2-2.32.8/include
)
add_test(NAME test_ghost_texture COMMAND test_ghost_texture)

add_executable(test_ghost_texture_gl2
    ${SOURCE_DIR}/renderergl2/tests/test_ghost_texture.cpp
    ${SOURCE_DIR}/qcommon/q_math.c
    ${SOURCE_DIR}/corepp/str.cpp
    ${SOURCE_DIR}/corepp/mem_tempalloc.cpp
    ${SOURCE_DIR}/corepp/mem_blockalloc.cpp
)
target_include_directories(test_ghost_texture_gl2 PRIVATE
    ${SOURCE_DIR}/qcommon
    ${SOURCE_DIR}/renderercommon
    ${SOURCE_DIR}/corepp
    ${SOURCE_DIR}/fgame
    ${SOURCE_DIR}/renderergl2
    ${SOURCE_DIR}/thirdparty/SDL2-2.32.8/include
)
add_test(NAME test_ghost_texture_gl2 COMMAND test_ghost_texture_gl2)
