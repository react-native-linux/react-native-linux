# @react-native-linux/core native build.
#
# Target list and third-party layout are modelled on Meta's own C++ host,
# private/react-native-fantom/tester/CMakeLists.txt, with the Android branches removed
# and the NDK-downloaded dependencies replaced by system packages where a distribution
# ships a usable version. See docs/cpp-toolchain.md.

cmake_minimum_required(VERSION 3.28)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

include(FetchContent)

set(RNL_VENDOR_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../../third_party/react-native/packages/react-native)

if(NOT EXISTS ${RNL_VENDOR_DIR}/ReactCommon)
    message(FATAL_ERROR "React Native sources are not vendored. Run: node scripts/vendor-react-native.ts")
endif()

set(REACT_COMMON_DIR ${RNL_VENDOR_DIR}/ReactCommon)
set(REACT_CXX_PLATFORM_DIR ${RNL_VENDOR_DIR}/ReactCxxPlatform)
set(REACT_ANDROID_DIR ${RNL_VENDOR_DIR}/ReactAndroid)

find_program(RNL_CCACHE ccache)

if(RNL_CCACHE)
    set(CMAKE_C_COMPILER_LAUNCHER ${RNL_CCACHE})
    set(CMAKE_CXX_COMPILER_LAUNCHER ${RNL_CCACHE})
endif()

# ---------------------------------------------------------------------------
# Third-party dependencies
#
# Versions that must match React Native's pins live in
# packages/react-native/gradle/libs.versions.toml upstream. folly is compiled from
# source because ReactCommon compiles every translation unit with -DFOLLY_NO_CONFIG=1,
# which is ABI-incompatible with a distribution folly built against folly-config.h.
# ---------------------------------------------------------------------------

set(RNL_FOLLY_VERSION 2024.11.18.00)
set(RNL_FAST_FLOAT_VERSION 8.0.0)

find_package(Boost CONFIG REQUIRED)
find_package(double-conversion CONFIG REQUIRED)
find_package(fmt CONFIG REQUIRED)
find_package(glog CONFIG REQUIRED)
find_package(Threads REQUIRED)

add_library(boost INTERFACE)
target_link_libraries(boost INTERFACE Boost::headers)
target_compile_options(boost INTERFACE -fexceptions -frtti)

add_library(double-conversion INTERFACE)
target_link_libraries(double-conversion INTERFACE double-conversion::double-conversion)

add_library(fmt INTERFACE)
target_link_libraries(fmt INTERFACE fmt::fmt)

add_library(glog INTERFACE)
target_link_libraries(glog INTERFACE glog::glog)

# fbgloginit is an Android-only shim; ReactCommon links the target unconditionally.
add_library(glog_init INTERFACE)

FetchContent_Declare(fast_float
    URL https://github.com/fastfloat/fast_float/archive/refs/tags/v${RNL_FAST_FLOAT_VERSION}.tar.gz
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    SOURCE_SUBDIR rnl-do-not-configure)
FetchContent_Declare(folly
    URL https://github.com/facebook/folly/archive/refs/tags/v${RNL_FOLLY_VERSION}.tar.gz
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    SOURCE_SUBDIR rnl-do-not-configure)
FetchContent_MakeAvailable(fast_float folly)

add_library(fast_float INTERFACE)
target_include_directories(fast_float INTERFACE ${fast_float_SOURCE_DIR}/include)

# The source list mirrors the folly subset React Native compiles in
# packages/react-native/ReactAndroid/src/main/jni/third-party/folly/CMakeLists.txt.
set(RNL_FOLLY_SOURCES
    folly/Conv.cpp
    folly/Demangle.cpp
    folly/FileUtil.cpp
    folly/Format.cpp
    folly/ScopeGuard.cpp
    folly/SharedMutex.cpp
    folly/String.cpp
    folly/Unicode.cpp
    folly/concurrency/CacheLocality.cpp
    folly/container/detail/F14Table.cpp
    folly/detail/FileUtilDetail.cpp
    folly/detail/Futex.cpp
    folly/detail/SplitStringSimd.cpp
    folly/detail/UniqueInstance.cpp
    folly/hash/SpookyHashV2.cpp
    folly/io/IOBuf.cpp
    folly/json/dynamic.cpp
    folly/json/json_pointer.cpp
    folly/json/json.cpp
    folly/lang/CString.cpp
    folly/lang/Exception.cpp
    folly/lang/SafeAssert.cpp
    folly/lang/ToAscii.cpp
    folly/memory/SanitizeAddress.cpp
    folly/memory/detail/MallocImpl.cpp
    folly/net/NetOps.cpp
    folly/portability/SysUio.cpp
    folly/synchronization/SanitizeThread.cpp
    folly/synchronization/ParkingLot.cpp
    folly/system/AtFork.cpp
    folly/system/ThreadId.cpp
    folly/system/ThreadName.cpp)
list(TRANSFORM RNL_FOLLY_SOURCES PREPEND ${folly_SOURCE_DIR}/)

add_library(folly_runtime STATIC ${RNL_FOLLY_SOURCES})
target_include_directories(folly_runtime PUBLIC ${folly_SOURCE_DIR})
# FOLLY_USE_LIBCPP and FOLLY_HAVE_XSI_STRERROR_R, which the Android and fantom builds set,
# are deliberately absent: this build is glibc + libstdc++, where both are wrong.
target_compile_definitions(folly_runtime PUBLIC
    FOLLY_NO_CONFIG=1
    FOLLY_MOBILE=0
    FOLLY_CFG_NO_COROUTINES=1
    FOLLY_HAVE_CLOCK_GETTIME=1
    FOLLY_HAVE_PTHREAD=1
    FOLLY_HAVE_RECVMMSG=1)
target_compile_options(folly_runtime PRIVATE -fexceptions -frtti -fno-omit-frame-pointer -w)
target_link_libraries(folly_runtime
    PUBLIC
        boost
        double-conversion
        fast_float
        fmt
        glog
        Threads::Threads
        ${CMAKE_DL_LIBS})
set_target_properties(folly_runtime PROPERTIES CXX_STANDARD 20)

# ---------------------------------------------------------------------------
# Hermes
#
# There is no prebuilt Hermes runtime for Linux, so the VM is built from source at the
# version React Native pins in sdks/hermes-engine/version.properties. Three hazards are
# handled here:
#   1. HERMES_ENABLE_TOOLS must stay ON even though we ship no CLI: Hermes precompiles
#      InternalBytecode with its own hermesc and Ninja fails with a missing rule otherwise.
#   2. Hermes' CDP and llvh sources rely on standard headers arriving transitively, which
#      stopped being true with libstdc++ 13. The force-includes below are scoped to the
#      Hermes add_subdirectory and restored afterwards.
#   3. HERMES_ENABLE_TEST_SUITE must stay OFF: Hermes' bundled llvh ships its own
#      googletest, which collides with an upstream googletest in the same configure.
#      Wiring GoogleTest for packages/core has to account for that.
# ---------------------------------------------------------------------------

file(READ ${RNL_VENDOR_DIR}/sdks/hermes-engine/version.properties RNL_HERMES_VERSION_PROPERTIES)
string(REGEX MATCH "HERMES_VERSION_NAME=([0-9A-Za-z.\\-]+)" RNL_HERMES_VERSION_MATCH "${RNL_HERMES_VERSION_PROPERTIES}")

if(NOT CMAKE_MATCH_1)
    message(FATAL_ERROR "Could not read HERMES_VERSION_NAME from sdks/hermes-engine/version.properties")
endif()

set(RNL_HERMES_VERSION ${CMAKE_MATCH_1})
message(STATUS "Hermes pinned by React Native: ${RNL_HERMES_VERSION}")

# JSI_DIR makes Hermes build its jsi target from React Native's own copy, so ReactCommon
# and Hermes share one jsi rather than defining the target name twice.
set(JSI_DIR ${REACT_COMMON_DIR}/jsi CACHE PATH "" FORCE)
set(HERMES_BUILD_APPLE_FRAMEWORK OFF CACHE BOOL "" FORCE)
set(HERMES_BUILD_SHARED_JSI ON CACHE BOOL "" FORCE)
set(HERMES_ENABLE_DEBUGGER OFF CACHE BOOL "" FORCE)
set(HERMES_ENABLE_INTL OFF CACHE BOOL "" FORCE)
set(HERMES_ENABLE_TEST_SUITE OFF CACHE BOOL "" FORCE)
set(HERMES_ENABLE_TOOLS ON CACHE BOOL "" FORCE)

FetchContent_Declare(hermes
    GIT_REPOSITORY https://github.com/facebook/hermes.git
    GIT_TAG hermes-v${RNL_HERMES_VERSION}
    GIT_SHALLOW TRUE
    GIT_PROGRESS TRUE)

set(RNL_SAVED_CXX_FLAGS "${CMAKE_CXX_FLAGS}")

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    set(CMAKE_CXX_FLAGS
        "${CMAKE_CXX_FLAGS} -include cstdint -include cstring -include cstddef -include string -include memory -include vector")
endif()

FetchContent_MakeAvailable(hermes)

set(CMAKE_CXX_FLAGS "${RNL_SAVED_CXX_FLAGS}")

if(NOT TARGET hermesvm)
    message(FATAL_ERROR "Hermes did not define the hermesvm target; check the pinned Hermes version")
endif()

# Hermes only compiles jsi.cpp. ReactCommon additionally needs the folly<->jsi bridge and
# the POSIX jsilib, which live in the same React Native directory.
target_sources(jsi PRIVATE
    ${REACT_COMMON_DIR}/jsi/jsi/JSIDynamic.cpp
    ${REACT_COMMON_DIR}/jsi/jsi/jsilib-posix.cpp)
target_link_libraries(jsi PUBLIC folly_runtime glog)
set_target_properties(jsi PROPERTIES CXX_STANDARD 20)

# ---------------------------------------------------------------------------
# ReactCommon
#
# This is the transitive closure of react_renderer_core and yoga. The remaining fantom
# subdirectories (react/runtime, cxxreact, the nativemodule and component trees, and all
# of ReactCxxPlatform) land with the ReactInstance bring-up in issue #9.
# ---------------------------------------------------------------------------

add_subdirectory(${REACT_COMMON_DIR}/yoga ReactCommon/yoga)
add_library(yoga ALIAS yogacore)

set(RNL_REACT_COMMON_SUBDIRS
    callinvoker
    jserrorhandler
    jsinspector-modern
    jsinspector-modern/cdp
    jsinspector-modern/network
    jsinspector-modern/tracing
    logger
    oscompat
    react/debug
    react/featureflags
    react/performance/timeline
    react/renderer/consistency
    react/renderer/core
    react/renderer/debug
    react/renderer/graphics
    react/renderer/mapbuffer
    react/renderer/runtimescheduler
    react/timing
    react/utils
    reactperflogger
    runtimeexecutor)

foreach(rnl_subdir IN LISTS RNL_REACT_COMMON_SUBDIRS)
    add_subdirectory(${REACT_COMMON_DIR}/${rnl_subdir} ReactCommon/${rnl_subdir})
endforeach()

set(RNL_REACT_COMMON_TARGETS
    callinvoker
    jserrorhandler
    jsinspector
    jsinspector_cdp
    jsinspector_network
    jsinspector_tracing
    logger
    oscompat
    react_debug
    react_featureflags
    react_performance_timeline
    react_renderer_consistency
    react_renderer_core
    react_renderer_debug
    react_renderer_graphics
    react_renderer_mapbuffer
    react_renderer_runtimescheduler
    react_timing
    react_utils
    reactperflogger
    runtimeexecutor
    yogacore)

# ReactCommon compiles with -Werror against the compilers Meta ships. Downgrade it rather
# than carry a patch per distribution compiler; our own sources keep -Werror below.
foreach(rnl_target IN LISTS RNL_REACT_COMMON_TARGETS)
    get_target_property(rnl_target_type ${rnl_target} TYPE)

    if(rnl_target_type STREQUAL "INTERFACE_LIBRARY")
        target_compile_options(${rnl_target} INTERFACE -Wno-error)
    else()
        target_compile_options(${rnl_target} PRIVATE -Wno-error)
    endif()
endforeach()

# ---------------------------------------------------------------------------
# hello_react
# ---------------------------------------------------------------------------

add_executable(hello_react src/main.cpp)
target_compile_options(hello_react PRIVATE -Wall -Wextra -fexceptions -frtti)
target_link_libraries(hello_react
    PRIVATE
        hermesvm
        jsi
        folly_runtime
        ${RNL_REACT_COMMON_TARGETS})
set_target_properties(hello_react PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
