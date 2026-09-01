# @react-native-linux/core native build.
#
# Target list and third-party layout are modelled on Meta's own C++ host,
# private/react-native-fantom/tester/CMakeLists.txt, with the Android branches removed
# and the NDK-downloaded dependencies replaced by system packages where a distribution
# ships a usable version. See docs/cpp-toolchain.md.

cmake_minimum_required(VERSION 3.28)

set(CMAKE_POSITION_INDEPENDENT_CODE ON)

include(FetchContent)

set(RNL_VENDOR_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../../third_party/react-native/packages/react-native)

if(NOT EXISTS ${RNL_VENDOR_DIR}/ReactCommon)
    message(FATAL_ERROR "React Native sources are not vendored. Run: node scripts/vendor-react-native.ts")
endif()

set(REACT_COMMON_DIR ${RNL_VENDOR_DIR}/ReactCommon)
# ReactCommon's jsinspector CMakeLists include SoMerging-utils.cmake from here; the function
# it defines is a no-op off Android, but the include itself is unconditional.
set(REACT_ANDROID_DIR ${RNL_VENDOR_DIR}/ReactAndroid)
# ReactCxxPlatform subdirectory CMakeLists resolve their own include root through this variable.
set(REACT_CXX_PLATFORM_DIR ${RNL_VENDOR_DIR}/ReactCxxPlatform)

include(${REACT_COMMON_DIR}/cmake-utils/react-native-flags.cmake)
include(${REACT_COMMON_DIR}/cmake-utils/internal/react-native-platform-selector.cmake)

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
set(RNL_BOOST_VERSION 1.83.0)
set(RNL_GLOG_VERSION 0.7.1)

# Everything fetched here is static; Hermes re-enables shared libraries for jsi in its own
# scope. Declaring it up front also stops glog's own option() from writing BUILD_SHARED_LIBS
# into the cache.
set(BUILD_SHARED_LIBS OFF)

find_package(double-conversion CONFIG REQUIRED)
find_package(fmt CONFIG REQUIRED)
find_package(Threads REQUIRED)

add_library(double-conversion INTERFACE)
target_link_libraries(double-conversion INTERFACE double-conversion::double-conversion)

add_library(fmt INTERFACE)
target_link_libraries(fmt INTERFACE fmt::fmt)

# Boost is used headers-only. React Native's own boost target is an INTERFACE library with
# nothing but an include directory, and its Gradle task extracts only boost/**/*.hpp, so the
# whole of ReactCommon and the folly subset build without a single compiled Boost library.
find_package(Boost ${RNL_BOOST_VERSION} CONFIG QUIET)

if(TARGET Boost::headers)
    message(STATUS "Boost: system ${Boost_VERSION}")
    add_library(boost INTERFACE)
    target_link_libraries(boost INTERFACE Boost::headers)
else()
    message(STATUS "Boost: FetchContent ${RNL_BOOST_VERSION}")
    # The boostorg/boost release archives are the modular CMake superproject: they carry no
    # merged boost/ include tree, so using them would mean naming every Boost module folly
    # and ReactCommon reach through. The classic source archive is the same artifact React
    # Native downloads, and needs no configure step at all.
    string(REPLACE "." "_" RNL_BOOST_ARCHIVE_VERSION ${RNL_BOOST_VERSION})
    FetchContent_Declare(boost
        URL https://archives.boost.io/release/${RNL_BOOST_VERSION}/source/boost_${RNL_BOOST_ARCHIVE_VERSION}.tar.gz
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE
        SOURCE_SUBDIR rnl-do-not-configure)
    FetchContent_MakeAvailable(boost)

    add_library(boost INTERFACE)
    target_include_directories(boost INTERFACE ${boost_SOURCE_DIR})
endif()

target_compile_options(boost INTERFACE -fexceptions -frtti)

# React Native pins glog 0.3.5, which has no usable standalone CMake build: Gradle generates
# its config.h and substitutes tokens into the .h.in headers. The fallback therefore builds
# 0.7.1, whose own CMake defines the target name ReactCommon links, `glog`.
find_package(glog ${RNL_GLOG_VERSION} CONFIG QUIET)

if(TARGET glog::glog)
    message(STATUS "glog: system ${glog_VERSION}")
    add_library(glog INTERFACE)
    target_link_libraries(glog INTERFACE glog::glog)
else()
    message(STATUS "glog: FetchContent ${RNL_GLOG_VERSION}")
    set(WITH_GFLAGS OFF CACHE BOOL "" FORCE)
    set(WITH_GTEST OFF CACHE BOOL "" FORCE)
    set(WITH_PKGCONFIG OFF CACHE BOOL "" FORCE)
    set(WITH_UNWIND none CACHE STRING "" FORCE)
    FetchContent_Declare(glog
        GIT_REPOSITORY https://github.com/google/glog.git
        GIT_TAG v${RNL_GLOG_VERSION}
        GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(glog)
endif()

# fbgloginit is an Android-only shim; ReactCommon links the target unconditionally.
add_library(glog_init INTERFACE)

# React Native's codegen output is produced by Gradle and is not vendored, so react_codegen_rncore has no
# definition here. react_renderer_animationbackend is the only target in this graph that names it, and none of
# its sources include a generated header, so an empty interface library satisfies the link line.
add_library(react_codegen_rncore INTERFACE)

FetchContent_Declare(fast_float
    URL https://github.com/fastfloat/fast_float/archive/refs/tags/v${RNL_FAST_FLOAT_VERSION}.tar.gz
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    SOURCE_SUBDIR rnl-do-not-configure)
FetchContent_Declare(folly
    URL https://github.com/facebook/folly/archive/refs/tags/v${RNL_FOLLY_VERSION}.tar.gz
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    PATCH_COMMAND sed -i "s|__has_include(<demangle.h>)|0|g" folly/Demangle.cpp
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
string(REGEX MATCH "HERMES_VERSION_NAME=([0-9A-Za-z.]+)" RNL_HERMES_VERSION_MATCH "${RNL_HERMES_VERSION_PROPERTIES}")

if(NOT RNL_HERMES_VERSION_MATCH)
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
set(HERMES_ENABLE_RTTI ON CACHE BOOL "" FORCE)
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

# Hermes only compiles jsi.cpp, and it builds libjsi.so with -fvisibility=hidden, which
# strips the unannotated JSIDynamic/jsilib symbols. ReactCommon additionally needs the
# folly<->jsi bridge and the POSIX jsilib, so they get their own static library with
# default visibility instead of being appended to Hermes' target.
target_link_libraries(jsi PUBLIC folly_runtime glog)
set_target_properties(jsi PROPERTIES CXX_STANDARD 20)

add_library(rnl_jsidynamic STATIC
    ${REACT_COMMON_DIR}/jsi/jsi/JSIDynamic.cpp
    ${REACT_COMMON_DIR}/jsi/jsi/jsilib-posix.cpp)
target_include_directories(rnl_jsidynamic PUBLIC ${REACT_COMMON_DIR}/jsi)
target_compile_options(rnl_jsidynamic PRIVATE -fexceptions -frtti)
target_link_libraries(rnl_jsidynamic PUBLIC jsi folly_runtime glog)
set_target_properties(rnl_jsidynamic PROPERTIES CXX_STANDARD 20)

# ---------------------------------------------------------------------------
# ReactCommon
#
# This is the transitive closure of react_renderer_core, yoga and the bridgeless
# ReactInstance. Four subdirectories were added for the ReactInstance bring-up, each because a
# target already in the graph names it:
#   cxxreact    -> react_cxxreact: JSBigString/JSBigFileString bundle loading and
#                  MessageQueueThread, named by jsitooling and jsireact.
#   jsitooling  -> jsitooling: JSRuntime/JSRuntimeFactory/JSIRuntimeHolder and
#                  bindNativeLogger/bindNativePerformanceNow, named by bridgeless.
#   jsiexecutor -> jsireact: named by bridgeless. Nothing here calls JSIExecutor, but the
#                  target name has to resolve or the bridgeless link line breaks.
#   react/runtime -> bridgeless: ReactInstance, TimerManager, BufferedRuntimeExecutor.
# None of them adds a system dependency: react/runtime/hermes (bridgelesshermes) is the one
# that would, through hermes_executor_common and hermes_inspector_modern, and it is not used.
#
# The Fabric bootstrap adds the transitive closure of react_renderer_scheduler. Every entry
# below is named by a target already in that closure, none of them introduces a system
# dependency, and the two text subdirectories arrive because react_renderer_uimanager links
# react_renderer_dom, which links rrc_text. The C++ platform selector resolves rrc_text and
# react_renderer_textlayoutmanager to their platform/cxx sources, where TextLayoutManager is a
# stub with no shaping engine behind it.
#
# ReactCxxPlatform's react/renderer/{uimanager,scheduler} carry IMountingManager and
# SchedulerDelegateImpl. They are the only two subdirectories of that tree used here besides
# react/threading; ReactCxxPlatform's ReactHost is not, because it pulls bridgelesshermes, the
# HTTP and WebSocket clients, and the whole nativemodule tree.
# ---------------------------------------------------------------------------

# Scoped to C++ so it does not leak into wayland-scanner's generated C, which cannot include <cstdint>.
add_compile_options("$<$<COMPILE_LANGUAGE:CXX>:SHELL:-include cstdint>")

add_subdirectory(${REACT_COMMON_DIR}/yoga ReactCommon/yoga)
add_library(yoga ALIAS yogacore)

set(RNL_REACT_COMMON_SUBDIRS
    callinvoker
    cxxreact
    jserrorhandler
    jsiexecutor
    jsinspector-modern
    jsinspector-modern/cdp
    jsinspector-modern/network
    jsinspector-modern/tracing
    jsitooling
    logger
    oscompat
    react/debug
    react/featureflags
    react/performance/cdpmetrics
    react/performance/timeline
    react/renderer/animationbackend
    react/renderer/attributedstring
    react/renderer/bridging
    react/renderer/componentregistry
    react/renderer/components/legacyviewmanagerinterop
    react/renderer/components/root
    react/renderer/components/scrollview
    react/renderer/components/text
    react/renderer/components/view
    react/renderer/consistency
    react/renderer/core
    react/renderer/css
    react/renderer/debug
    react/renderer/dom
    react/renderer/graphics
    react/renderer/leakchecker
    react/renderer/mapbuffer
    react/renderer/mounting
    react/renderer/observers/events
    react/renderer/runtimescheduler
    react/renderer/scheduler
    react/renderer/telemetry
    react/renderer/textlayoutmanager
    react/renderer/uimanager
    react/renderer/uimanager/consistency
    react/renderer/viewtransition
    react/runtime
    react/timing
    react/utils
    reactperflogger
    runtimeexecutor)

foreach(rnl_subdir IN LISTS RNL_REACT_COMMON_SUBDIRS)
    add_subdirectory(${REACT_COMMON_DIR}/${rnl_subdir} ReactCommon/${rnl_subdir})
endforeach()

set(RNL_REACT_CXX_PLATFORM_SUBDIRS
    react/renderer/scheduler
    react/renderer/uimanager
    react/threading)

foreach(rnl_subdir IN LISTS RNL_REACT_CXX_PLATFORM_SUBDIRS)
    add_subdirectory(${REACT_CXX_PLATFORM_DIR}/${rnl_subdir} ReactCxxPlatform/${rnl_subdir})
endforeach()

set(RNL_REACT_COMMON_TARGETS
    bridgeless
    callinvoker
    jserrorhandler
    jsinspector
    jsinspector_cdp
    jsinspector_network
    jsinspector_tracing
    jsireact
    jsitooling
    logger
    oscompat
    react_cxx_platform_react_renderer_scheduler
    react_cxx_platform_react_renderer_uimanager
    react_cxx_platform_react_threading
    react_cxxreact
    react_debug
    react_featureflags
    react_performance_cdpmetrics
    react_performance_timeline
    react_renderer_animationbackend
    react_renderer_attributedstring
    react_renderer_bridging
    react_renderer_componentregistry
    react_renderer_consistency
    react_renderer_core
    react_renderer_css
    react_renderer_debug
    react_renderer_dom
    react_renderer_graphics
    react_renderer_leakchecker
    react_renderer_mapbuffer
    react_renderer_mounting
    react_renderer_observers_events
    react_renderer_runtimescheduler
    react_renderer_scheduler
    react_renderer_telemetry
    react_renderer_textlayoutmanager
    react_renderer_uimanager
    react_renderer_uimanager_consistency
    react_renderer_viewtransition
    react_timing
    react_utils
    reactperflogger
    rrc_legacyviewmanagerinterop
    rrc_root
    rrc_scrollview
    rrc_text
    rrc_view
    runtimeexecutor
    yogacore)

# bridgeless includes react/renderer and react/timing headers that its own CMakeLists does not
# name, and consumes the RuntimeScheduler. Those usage requirements are added here rather than
# by patching the vendored file.
target_link_libraries(bridgeless
    react_renderer_consistency
    react_renderer_core
    react_renderer_runtimescheduler
    react_timing)

# ReactCommon compiles with -Werror against the compilers Meta ships. Downgrade it rather
# than carry a patch for every distribution compiler that adds a new warning.
foreach(rnl_target IN LISTS RNL_REACT_COMMON_TARGETS)
    get_target_property(rnl_target_type ${rnl_target} TYPE)

    if(rnl_target_type STREQUAL "INTERFACE_LIBRARY")
        target_compile_options(${rnl_target} INTERFACE -Wno-error)
    else()
        target_compile_options(${rnl_target} PRIVATE -Wno-error)
    endif()
endforeach()

# ---------------------------------------------------------------------------
# rnl_react_core
#
# The engine and Fabric half of the platform: the bridgeless ReactInstance, the Fabric host and the retained
# scene. Both executables link it, so the ReactCommon closure and the atomic hazard from the fix ledger are named
# once instead of being duplicated onto each target. Nothing here draws: Skia and the SK_* definitions live on
# rnl_scene_painter, which is the only translation unit that turns a scene into pixels.
# ---------------------------------------------------------------------------

add_library(rnl_react_core STATIC
    src/ConsoleBinding.cpp
    src/FabricHost.cpp
    src/HermesJSRuntimeFactory.cpp
    src/HostTimerRegistry.cpp
    src/JsErrorReporter.cpp
    src/LinuxMountingManager.cpp
    src/ReactHost.cpp
    src/RetainedScene.cpp)
target_compile_options(rnl_react_core PRIVATE -Wall -Wextra)
target_compile_options(rnl_react_core PUBLIC -fexceptions -frtti)
target_include_directories(rnl_react_core PUBLIC src)
target_link_libraries(rnl_react_core
    PUBLIC
        hermesvm
        jsi
        rnl_jsidynamic
        folly_runtime
        ${RNL_REACT_COMMON_TARGETS}
        atomic)

# ---------------------------------------------------------------------------
# Skia, and the one place the scene is painted
#
# Skia has two consumers now: rnl_window draws the retained scene into a swapchain-backed SkSurface, and
# hello_react --golden draws the same scene into an offscreen raster SkSurface for the golden-image rig. The
# detection therefore lives here rather than inside the window block, and rnl_scene_painter carries the single
# paint implementation both of them call. A golden that came from a second painter would prove nothing.
#
# Skia is a prebuilt static archive pinned by scripts/skia.lock.json; see docs/cpp-toolchain.md for the
# acquisition decision and the accepted risk. Configure with -DRNL_ENABLE_SKIA=OFF to skip it and everything
# that draws.
# ---------------------------------------------------------------------------

option(RNL_ENABLE_SKIA "Build the Skia-dependent targets: the scene painter, rnl_window, and hello_react --golden" ON)

if(RNL_ENABLE_SKIA)
    set(RNL_SKIA_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../../third_party/skia)
    set(RNL_SKIA_LIBRARY ${RNL_SKIA_DIR}/lib/libskia.a)
    set(RNL_SKIA_MISSING "")

    find_package(PkgConfig QUIET)

    if(PkgConfig_FOUND)
        pkg_check_modules(RNL_FREETYPE QUIET IMPORTED_TARGET freetype2)
        pkg_check_modules(RNL_FONTCONFIG QUIET IMPORTED_TARGET fontconfig)
    else()
        list(APPEND RNL_SKIA_MISSING "pkg-config")
    endif()

    if(NOT RNL_FREETYPE_FOUND)
        list(APPEND RNL_SKIA_MISSING "freetype2")
    endif()

    if(NOT RNL_FONTCONFIG_FOUND)
        list(APPEND RNL_SKIA_MISSING "fontconfig")
    endif()

    if(NOT EXISTS "${RNL_SKIA_LIBRARY}")
        list(APPEND RNL_SKIA_MISSING "third_party/skia (run: node scripts/vendor-skia.ts)")
    endif()
endif()

if(RNL_ENABLE_SKIA AND RNL_SKIA_MISSING)
    string(REPLACE ";" ", " RNL_SKIA_MISSING_TEXT "${RNL_SKIA_MISSING}")
    message(WARNING "Skia is unavailable, missing: ${RNL_SKIA_MISSING_TEXT}; rnl_window and hello_react --golden are disabled")
elseif(RNL_ENABLE_SKIA)
    # SK_RELEASE is not an optimisation switch here: the pinned archive is a release build, and
    # Skia's public headers change class layout when SK_DEBUG is defined, so a Debug preset would
    # otherwise compile against a different ABI than the one in libskia.a.
    add_library(rnl_skia STATIC IMPORTED GLOBAL)
    set_target_properties(rnl_skia PROPERTIES
        IMPORTED_LOCATION ${RNL_SKIA_LIBRARY}
        INTERFACE_INCLUDE_DIRECTORIES ${RNL_SKIA_DIR}
        INTERFACE_COMPILE_DEFINITIONS "SK_GANESH;SK_VULKAN;SK_RELEASE")

    add_library(rnl_scene_painter STATIC src/ScenePainter.cpp)
    target_compile_options(rnl_scene_painter PRIVATE -Wall -Wextra)
    target_include_directories(rnl_scene_painter PUBLIC src)
    target_link_libraries(rnl_scene_painter PUBLIC rnl_react_core rnl_skia)
endif()

# ---------------------------------------------------------------------------
# hello_react
#
# The headless runner. With Skia available it also carries --golden, which renders a bundle's final scene into an
# offscreen raster surface and writes a PNG: no GPU, no Vulkan driver, no Wayland compositor. libskia.a is a
# static archive, so only the raster and PNG objects are pulled in; nothing here names GrDirectContext or any
# Vulkan entry point. libpng and zlib are inside the archive, so the only new link inputs are freetype and
# fontconfig, which Skia's font ports reference and which rnl_window already required.
# ---------------------------------------------------------------------------

add_executable(hello_react
    src/BundleRunner.cpp
    src/main.cpp)
target_compile_options(hello_react PRIVATE -Wall -Wextra)
target_link_libraries(hello_react PRIVATE rnl_react_core)
set_target_properties(hello_react PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

if(TARGET rnl_scene_painter)
    target_sources(hello_react PRIVATE src/GoldenRenderer.cpp)
    target_compile_definitions(hello_react PRIVATE RNL_ENABLE_GOLDEN)
    target_link_libraries(hello_react
        PRIVATE
            rnl_scene_painter
            PkgConfig::RNL_FREETYPE
            PkgConfig::RNL_FONTCONFIG
            Threads::Threads
            ${CMAKE_DL_LIBS})
endif()

# ---------------------------------------------------------------------------
# rnl_window
#
# The Wayland/Vulkan/Skia host from issue #8, drawing the retained scene from issue #32. It shares
# the engine and Fabric half with hello_react through rnl_react_core, the paint code path through
# rnl_scene_painter, and adds only the drawing and windowing sources. It is skipped rather than fatal when its
# dependencies are absent, so a machine with neither Skia nor a Wayland development environment still builds the
# headless target. Configure with -DRNL_ENABLE_WINDOW=OFF to skip it deliberately.
# ---------------------------------------------------------------------------

option(RNL_ENABLE_WINDOW "Build rnl_window, the Wayland/Vulkan/Skia host" ON)

if(RNL_ENABLE_WINDOW)
    set(RNL_WINDOW_MISSING "")

    find_package(Vulkan QUIET)

    if(PkgConfig_FOUND)
        pkg_check_modules(RNL_WAYLAND_CLIENT QUIET IMPORTED_TARGET wayland-client)
        pkg_get_variable(RNL_WAYLAND_SCANNER wayland-scanner wayland_scanner)
        pkg_get_variable(RNL_WAYLAND_PROTOCOLS_DIR wayland-protocols pkgdatadir)
    endif()

    set(RNL_XDG_SHELL_XML ${RNL_WAYLAND_PROTOCOLS_DIR}/stable/xdg-shell/xdg-shell.xml)

    if(NOT PkgConfig_FOUND)
        list(APPEND RNL_WINDOW_MISSING "pkg-config")
    endif()

    if(NOT RNL_WAYLAND_CLIENT_FOUND)
        list(APPEND RNL_WINDOW_MISSING "wayland-client")
    endif()

    if(NOT RNL_WAYLAND_SCANNER OR NOT EXISTS "${RNL_WAYLAND_SCANNER}")
        list(APPEND RNL_WINDOW_MISSING "wayland-scanner")
    endif()

    if(NOT RNL_WAYLAND_PROTOCOLS_DIR OR NOT EXISTS "${RNL_XDG_SHELL_XML}")
        list(APPEND RNL_WINDOW_MISSING "wayland-protocols (stable/xdg-shell)")
    endif()

    if(NOT Vulkan_FOUND)
        list(APPEND RNL_WINDOW_MISSING "Vulkan loader and headers")
    endif()

    if(NOT TARGET rnl_scene_painter)
        list(APPEND RNL_WINDOW_MISSING "Skia (see the warning above)")
    endif()
endif()

if(RNL_ENABLE_WINDOW AND RNL_WINDOW_MISSING)
    string(REPLACE ";" ", " RNL_WINDOW_MISSING_TEXT "${RNL_WINDOW_MISSING}")
    message(WARNING "rnl_window is disabled, missing: ${RNL_WINDOW_MISSING_TEXT}")
elseif(RNL_ENABLE_WINDOW)
    set(RNL_PROTOCOL_DIR ${CMAKE_CURRENT_BINARY_DIR}/wayland-protocols)
    file(MAKE_DIRECTORY ${RNL_PROTOCOL_DIR})

    add_custom_command(
        OUTPUT ${RNL_PROTOCOL_DIR}/xdg-shell-client-protocol.h
        COMMAND ${RNL_WAYLAND_SCANNER} client-header ${RNL_XDG_SHELL_XML}
                ${RNL_PROTOCOL_DIR}/xdg-shell-client-protocol.h
        DEPENDS ${RNL_XDG_SHELL_XML}
        COMMENT "wayland-scanner client-header xdg-shell"
        VERBATIM)

    add_custom_command(
        OUTPUT ${RNL_PROTOCOL_DIR}/xdg-shell-protocol.c
        COMMAND ${RNL_WAYLAND_SCANNER} private-code ${RNL_XDG_SHELL_XML}
                ${RNL_PROTOCOL_DIR}/xdg-shell-protocol.c
        DEPENDS ${RNL_XDG_SHELL_XML}
        COMMENT "wayland-scanner private-code xdg-shell"
        VERBATIM)

    add_library(rnl_xdg_shell STATIC
        ${RNL_PROTOCOL_DIR}/xdg-shell-protocol.c
        ${RNL_PROTOCOL_DIR}/xdg-shell-client-protocol.h)
    target_include_directories(rnl_xdg_shell PUBLIC ${RNL_PROTOCOL_DIR})
    target_link_libraries(rnl_xdg_shell PUBLIC PkgConfig::RNL_WAYLAND_CLIENT)

    add_executable(rnl_window
        src/SkiaVulkanRenderer.cpp
        src/WaylandWindow.cpp
        src/WindowMain.cpp
        src/WindowSession.cpp)
    target_compile_options(rnl_window PRIVATE -Wall -Wextra)
    target_include_directories(rnl_window PRIVATE src)
    # libskia.a is a single static archive built against system freetype and fontconfig, so those two
    # have to follow it on the link line rather than travel as usage requirements of the archive.
    target_link_libraries(rnl_window
        PRIVATE
            rnl_react_core
            rnl_scene_painter
            rnl_xdg_shell
            rnl_skia
            Vulkan::Vulkan
            PkgConfig::RNL_FREETYPE
            PkgConfig::RNL_FONTCONFIG
            Threads::Threads
            ${CMAKE_DL_LIBS})
    set_target_properties(rnl_window PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
endif()
