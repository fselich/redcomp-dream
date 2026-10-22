cmake_minimum_required(VERSION 3.16)
project(recomp_net VERSION 0.1.0 LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

option(RNET_ENABLE_ICE "Build libjuice ICE transport" OFF)
option(RNET_ICE_BUNDLE_STATIC
  "Fetch/build static libjuice into recomp_net (portable; no system .so)" ON)
option(RNET_ICE_FORCE_TURN
  "Testing: only emit/accept ICE typ relay candidates (requires TURN)" OFF)
set(RNET_LIBJUICE_ROOT "" CACHE PATH
  "Optional path to libjuice sources (skip FetchContent when set)")
option(RNET_BUILD_EXAMPLES "Build example programs" ON)
option(RNET_BUILD_TESTS "Build unit tests" ON)
option(RNET_BUILD_MODULE
  "Also build recomp_net_module: one shared library (recomp-net + rbengine) an engine loads at run time (recomp_net/module.h)" OFF)
set(RNET_RBENGINE_DIR "" CACHE PATH "retcomm-rbengine checkout folded into the module (RNET_BUILD_MODULE)")
set(RNET_BUILD_VERSION "" CACHE STRING "Release version (x.y.z) the module reports; empty: the library version (RNET_BUILD_MODULE)")
set(RNET_BUILD_VERSION "" CACHE STRING "Release version (x.y.z) the module reports; empty: the library version (RNET_BUILD_MODULE)")
set(RNET_BUILD_ID "unknown" CACHE STRING "Source id reported by rnet_module_info (RNET_BUILD_MODULE)")

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

set(RNET_CORE_SOURCES
  src/platform/rnet_address.c
  src/platform/rnet_platform.c
  src/platform/rnet_stun.c
  src/protocol/rnet_protocol.c
  src/input/rnet_rings.c
  src/input/rnet_input_contract.c
  src/input/rnet_input_hist.c
  src/rollback/rnet_rollback.c
  src/rollback/rnet_hash_confirm.c
  src/rollback/rnet_rb_quorum.c
  src/rollback/rnet_rb_driver.c
  src/sched/rnet_sched.c
  src/lobby/rnet_lan_lobby.c
  src/lobby/rnet_lan_direct.c
  src/lobby/rnet_lan_beacon.c
  src/lobby/rnet_rtt_probe.c
  # Host relay: the host's reachable port (UPnP / NAT-PMP / STUN), the
  # guest's probe, and the waiting-room orchestration every lobby client
  # drives (recomp_net/host_relay.h). One copy, for recomp-ui and the engines.
  src/nat/rnet_host_relay.c
  # Host relay over ICE: the waiting-room agents (recomp_net/host_ice.h).
  src/nat/rnet_host_ice.c
  src/nat/rnet_sig_hold.c
  src/lobby/rnet_ice_rtt.c
  src/lobby/rnet_ice_xfer.c
  src/transport/rnet_netsim.c
  src/transport/rnet_transport.c
  src/session/rnet_session.c
  src/chat/rnet_chat_filter.c
  src/chat/rnet_chat_report.c
  src/auth/rnet_auth.c
  src/auth/rnet_open_url.c
  src/auth/rnet_sha256.c
  src/ice/rnet_ice_state.c
  src/ice/rnet_ice_agent.c
  # RFC 6455 client framing + the SHA-1 the handshake needs. Lifted from
  # the engines, where psxrecomp and snesrecomp each vendored a copy:
  # rnet_sha1.{c,h} and rnet_ws.h were byte-identical between them and
  # rnet_ws.c differed only by two static helpers in swapped order.
  src/lobby_ws/rnet_ws.c
  src/lobby_ws/rnet_sha1.c
  # The WebSocket lobby protocol client (recomp_net/lobby_client.h), lifted
  # from snesrecomp's snes_lobby_client.c so every engine links one copy.
  src/lobby/rnet_lobby_client.c
)

add_library(recomp_net STATIC ${RNET_CORE_SOURCES})
add_library(recomp_net::recomp_net ALIAS recomp_net)

target_include_directories(recomp_net
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_compile_definitions(recomp_net PRIVATE $<$<PLATFORM_ID:Windows>:_CRT_SECURE_NO_WARNINGS>)
if(UNIX)
  target_compile_definitions(recomp_net PRIVATE _POSIX_C_SOURCE=200809L _DEFAULT_SOURCE)
  # Darwin gates BSD extensions in <net/if.h> behind
  # `!_POSIX_C_SOURCE || _DARWIN_C_SOURCE`. Strict POSIX above hides IFF_UP
  # (rnet_address.c). _DEFAULT_SOURCE is glibc-only and does nothing here.
  if(APPLE)
    target_compile_definitions(recomp_net PRIVATE _DARWIN_C_SOURCE)
  endif()
endif()

if(WIN32)
  target_link_libraries(recomp_net PUBLIC ws2_32 iphlpapi bcrypt shell32)
endif()

if(RNET_ENABLE_ICE)
  # Portable game embeds: prefer a static juice linked into recomp_net so Linux
  # binaries do not depend on a distro libjuice.so at runtime. Opt out with
  # -DRNET_ICE_BUNDLE_STATIC=OFF to use find_package(Libjuice) (shared OK).
  set(_rnet_juice_ok FALSE)
  if(NOT RNET_ICE_BUNDLE_STATIC)
    find_package(Libjuice QUIET)
    if(Libjuice_FOUND)
      set(_rnet_juice_ok TRUE)
    endif()
  endif()
  # Offline / vendored tree (setup CI can pre-seed this to skip network).
  if(NOT _rnet_juice_ok AND EXISTS
      "${CMAKE_CURRENT_SOURCE_DIR}/third_party/libjuice/CMakeLists.txt")
    message(STATUS "Libjuice: using vendored third_party/libjuice")
    set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
    set(NO_TESTS ON CACHE BOOL "" FORCE)
    set(NO_SERVER ON CACHE BOOL "" FORCE)
    set(USE_NETTLE OFF CACHE BOOL "" FORCE)
    set(WARNINGS_AS_ERRORS OFF CACHE BOOL "" FORCE)
    add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/third_party/libjuice"
                     "${CMAKE_BINARY_DIR}/third_party-libjuice")
    set(_rnet_juice_ok TRUE)
  endif()
  if(NOT _rnet_juice_ok AND RNET_LIBJUICE_ROOT AND EXISTS
      "${RNET_LIBJUICE_ROOT}/CMakeLists.txt")
    message(STATUS "Libjuice: using RNET_LIBJUICE_ROOT=${RNET_LIBJUICE_ROOT}")
    set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
    set(NO_TESTS ON CACHE BOOL "" FORCE)
    set(NO_SERVER ON CACHE BOOL "" FORCE)
    set(USE_NETTLE OFF CACHE BOOL "" FORCE)
    set(WARNINGS_AS_ERRORS OFF CACHE BOOL "" FORCE)
    add_subdirectory("${RNET_LIBJUICE_ROOT}"
                     "${CMAKE_BINARY_DIR}/rnet-libjuice-root")
    set(_rnet_juice_ok TRUE)
  endif()
  if(NOT _rnet_juice_ok)
    include(FetchContent)
    # URL archive (not GIT_REPOSITORY): cmake's file(DOWNLOAD) avoids system
    # git-remote-https, which breaks under Retro AppImage LD_LIBRARY_PATH
    # (curl_global_trace / CURL_OPENSSL_4 symbol mismatch).
    #
    # CMP0135 NEW + DOWNLOAD_EXTRACT_TIMESTAMP: stamp extracted files at
    # extract time, not archive mtimes. Archive timestamps (often far in the
    # past, occasionally ahead of a skewed host clock) make Ninja treat
    # build.ninja as forever dirty ("still dirty after 100 tries") on Windows
    # Hub builds that FetchContent libjuice into the game build tree.
    message(STATUS
      "Libjuice: bundling static juice via FetchContent URL "
      "(RNET_ICE_BUNDLE_STATIC=${RNET_ICE_BUNDLE_STATIC})")
    set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
    set(NO_TESTS ON CACHE BOOL "" FORCE)
    set(NO_SERVER ON CACHE BOOL "" FORCE)
    set(USE_NETTLE OFF CACHE BOOL "" FORCE)
    set(WARNINGS_AS_ERRORS OFF CACHE BOOL "" FORCE)
    if(POLICY CMP0135)
      cmake_policy(SET CMP0135 NEW)
    endif()
    if(CMAKE_VERSION VERSION_GREATER_EQUAL "3.24")
      FetchContent_Declare(
        libjuice
        URL https://github.com/paullouisageneau/libjuice/archive/refs/tags/v1.7.2.tar.gz
        URL_HASH SHA256=75159867c4a5a689a6559e11aa0d30c9eba12ce73a4ae3d898b521467e1f635d
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE
      )
    else()
      FetchContent_Declare(
        libjuice
        URL https://github.com/paullouisageneau/libjuice/archive/refs/tags/v1.7.2.tar.gz
        URL_HASH SHA256=75159867c4a5a689a6559e11aa0d30c9eba12ce73a4ae3d898b521467e1f635d
      )
    endif()
    FetchContent_MakeAvailable(libjuice)
    set(_rnet_juice_ok TRUE)
  endif()
  if(_rnet_juice_ok)
    # Parent CMAKE_C_EXTENSIONS=OFF compiles juice as -std=c11.
    # Unix: _GNU_SOURCE exposes getaddrinfo. Windows/MinGW: that same
    # macro plus __STRICT_ANSI__ skips IN_ADDR in inaddr.h, then
    # mstcpip.h (via ws2tcpip.h) fails; libjuice also needs gnu11 for
    # <sys/time.h>. C_EXTENSIONS ON alone is not enough under Retro
    # cmake-clang-v1 — toolchain / CMAKE_C_FLAGS often keep -std=c11, so
    # force -std=gnu11 on the juice targets for Clang/GNU on Windows.
    foreach(_rnet_juice IN ITEMS juice-static juice)
      if(TARGET ${_rnet_juice})
        if(UNIX)
          target_compile_definitions(${_rnet_juice} PRIVATE _GNU_SOURCE)
        endif()
        set_target_properties(${_rnet_juice} PROPERTIES
          C_STANDARD 11
          C_STANDARD_REQUIRED ON
          C_EXTENSIONS ON)
        if(WIN32 AND CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
          target_compile_options(${_rnet_juice} PRIVATE -std=gnu11)
        endif()
      endif()
    endforeach()
    if(NOT TARGET Libjuice::Libjuice)
      if(TARGET juice-static)
        add_library(Libjuice::Libjuice ALIAS juice-static)
      elseif(TARGET juice)
        add_library(Libjuice::Libjuice ALIAS juice)
      else()
        message(FATAL_ERROR
          "RNET_ENABLE_ICE=ON but libjuice was not found and FetchContent did not "
          "produce a juice target. Install libjuice, set RNET_LIBJUICE_ROOT, "
          "vendor sources under third_party/libjuice, or enable network for "
          "FetchContent.")
      endif()
    endif()
  endif()
  target_compile_definitions(recomp_net PUBLIC RNET_ENABLE_ICE=1)
  if(RNET_ICE_FORCE_TURN)
    target_compile_definitions(recomp_net PUBLIC RNET_ICE_FORCE_TURN=1)
    message(STATUS "recomp_net: RNET_ICE_FORCE_TURN — relay-only ICE candidates")
  endif()
  target_link_libraries(recomp_net PUBLIC Libjuice::Libjuice)
  if(UNIX AND NOT APPLE)
    find_package(Threads REQUIRED)
    target_link_libraries(recomp_net PUBLIC Threads::Threads)
  endif()
  # When ICE is off, session uses inline stubs from rnet_ice_internal.h.
endif()

if(MSVC)
  target_compile_options(recomp_net PRIVATE /W4)
else()
  target_compile_options(recomp_net PRIVATE -Wall -Wextra -Wpedantic)
endif()

# ---- Loadable module ----
if(RNET_BUILD_MODULE)
  if(NOT RNET_RBENGINE_DIR)
    message(FATAL_ERROR "RNET_BUILD_MODULE=ON needs RNET_RBENGINE_DIR (the module exports rbe_*)")
  endif()
  add_library(recomp_net_module SHARED ${RNET_CORE_SOURCES} module/rnet_module.c
    ${RNET_RBENGINE_DIR}/src/snap/rbe_snap_ring.c
    ${RNET_RBENGINE_DIR}/src/util/rbe_mono_ms.c)
  target_include_directories(recomp_net_module
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src ${RNET_RBENGINE_DIR}/include)
  target_compile_definitions(recomp_net_module PRIVATE
    RNET_MODULE_HAS_RBENGINE=1 "RNET_BUILD_ID=\"${RNET_BUILD_ID}\""
    $<$<PLATFORM_ID:Windows>:_CRT_SECURE_NO_WARNINGS>)
  if(RNET_BUILD_VERSION MATCHES "^([0-9]+)\\.([0-9]+)\\.([0-9]+)$")
    target_compile_definitions(recomp_net_module PRIVATE
      RNET_MODULE_VERSION_MAJOR=${CMAKE_MATCH_1} RNET_MODULE_VERSION_MINOR=${CMAKE_MATCH_2}
      RNET_MODULE_VERSION_PATCH=${CMAKE_MATCH_3})
  elseif(RNET_BUILD_VERSION)
    message(FATAL_ERROR "RNET_BUILD_VERSION must be x.y.z, got '${RNET_BUILD_VERSION}'")
  endif()
  if(RNET_BUILD_VERSION MATCHES "^([0-9]+)\\.([0-9]+)\\.([0-9]+)$")
    target_compile_definitions(recomp_net_module PRIVATE
      RNET_MODULE_VERSION_MAJOR=${CMAKE_MATCH_1} RNET_MODULE_VERSION_MINOR=${CMAKE_MATCH_2}
      RNET_MODULE_VERSION_PATCH=${CMAKE_MATCH_3})
  elseif(RNET_BUILD_VERSION)
    message(FATAL_ERROR "RNET_BUILD_VERSION must be x.y.z, got '${RNET_BUILD_VERSION}'")
  endif()
  if(UNIX)
    target_compile_definitions(recomp_net_module PRIVATE _POSIX_C_SOURCE=200809L _DEFAULT_SOURCE)
    if(APPLE)
      target_compile_definitions(recomp_net_module PRIVATE _DARWIN_C_SOURCE)
    endif()
  endif()
  if(WIN32)
    target_link_libraries(recomp_net_module PUBLIC ws2_32 iphlpapi bcrypt shell32)
    set_target_properties(recomp_net_module PROPERTIES WINDOWS_EXPORT_ALL_SYMBOLS ON)
  elseif(NOT APPLE)
    target_link_options(recomp_net_module PRIVATE
      "LINKER:--version-script=${CMAKE_CURRENT_SOURCE_DIR}/module/exports.map")
  endif()
  if(RNET_ENABLE_ICE)
    target_compile_definitions(recomp_net_module PRIVATE RNET_ENABLE_ICE=1)
    foreach(_j IN ITEMS juice-static juice)
      if(TARGET ${_j})
        set_target_properties(${_j} PROPERTIES POSITION_INDEPENDENT_CODE ON)
      endif()
    endforeach()
    target_link_libraries(recomp_net_module PRIVATE Libjuice::Libjuice)
  endif()
  if(UNIX AND NOT APPLE)
    find_package(Threads REQUIRED)
    target_link_libraries(recomp_net_module PRIVATE Threads::Threads)
  endif()
  if(MSVC)
    target_compile_options(recomp_net_module PRIVATE /W4)
  else()
    target_compile_options(recomp_net_module PRIVATE -Wall -Wextra -Wpedantic)
  endif()
  if(RNET_BUILD_TESTS AND UNIX)
    add_executable(module_test tests/module_test.c)
    target_include_directories(module_test PRIVATE include)
    target_link_libraries(module_test PRIVATE ${CMAKE_DL_LIBS})
    add_test(NAME module_test COMMAND module_test $<TARGET_FILE:recomp_net_module>)
  endif()
  install(TARGETS recomp_net_module LIBRARY DESTINATION lib RUNTIME DESTINATION bin)
endif()

# ---- Examples ----
if(RNET_BUILD_EXAMPLES)
  add_executable(lan_delay_2p examples/lan_delay_2p/main.c)
  target_link_libraries(lan_delay_2p PRIVATE recomp_net)

  add_executable(ice_manual_signaling examples/ice_manual_signaling/main.c)
  target_link_libraries(ice_manual_signaling PRIVATE recomp_net)
endif()

# ---- Tests ----
if(RNET_BUILD_TESTS)
  enable_testing()

  find_package(Python3 QUIET COMPONENTS Interpreter)
  if(Python3_Interpreter_FOUND)
    add_executable(auth_startup_probe tests/auth_startup_probe.c)
    target_link_libraries(auth_startup_probe PRIVATE recomp_net)
    add_test(NAME auth_startup_test COMMAND "${Python3_EXECUTABLE}"
      "${CMAKE_CURRENT_SOURCE_DIR}/tests/auth_startup_test.py"
      --probe "$<TARGET_FILE:auth_startup_probe>")
  endif()

  # Every public static-library function links against recomp_net. The
  # optional module's loader contract is checked by module_test above.
  include(RnetApiLinkTest)
  rnet_add_api_link_test()

  if(UNIX AND NOT APPLE)
    add_executable(open_url_test tests/open_url_test.c src/auth/rnet_open_url.c)
    target_compile_definitions(open_url_test PRIVATE _POSIX_C_SOURCE=200809L _DEFAULT_SOURCE)
    add_test(NAME open_url_test COMMAND open_url_test)
  endif()

  add_executable(chat_filter_test tests/chat_filter_test.c)
  target_link_libraries(chat_filter_test PRIVATE recomp_net)
  add_test(NAME chat_filter_test COMMAND chat_filter_test)

  add_executable(ring_admit_test tests/ring_admit_test.c)
  target_link_libraries(ring_admit_test PRIVATE recomp_net)
  target_include_directories(ring_admit_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
  add_test(NAME ring_admit_test COMMAND ring_admit_test)

  add_executable(wire_map_test tests/wire_map_test.c)
  target_link_libraries(wire_map_test PRIVATE recomp_net)
  add_test(NAME wire_map_test COMMAND wire_map_test)

  add_executable(hostport_test tests/hostport_test.c)
  target_link_libraries(hostport_test PRIVATE recomp_net)
  target_include_directories(hostport_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
  add_test(NAME hostport_test COMMAND hostport_test)

  # Two-process end-to-end check of the host relay against a real lobby
  # server (tools/host_relay_e2e.c says how to run it). Built, not a ctest:
  # it needs the server.
  add_executable(host_relay_e2e tools/host_relay_e2e.c)
  target_link_libraries(host_relay_e2e PRIVATE recomp_net)
  target_include_directories(host_relay_e2e PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)

  add_executable(host_relay_test tests/host_relay_test.c)
  target_link_libraries(host_relay_test PRIVATE recomp_net)
  target_include_directories(host_relay_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
  add_test(NAME host_relay_test COMMAND host_relay_test)

  if(RNET_ENABLE_ICE)
    # Host-as-relay over ICE: host + two guests through real libjuice agents
    # on loopback (no STUN). Single process is deliberate; see the test header.
    add_executable(ice_hub_test tests/ice_hub_test.c)
    target_link_libraries(ice_hub_test PRIVATE recomp_net)
    target_include_directories(ice_hub_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    add_test(NAME ice_hub_test COMMAND ice_hub_test)
    set_tests_properties(ice_hub_test PROPERTIES TIMEOUT 120)

    # Waiting-room agents (host_ice.h) and the handover into sessions.
    add_executable(host_ice_test tests/host_ice_test.c)
    target_link_libraries(host_ice_test PRIVATE recomp_net)
    target_include_directories(host_ice_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    add_test(NAME host_ice_test COMMAND host_ice_test)
    set_tests_properties(host_ice_test PROPERTIES TIMEOUT 180)

    # The room list's direct latency probe, pinger and responder on loopback.
    add_executable(ice_rtt_test tests/ice_rtt_test.c)
    target_link_libraries(ice_rtt_test PRIVATE recomp_net)
    target_include_directories(ice_rtt_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    add_test(NAME ice_rtt_test COMMAND ice_rtt_test)

    # The real lobby client (singleton, so this process is the host) driving
    # waiting-room agents through a launch into adopting sessions.
    if(UNIX)
      add_executable(lobby_ice_launch_test tests/lobby_ice_launch_test.c)
      target_link_libraries(lobby_ice_launch_test PRIVATE recomp_net)
      target_include_directories(lobby_ice_launch_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
      target_compile_definitions(lobby_ice_launch_test PRIVATE _POSIX_C_SOURCE=200809L _DEFAULT_SOURCE)
      add_test(NAME lobby_ice_launch_test COMMAND lobby_ice_launch_test)
      set_tests_properties(lobby_ice_launch_test PROPERTIES TIMEOUT 120)
    endif()
  endif()

  add_executable(lan_chat_test tests/lan_chat_test.c)
  target_link_libraries(lan_chat_test PRIVATE recomp_net)
  find_package(Threads REQUIRED)
  target_link_libraries(lan_chat_test PRIVATE Threads::Threads)
  add_test(NAME lan_chat_test COMMAND lan_chat_test)

  add_executable(lan_rematch_test tests/lan_rematch_test.c)
  target_link_libraries(lan_rematch_test PRIVATE recomp_net)
  target_include_directories(lan_rematch_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
  add_test(NAME lan_rematch_test COMMAND lan_rematch_test)

  add_executable(lan_lobby_test tests/lan_lobby_test.c)
  target_link_libraries(lan_lobby_test PRIVATE recomp_net)
  add_test(NAME lan_lobby_test COMMAND lan_lobby_test)

  add_executable(lan_beacon_test tests/lan_beacon_test.c)
  target_link_libraries(lan_beacon_test PRIVATE recomp_net)
  add_test(NAME lan_beacon_test COMMAND lan_beacon_test)

  add_executable(udp_port_test tests/udp_port_test.c)
  target_link_libraries(udp_port_test PRIVATE recomp_net)
  target_include_directories(udp_port_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
  add_test(NAME udp_port_test COMMAND udp_port_test)

  add_executable(address_test tests/address_test.c)
  target_link_libraries(address_test PRIVATE recomp_net)
  target_include_directories(address_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
  add_test(NAME address_test COMMAND address_test)

  add_executable(stun_test tests/stun_test.c)
  target_link_libraries(stun_test PRIVATE recomp_net)
  target_include_directories(stun_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
  add_test(NAME stun_test COMMAND stun_test)

  add_executable(session_state_test tests/session_state_test.c)
  add_executable(startup_profiles_test tests/startup_profiles_test.c)
  target_link_libraries(startup_profiles_test PRIVATE recomp_net)
  add_test(NAME startup_profiles_test COMMAND startup_profiles_test)
  add_test(NAME startup_profiles_loss_test COMMAND startup_profiles_test)
  set_tests_properties(startup_profiles_loss_test PROPERTIES
    ENVIRONMENT "RNET_SIM_LOSS_PCT=30;RNET_SIM_LATENCY_MS=2;RNET_SIM_JITTER_MS=1;RNET_SIM_SEED=42")
  # The multi-seat cases drive a lossy relay built on the internal transport.
  target_include_directories(session_state_test PRIVATE src)
  target_link_libraries(session_state_test PRIVATE recomp_net)
  if(UNIX)
    target_compile_definitions(session_state_test PRIVATE _POSIX_C_SOURCE=200809L _DEFAULT_SOURCE)
  endif()
  add_test(NAME session_state_test COMMAND session_state_test)

  add_executable(session_state_multipeer_test tests/session_state_multipeer_test.c)
  target_link_libraries(session_state_multipeer_test PRIVATE recomp_net)
  if(UNIX)
    target_compile_definitions(session_state_multipeer_test PRIVATE _POSIX_C_SOURCE=200809L _DEFAULT_SOURCE)
  endif()
  add_test(NAME session_state_multipeer_test COMMAND session_state_multipeer_test)

  add_executable(rb_ctrl_multipeer_test tests/rb_ctrl_multipeer_test.c)
  target_link_libraries(rb_ctrl_multipeer_test PRIVATE recomp_net)
  if(UNIX)
    target_compile_definitions(rb_ctrl_multipeer_test PRIVATE _POSIX_C_SOURCE=200809L _DEFAULT_SOURCE)
  endif()
  add_test(NAME rb_ctrl_multipeer_test COMMAND rb_ctrl_multipeer_test)

  add_executable(state_replay_test tests/state_replay_test.c)
  target_include_directories(state_replay_test PRIVATE src)
  target_link_libraries(state_replay_test PRIVATE recomp_net)
  if(UNIX)
    target_compile_definitions(state_replay_test PRIVATE _POSIX_C_SOURCE=200809L _DEFAULT_SOURCE)
  endif()
  add_test(NAME state_replay_test COMMAND state_replay_test)

  add_executable(input_contract_test tests/input_contract_test.c)
  target_link_libraries(input_contract_test PRIVATE recomp_net)
  add_test(NAME input_contract_test COMMAND input_contract_test)

  # The episode driver: in-process contracts, then a forked pair over UDP
  # loopback with the link simulator (both replay shapes, and a mixed pair).
  add_executable(rb_driver_test tests/rb_driver_test.c)
  target_link_libraries(rb_driver_test PRIVATE recomp_net)
  add_test(NAME rb_driver_test COMMAND rb_driver_test)

  add_executable(rollback_episode_test tests/rollback_episode_test.c)
  target_link_libraries(rollback_episode_test PRIVATE recomp_net)
  add_test(NAME rollback_episode_test COMMAND rollback_episode_test)

  add_executable(session_observer_test tests/session_observer_test.c)
  target_link_libraries(session_observer_test PRIVATE recomp_net)
  add_test(NAME session_observer_test COMMAND session_observer_test)

  add_executable(rb_wire_test tests/rb_wire_test.c)
  target_link_libraries(rb_wire_test PRIVATE recomp_net)
  target_include_directories(rb_wire_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
  add_test(NAME rb_wire_test COMMAND rb_wire_test)

  # Moved from retcomm-rbengine with the modules they cover.
  add_executable(hash_confirm_test tests/hash_confirm_test.c)
  target_link_libraries(hash_confirm_test PRIVATE recomp_net)
  add_test(NAME hash_confirm_test COMMAND hash_confirm_test)

  add_executable(rb_quorum_test tests/rb_quorum_test.c)
  target_link_libraries(rb_quorum_test PRIVATE recomp_net)
  add_test(NAME rb_quorum_test COMMAND rb_quorum_test)

  add_executable(input_hist_test tests/input_hist_test.c)
  target_link_libraries(input_hist_test PRIVATE recomp_net)
  add_test(NAME input_hist_test COMMAND input_hist_test)

  add_executable(rb_post_test tests/rb_post_test.c)
  target_link_libraries(rb_post_test PRIVATE recomp_net)
  add_test(NAME rb_post_test COMMAND rb_post_test)

  # The lobby protocol client, driven through its own translation unit. NOT
  # linked against recomp_net: the test replaces every network entry point the
  # client could reach with an aborting trap (see the file header), so it
  # compiles only the two pure modules it needs beside the client, plus the
  # platform layer for its monotonic clock.
  if(UNIX)
    add_executable(lobby_client_test
      tests/lobby_client_test.c
      src/chat/rnet_chat_filter.c
      src/chat/rnet_chat_report.c
      src/platform/rnet_platform.c
      src/platform/rnet_stun.c
      src/nat/rnet_host_relay.c
      src/nat/rnet_host_ice.c
      src/nat/rnet_sig_hold.c
      src/lobby/rnet_ice_rtt.c)
    target_include_directories(lobby_client_test PRIVATE
      ${CMAKE_CURRENT_SOURCE_DIR}/include ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_compile_definitions(lobby_client_test PRIVATE
      _POSIX_C_SOURCE=200809L _DEFAULT_SOURCE)
    add_test(NAME lobby_client_test COMMAND lobby_client_test)

    # The same client TU against a real (socketpair) socket that would-blocks
    # mid-frame: the frame-atomic outbound buffer (snesrecomp#104).
    add_executable(lobby_ws_backlog_test
      tests/lobby_ws_backlog_test.c
      src/lobby_ws/rnet_ws.c
      src/lobby_ws/rnet_sha1.c
      src/chat/rnet_chat_filter.c
      src/chat/rnet_chat_report.c
      src/platform/rnet_platform.c
      src/platform/rnet_stun.c
      src/nat/rnet_host_relay.c
      src/nat/rnet_host_ice.c
      src/nat/rnet_sig_hold.c
      src/lobby/rnet_ice_rtt.c)
    target_include_directories(lobby_ws_backlog_test PRIVATE
      ${CMAKE_CURRENT_SOURCE_DIR}/include ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_compile_definitions(lobby_ws_backlog_test PRIVATE
      _POSIX_C_SOURCE=200809L _DEFAULT_SOURCE)
    add_test(NAME lobby_ws_backlog_test COMMAND lobby_ws_backlog_test)
  endif()

  add_executable(sched_smoke_test tests/sched_smoke_test.c)
  target_link_libraries(sched_smoke_test PRIVATE recomp_net)
  target_include_directories(sched_smoke_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
  add_test(NAME sched_smoke_test COMMAND sched_smoke_test)
endif()

install(TARGETS recomp_net ARCHIVE DESTINATION lib LIBRARY DESTINATION lib)
install(DIRECTORY include/recomp_net DESTINATION include)
