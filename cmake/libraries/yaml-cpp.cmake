if(USE_INTERNAL_LIBS)
    set(CMAKE_POSITION_INDEPENDENT_CODE ON) # Required for linking static lib into shared lib
    add_subdirectory(${SOURCE_DIR}/thirdparty/yaml-cpp)
    set(YAML_CPP_LIBRARIES yaml-cpp)
    set(YAML_CPP_INCLUDE_DIRS ${SOURCE_DIR}/thirdparty/yaml-cpp/include)
else()
    find_package(yaml-cpp REQUIRED)
    set(YAML_CPP_LIBRARIES yaml-cpp)
endif()