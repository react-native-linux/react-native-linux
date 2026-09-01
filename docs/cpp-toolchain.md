# C++ toolchain

`packages/core` builds `hello_react`: a bridgeless `ReactInstance` running a JavaScript bundle on Hermes, linked
against the React Native renderer core and Yoga. It is the toolchain proof for issue #3 and the engine-embedding
proof for issue #9, not a renderer.

## Scope

`hello_react` boots the upstream bridgeless stack: a `JSRuntimeFactory` that wraps `makeHermesRuntime` in
`JSIRuntimeHolder`, `ReactCxxPlatform`'s `MessageQueueThreadImpl` as the JS thread, a `PlatformTimerRegistry`
implementation on a `TaskDispatchThread` feeding `TimerManager`, and `JsErrorHandler::OnJsError` printing structured
errors to stderr. `console` and `nativeLoggingHook` are installed in C++ and write to stdout and stderr. The
executable takes an optional bundle path and falls back to an inline smoke line without one.

`react/runtime/hermes` (`bridgelesshermes`) is deliberately not used: it needs `hermes_executor_common` and
`hermes_inspector_modern`, and the debugger is off in this build. The consequence is that `JSRuntime` falls back to
`FallbackRuntimeTargetDelegate`, so no CDP-backed console or sampling profiler. Fabric, TurboModules, the
nativemodule and component trees, and the remaining eleven `ReactCxxPlatform` subdirectories are still out.

## Pins

| Component | Pin | Source of truth |
| --- | --- | --- |
| React Native | `v0.87.1` | `scripts/vendor.lock.json` |
| Hermes | `hermes-v250829098.0.17` | `third_party/react-native/packages/react-native/sdks/hermes-engine/version.properties`, read at configure time |
| folly | `v2024.11.18.00` | `RNL_FOLLY_VERSION` in `packages/core/CMakeLists.txt`, mirroring RN's `gradle/libs.versions.toml` |
| fast_float | `v8.0.0` | `RNL_FAST_FLOAT_VERSION`, same mirror |
| Boost (fallback only) | `1.83.0` | `RNL_BOOST_VERSION`, same mirror; used only when no system Boost is found |
| glog (fallback only) | `v0.7.1` | `RNL_GLOG_VERSION`; used only when no system glog is found |

The Hermes tag is derived, never hardcoded: `hermes-v${HERMES_VERSION_NAME}`. `sdks/.hermesversion` was removed
upstream and must not be used. The Hermes CMake library target is `hermesvm`, not `libhermes`.

Re-vendoring on a React Native bump: edit `tag` in `scripts/vendor.lock.json`, run `pnpm --filter @react-native-linux/core vendor`,
reconfigure. The script re-clones when the pin changes and is a no-op when it has not.

## Prerequisites

Boost and glog are looked up with `find_package` first and fetched from source only when the lookup fails, so a
machine without root can build without them. `double-conversion`, `fmt` and ICU have no fallback and must be present.

### Arch Linux

```bash
sudo pacman -S --needed base-devel git cmake ninja ccache python \
  boost boost-libs double-conversion fmt google-glog icu
```

`clang` and `llvm` are optional but recommended: Meta's own Linux C++ host (`react-native-fantom`) builds with
`CC=clang`, and Hermes' Linux CI does the same, so clang is the better-tested path for the Hermes and llvh sources.

### Without root

`cmake` and `ninja` can come from a user-space version manager; prefix every CMake invocation, for example
`mise exec cmake@latest ninja@latest -- cmake --preset dev`. Boost and glog are then fetched from source. The Boost
fallback downloads the classic 145 MB source archive into the preset's `build/<preset>/_deps`, once per build
directory, so installing system Boost is still the faster path when it is available.

### Ubuntu (CI)

```bash
sudo apt-get install -y build-essential git cmake ninja-build ccache python3 \
  libboost-dev libdouble-conversion-dev libfmt-dev libgoogle-glog-dev libicu-dev
```

Two version caveats on Ubuntu 24.04, both understood and accepted:

- `libfmt-dev` is 10.x while React Native pins fmt 12.1.0. folly's subset only needs fmt >= 8.
- `cmake` is 3.28.3, exactly the minimum this build declares.

`fast_float` has no Ubuntu package, so it is fetched at React Native's pinned version rather than split across
two acquisition paths.

## Commands

```bash
pnpm --filter @react-native-linux/core vendor      # node scripts/vendor-react-native.ts
pnpm --filter @react-native-linux/core configure   # cmake -S <repo root> --preset dev
pnpm --filter @react-native-linux/core build       # cmake --build build/dev
pnpm --filter @react-native-linux/core run:hello   # build/dev/bin/hello_react
```

The same sequence without pnpm, from the repository root:

```bash
node scripts/vendor-react-native.ts
cmake --preset dev
cmake --build build/dev
./build/dev/bin/hello_react
./build/dev/bin/hello_react packages/core/test-bundles/hello.js
```

Without an argument the expected output is `react-native-linux: hermes alive`. With
`packages/core/test-bundles/hello.js` the bundle prints its own evaluation, microtask and timer lines and the
process exits 0. `packages/core/test-bundles/throws.js` is the error fixture: it prints a `[js-error] fatal` block
with a parsed stack to stderr and exits 1.

When `cmake` and `ninja` are not on `PATH`, wrap the two CMake steps:

```bash
mise exec cmake@latest ninja@latest -- cmake --preset dev
mise exec cmake@latest ninja@latest -- cmake --build build/dev
```

Presets: `dev` (Debug), `release` (Release), `asan` (ASan + UBSan), `tsan` (TSan). The sanitizer presets also flip
Hermes' own `HERMES_ENABLE_*_SANITIZER` options, because Hermes disables its Boost.Context fibers under ASan and
cannot infer that from `CMAKE_CXX_FLAGS`. `CMAKE_EXPORT_COMPILE_COMMANDS` is on in every preset, so clangd works
from a fresh configure.

## Dependencies, and how they differ from Meta's

`private/react-native-fantom/tester/CMakeLists.txt` is the canonical template for a C++ React Native host. It pulls
`boost`, `glog`, `double-conversion`, `fast_float`, `fmt`, `folly`, `gflags`, `nlohmann_json` and OpenSSL, all
downloaded into an NDK staging directory by Gradle's `prepareNative3pDependencies`. This build needs a subset,
because it stops at the `ReactInstance` rather than the full fantom host:

| Dependency | Here | Why |
| --- | --- | --- |
| boost | system headers, else classic source archive at 1.83.0 | React Native's own boost target is include-only and its Gradle task extracts only `boost/**/*.hpp`, so no compiled Boost library is ever linked. Hermes vendors its own Boost.Context separately. |
| glog | system, else built from source at 0.7.1 | RN's 0.3.5 pin has no standalone CMake build — Gradle generates its `config.h` and substitutes tokens into the `.h.in` headers. glog 0.7.1's own CMake defines the target name ReactCommon links, `glog`, so it drops straight in with `WITH_GFLAGS=OFF`, `WITH_GTEST=OFF`, `WITH_UNWIND=none`. |
| double-conversion, fmt | system | Distribution versions satisfy folly and ReactCommon. |
| fast_float | FetchContent at RN's pin | No Ubuntu package. |
| folly | FetchContent at RN's pin, RN's subset source list | ReactCommon compiles every TU with `-DFOLLY_NO_CONFIG=1`, which is ABI-incompatible with a distribution folly built against `folly-config.h`. |
| gflags | not used | Only fantom's own CLI needs it. |
| nlohmann_json, OpenSSL | not used | Only `ReactCxxPlatform`'s HTTP/WebSocket clients need them, and only `react/threading` is linked from that tree. They arrive with the Metro dev server and the inspector. |

Two flags fantom sets are deliberately dropped, because fantom targets the NDK and libc++ while this targets glibc
and libstdc++: `FOLLY_USE_LIBCPP` (folly would include libc++'s `<__config>`) and `FOLLY_HAVE_XSI_STRERROR_R`
(glibc's `strerror_r` is the GNU variant and returns `char*`).

## Known hazards

1. **`HERMES_ENABLE_TOOLS=ON` is mandatory** even though no CLI is shipped. Hermes precompiles `InternalBytecode`
   with its own `hermesc` during the build and Ninja fails with a missing-rule error when tools are off.
2. **libstdc++ include hygiene.** Hermes' CDP and llvh sources assume `<string>`, `<memory>`, `<vector>`,
   `<cstdint>`, `<cstring>` arrive transitively, which stopped being true with libstdc++ 13. The build force-includes
   them for the duration of Hermes' `add_subdirectory` and restores `CMAKE_CXX_FLAGS` afterwards. Upstream has no
   Linux CI job with `HERMES_ENABLE_DEBUGGER=ON`, so this keeps recurring; adding one is the cheapest useful
   upstream contribution this project can make.
3. **gtest collides with Hermes' llvh.** Hermes' bundled llvh ships its own googletest. `HERMES_ENABLE_TEST_SUITE`
   is forced off here, but wiring a GoogleTest target for `packages/core` in the same configure as Hermes still has
   to solve the collision — either by keeping the test binary in a Hermes-free configure, or by fetching googletest
   before Hermes and letting the existing targets win.
4. **`jsi` is defined twice upstream.** Hermes builds a `jsi` target and so does ReactCommon. `JSI_DIR` points
   Hermes at React Native's `ReactCommon/jsi`, so exactly one `jsi` target exists; the build then appends
   `JSIDynamic.cpp` and `jsilib-posix.cpp`, which Hermes' own CMakeLists does not compile but ReactCommon needs.
   `HERMES_BUILD_SHARED_JSI=ON` keeps that single `jsi` a shared library so `hermesvm` and the executable resolve
   the same definition.
5. **ReactCommon compiles with `-Werror`.** Every ReactCommon target here gets `-Wno-error` appended, rather than
   carrying a patch for each distribution compiler that introduces a new warning.
6. **CMake 4 and old `cmake_minimum_required`.** Hermes' vendored Boost.Context declares `VERSION 3.5...3.16`.
   If a future CMake refuses it, configure with `-DCMAKE_POLICY_VERSION_MINIMUM=3.5`.

## First-build fix ledger (2026-09-01, clang 22, Arch)

1. ReactCommon subdirectory CMakeLists assume the parent includes `cmake-utils/react-native-flags.cmake` and `cmake-utils/internal/react-native-platform-selector.cmake`; both are now included after `REACT_COMMON_DIR` is set.
2. folly `Demangle.cpp` fails against binutils >= 2.44 `demangle.h` (`demangle_callbackref` removed); the FetchContent `PATCH_COMMAND` forces folly's supported no-libiberty path.
3. `jsinspector-modern/network/HttpUtils.h` misses `<cstdint>` under libstdc++ 16; a directory-wide `-include cstdint` covers the class of problem.
4. `HERMES_ENABLE_RTTI=ON` is required: llvh otherwise builds without RTTI and every RTTI-enabled consumer of `libhermesvm.so` fails on missing `llvh::cl::*` typeinfo.
5. `JSIDynamic.cpp`/`jsilib-posix.cpp` must not be appended to Hermes' `jsi` target — its `-fvisibility=hidden` strips their unannotated symbols; they live in the `rnl_jsidynamic` static library instead.
6. `hello_react` links `atomic`: `std::atomic<std::optional<double>>` in `ReactNativeFeatureFlagsAccessor` needs `__atomic_load_16`/`__atomic_store_16` on x86-64 glibc.
