cmake_minimum_required(VERSION 3.16)
project(retcomm_rbengine VERSION 0.1.0 LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

option(RBE_BUILD_TESTS "Build unit tests" ON)

# No dependencies. The admission scheduler, hash-confirm, input history and
# the RB_POST tip filter moved to recomp-net, because every decision they make
# is about peers; what is left here works with netplay compiled out, so an
# engine's offline rewind can link this library instead of cherry-picking
# rbe_snap_ring.c as a bare source file.

set(RBE_SOURCES
  src/snap/rbe_snap_ring.c
  src/util/rbe_mono_ms.c
)

add_library(retcomm_rbengine STATIC ${RBE_SOURCES})
add_library(retcomm_rbengine::retcomm_rbengine ALIAS retcomm_rbengine)

target_include_directories(retcomm_rbengine
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

target_compile_definitions(retcomm_rbengine PRIVATE
  $<$<PLATFORM_ID:Windows>:_CRT_SECURE_NO_WARNINGS>
)
if(UNIX)
  target_compile_definitions(retcomm_rbengine PRIVATE
    _POSIX_C_SOURCE=200809L
    _DEFAULT_SOURCE
  )
endif()

if(MSVC)
  target_compile_options(retcomm_rbengine PRIVATE /W4)
else()
  target_compile_options(retcomm_rbengine PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(RBE_BUILD_TESTS)
  enable_testing()

  add_executable(snap_ring_test tests/snap_ring_test.c)
  target_link_libraries(snap_ring_test PRIVATE retcomm_rbengine)
  add_test(NAME snap_ring_test COMMAND snap_ring_test)

endif()

install(TARGETS retcomm_rbengine ARCHIVE DESTINATION lib LIBRARY DESTINATION lib)
install(DIRECTORY include/retcomm_rbengine DESTINATION include)
