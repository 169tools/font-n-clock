cmake_minimum_required(VERSION 3.28...3.30)

include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/common/bootstrap.cmake" NO_POLICY_SCOPE)

project(${_name} VERSION ${_version})

include(compilerconfig)
include(defaults)
include(helpers)
set(CMAKE_CXX_STANDARD 20)

add_library(${CMAKE_PROJECT_NAME} MODULE)

find_package(libobs REQUIRED)
target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE OBS::libobs)

find_package(obs-frontend-api REQUIRED)
target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE OBS::obs-frontend-api)

find_package(Qt6 COMPONENTS Widgets Core)
if(APPLE AND TARGET WrapOpenGL::WrapOpenGL)
  get_target_property(_wrap_opengl_libraries WrapOpenGL::WrapOpenGL INTERFACE_LINK_LIBRARIES)
  list(FILTER _wrap_opengl_libraries EXCLUDE REGEX "AGL\\.framework|-framework[ ]+AGL")
  set_target_properties(WrapOpenGL::WrapOpenGL PROPERTIES INTERFACE_LINK_LIBRARIES "${_wrap_opengl_libraries}")
endif()
target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE Qt6::Core Qt6::Widgets)
target_compile_options(
  ${CMAKE_PROJECT_NAME}
  PRIVATE $<$<C_COMPILER_ID:Clang,AppleClang>:-Wno-quoted-include-in-framework-header -Wno-comma>
)
set_target_properties(
  ${CMAKE_PROJECT_NAME}
  PROPERTIES AUTOMOC ON AUTOUIC ON AUTORCC ON
)

target_sources(
  ${CMAKE_PROJECT_NAME}
  PRIVATE
    src/plugin-main.cpp
    src/clock-source.cpp
    src/clock-source.hpp
    src/font-dialog.cpp
    src/font-dialog.hpp
    src/layout.hpp
    src/text-renderer.hpp
)

if(APPLE)
  target_sources(${CMAKE_PROJECT_NAME} PRIVATE src/cf-ptr.hpp src/text-renderer-mac.cpp)
  target_link_libraries(
    ${CMAKE_PROJECT_NAME}
    PRIVATE "-framework CoreFoundation" "-framework CoreGraphics" "-framework CoreText"
  )
else()
  target_sources(${CMAKE_PROJECT_NAME} PRIVATE src/text-renderer-stub.cpp)
endif()

set_target_properties_plugin(${CMAKE_PROJECT_NAME} PROPERTIES OUTPUT_NAME ${_name})
