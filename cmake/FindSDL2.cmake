# Advanced FindSDL2.cmake for fallback

# Check if SDL2 is found using the upstream config
find_package(SDL2 CONFIG QUIET)
if(SDL2_FOUND)
  if(TARGET SDL2::SDL2)
    set(SDL2_LIBRARIES SDL2::SDL2)
    get_target_property(SDL2_INCLUDE_DIRS SDL2::SDL2 INTERFACE_INCLUDE_DIRECTORIES)
  elseif(TARGET SDL2::SDL2-static)
    set(SDL2_LIBRARIES SDL2::SDL2-static)
    get_target_property(SDL2_INCLUDE_DIRS SDL2::SDL2-static INTERFACE_INCLUDE_DIRECTORIES)
  endif()
  return()
endif()

find_path(SDL2_INCLUDE_DIR SDL.h PATH_SUFFIXES SDL2 include/SDL2 include PATHS ${SDL2_DIR} ${SDL2_DIR}/include ${SDL2_DIR}/../../../include ${SDL2_DIR}/../../include)
find_library(SDL2_LIBRARY NAMES SDL2 SDL2-2.0 SDL2.lib PATHS ${SDL2_DIR} ${SDL2_DIR}/lib ${SDL2_DIR}/../../../lib ${SDL2_DIR}/../../lib)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(SDL2 DEFAULT_MSG SDL2_LIBRARY SDL2_INCLUDE_DIR)

if(SDL2_FOUND)
  set(SDL2_LIBRARIES ${SDL2_LIBRARY})
  set(SDL2_INCLUDE_DIRS ${SDL2_INCLUDE_DIR})
endif()
