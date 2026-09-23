find_package(PkgConfig QUIET)
pkg_check_modules(PC_SDL2 QUIET sdl2)
find_path(SDL2_INCLUDE_DIRS
  NAMES SDL.h
  HINTS ${PC_SDL2_INCLUDEDIR} ${PC_SDL2_INCLUDE_DIRS}
  PATH_SUFFIXES SDL2
)
find_library(SDL2_LIBRARIES
  NAMES SDL2
  HINTS ${PC_SDL2_LIBDIR} ${PC_SDL2_LIBRARY_DIRS}
)
include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(SDL2 REQUIRED_VARS SDL2_LIBRARIES SDL2_INCLUDE_DIRS)
