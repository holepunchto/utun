cmake_minimum_required(VERSION 3.25)

project(utun C)

include(bare)
include(napi)

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

bare_include_directories(compat NAPI)

target_include_directories(
  ${utun_node}
  PRIVATE
    ${compat}
)

target_sources(
  ${utun_node}
  PRIVATE
    binding.c
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
