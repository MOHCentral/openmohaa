if(SDL2_DIR)
    # When SDL2_DIR is provided, use the config mode
    find_package(SDL2 CONFIG QUIET)
    if(TARGET SDL2::SDL2)
        get_target_property(SDL2_INCLUDE_DIRS SDL2::SDL2 INTERFACE_INCLUDE_DIRECTORIES)
        if(NOT SDL2_INCLUDE_DIRS OR SDL2_INCLUDE_DIRS MATCHES "-NOTFOUND")
            set(SDL2_INCLUDE_DIRS ${SDL2_DIR}/../../include/SDL2 ${SDL2_DIR}/../../include)
        endif()
        set(SDL2_LIBRARIES SDL2::SDL2)
    elseif(TARGET SDL2::SDL2-static)
        get_target_property(SDL2_INCLUDE_DIRS SDL2::SDL2-static INTERFACE_INCLUDE_DIRECTORIES)
        if(NOT SDL2_INCLUDE_DIRS OR SDL2_INCLUDE_DIRS MATCHES "-NOTFOUND")
            set(SDL2_INCLUDE_DIRS ${SDL2_DIR}/../../include/SDL2 ${SDL2_DIR}/../../include)
        endif()
        set(SDL2_LIBRARIES SDL2::SDL2-static)
    endif()

    if(SDL2_LIBRARIES)
        set(SDL2_FOUND TRUE)
        return()
    endif()
endif()

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
