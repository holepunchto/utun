cmake_minimum_required(VERSION 3.25)

find_package(cmake-bare REQUIRED PATHS node_modules/cmake-bare)
find_package(cmake-napi REQUIRED PATHS node_modules/cmake-napi)
find_package(cmake-npm REQUIRED PATHS node_modules/cmake-npm)

project(utun C)

bare_target(target)

add_bare_module(utun_bare)

target_sources(
  ${utun_bare}
  PRIVATE
    binding.c
)

if(target MATCHES "darwin|ios")
  target_compile_definitions(
    ${utun_bare}
    PRIVATE
      UTUN_APPLE=1
  )
elseif(target MATCHES "linux|android")
  target_compile_definitions(
    ${utun_bare}
    PRIVATE
      UTUN_LINUX=1
  )
endif()

add_napi_module(utun_node)

target_sources(
  ${utun_node}
  PRIVATE
    binding.c
)

target_compile_definitions(
  ${utun_node}
  PRIVATE
    NAPI_VERSION=9
)

if(target MATCHES "darwin|ios")
  target_compile_definitions(
    ${utun_node}
    PRIVATE
      UTUN_APPLE=1
  )
elseif(target MATCHES "linux|android")
  target_compile_definitions(
    ${utun_node}
    PRIVATE
      UTUN_LINUX=1
  )
endif()

resolve_node_module(bare-compat-napi compat)

target_include_directories(
  ${utun_node}
  PRIVATE
    "${compat}/include"
)
