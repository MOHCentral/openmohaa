# Simple FindSDL2.cmake for fallback
find_path(SDL2_INCLUDE_DIR SDL.h PATH_SUFFIXES SDL2 include/SDL2 include)
find_library(SDL2_LIBRARY NAMES SDL2 SDL2-2.0)
message(STATUS "SDL2_INCLUDE_DIR: ${SDL2_INCLUDE_DIR}")
message(STATUS "SDL2_LIBRARY: ${SDL2_LIBRARY}")
