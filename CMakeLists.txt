cmake_minimum_required(VERSION 3.25)

project(utun_native C)

include(bare)

add_bare_module(utun_native)

target_sources(
  ${utun_native}
  PRIVATE
    binding.c
)
