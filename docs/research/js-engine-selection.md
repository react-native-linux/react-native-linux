# JS Engine Selection for react-native-linux

> Research report, 2026-09-01. Verification legend: [V] verified in primary source, [I] inferred, [?] unconfirmed.

# JS Engine Options for a Hypothetical `react-native-linux` (RN 0.76 → 0.87)

Legend: **[V]** = verified in a primary source I read. **[I]** = inferred. **[?]** = could not confirm.

Note: my WebSearch budget was exhausted partway through; most findings below come from reading repo files directly via `gh api`, which is the stronger evidence anyway. Where I flag **[?]** it is usually because a search would have been the only route.

---

## 0) Headline answers

1. **Hermes builds and fully runs on desktop Linux (x86-64 and arm64), and is CI-tested there as a runtime — not just as a compiler host.** This is the single most important correction to the common assumption.
2. **But Meta ships no prebuilt Hermes *runtime* for Linux.** Only `hermesc` (the compiler) is published for Linux. A `react-native-linux` must build the Hermes VM from source.
3. **The Hermes JIT is ARM64-only.** On x86-64 Linux desktop you get the interpreter (plus AOT bytecode). This is a real, quantifiable disadvantage on the most common desktop target.
4. **RN's bridgeless core is genuinely engine-agnostic in 0.87**, with a documented C extension point (`JSRuntimeFactoryRef` / `jsrt_create_*_factory`) and a non-Hermes DevTools fallback (`FallbackRuntimeTargetDelegate`). Hermes is not hardcoded.
5. **Prior art already exists and already chose Hermes**: `lucid-softworks/react-native-linux` (GTK4, Fabric-only, active Aug 2026).

---

## 1) Hermes on desktop Linux

### 1a. Is Linux CI-tested as a runtime? — YES **[V]**

`facebook/hermes` → `.github/workflows/build.yml` contains a `test-linux-test262` job:

```yaml
test-linux-test262:
  strategy:
    matrix:
      os: [4-core-ubuntu, ubuntu-24.04-arm]
  ...
  run: |
    cmake -S hermes -GNinja -B build -DCMAKE_BUILD_TYPE=Debug ${{ matrix.cmake_flags }}
    cmake --build build --target check-hermes all
    ...
    python3 hermes/utils/test_runner.py --test-intl test262/test -b build_intl/bin
```

This builds the **whole VM** and runs `check-hermes` (the lit suite) **and test262**, on both x86-64 (`4-core-ubuntu`) and arm64 (`ubuntu-24.04-arm`), with and without `-DHERMES_ENABLE_INTL=ON`. There is also a `test-linux-armv7` job running in an `arm32v7/ubuntu:noble` container.

- File: `.github/workflows/build.yml` — https://github.com/facebook/hermes/blob/static_h/.github/workflows/build.yml
- `4-core-ubuntu` is x86-64 by GitHub's larger-runner naming convention (ARM labels carry `-arm`) **[I]**

`doc/BuildingAndRunning.md` documents Linux as a first-class build target, including per-distro dependency lists for **Ubuntu and Arch Linux** — https://github.com/facebook/hermes/blob/static_h/doc/BuildingAndRunning.md

### 1b. Build host vs. runtime target — the critical distinction **[V]**

Both are true, but they are served by *different* CI workflows and *different* artifacts.

| Role | Workflow | Artifact | Published? |
|---|---|---|---|
| Linux as **build host** (hermesc only) | `.github/workflows/build-hermesc-linux.yml` | `hermesc` binary | **Yes** — npm |
| Linux as **runtime target** (full VM) | `.github/workflows/build.yml` job `test-linux-test262` | none | **No** |

`build-hermesc-linux.yml` is `on: workflow_call` only, and its build is deliberately compiler-only:

```yaml
cmake -S . -B build -DHERMES_STATIC_LINK=ON -DCMAKE_BUILD_TYPE=Release -DHERMES_ENABLE_TEST_SUITE=OFF ...
cmake --build build --target hermesc -j 4
cp build/bin/hermesc /tmp/hermes/linux64-bin/.
```

It is called from `.github/workflows/rn-build-hermes.yml` ("RN Build Static Hermes"), whose job list makes the asymmetry explicit **[V]**:

- `build_hermesc_apple`, `build_apple_slices_hermes`, `build_hermes_macos` → **Apple runtime** (xcframework)
- `build_android` → `./gradlew publishAndroidOnlyToMavenTempLocal` → **Android runtime** (Maven AAR)
- `build_hermesc_linux`, `build_hermesc_windows` → **compiler only**

**Verification of the published artifact**, `hermes-compiler@260318099.0.1` from npm:

```
package/hermesc/linux64-bin/hermesc      <- ELF 64-bit LSB executable, x86-64, statically linked, stripped
package/hermesc/osx-bin/hermesc
package/hermesc/win64-bin/hermesc.exe
```

Note: `linux64-bin` is **x86-64 only — there is no linux-arm64 hermesc** in the npm package **[V]**. A Linux/arm64 dev machine would have to build `hermesc` itself.

Also relevant: `facebook/hermes` **GitHub Releases stopped at v0.13.0 (Aug 2024)** **[V]**. Modern Hermes ships exclusively through the RN pipeline (Maven / CocoaPods / npm). The last Linux release asset was `hermes-cli-linux.tar.gz` on v0.13.0 — long obsolete.

The `npm/hermes-engine-cli/package.json` manifest does list full Linux CLI tools (`linux64-bin/hermes`, `hdb`, `hbcdump`, `hermesc`) — but that package is not being published to npm today **[V]** / **[I]**.

**Bottom line: a `react-native-linux` must build the Hermes VM from source. There is no prebuilt-runtime path.**

### 1c. Hermes V1 vs. Static Hermes **[V]**

They are **related but not the same thing**, and this is officially stated.

From the RN 0.82 blog post (https://reactnative.dev/blog/2025/10/08/react-native-0.82):

> "Hermes V1 does not yet contain JS-to-native compilation (previously known as "Static Hermes") or the JIT compilation that was presented during React Native EU 2023. We are still testing these features, and will share more as we make progress."

The accurate framing: **Hermes V1 is the shipping product built from the `static_h` *branch*, with the Static Hermes AOT-to-native and JIT features not enabled by default.**

Supporting evidence:
- `facebook/hermes` **default branch is `static_h`**, not `main` **[V]** — the Static Hermes line has become the mainline.
- Release branches are named `250829098.0.0-stable`, `260318099.0.0-stable` — matching the date-based Hermes V1 version scheme **[V]**.
- The AOT-to-native compiler still exists as `tools/shermes`, and `doc/TypedLanguage.md` opens with: *"The typed language implementation is in progress and considered unstable."* **[V]**
- `doc/blog/2025-11-02-hermes-compilation-runtime-modes.md` lays out all four modes (AOT→bytecode, AOT→native, interpreter, lazy compilation, baseline JIT) as mixable in one runtime **[V]**.

**Timeline** **[V]**: 0.82 = experimental opt-in (`hermesV1Enabled=true` / `RCT_HERMES_V1_ENABLED=1`); 0.84 = default on iOS + Android (https://reactnative.dev/blog/2026/02/11/react-native-0.84).

**Does V1 change platform support? No — if anything it improves the Linux story** **[I, well-supported]**. All the Linux CI jobs above run on `static_h`. Nothing in the V1 work is platform-narrowing. The RN 0.87.0 release notes reference Hermes V1 `250829098.0.16` **[V]**, and `packages/react-native/sdks/hermes-engine/version.properties` on main pins `HERMES_VERSION_NAME=260318099.0.1` **[V]**.

**Language-feature note that matters a lot for Linux [V]**: the prior-art project is stuck on Hermes 0.12 and reports *"Hermes rejects raw `async` / `await` syntax; the bundler lowers it to generators."* Hermes V1 (`doc/blog/2026-06-05-new-hermes-stable-release.md`) adds `for await...of`, async generators, `FinalizationRegistry`, Iterator Helpers, Set ops, `Object.groupBy`, `TextDecoder`, Unicode 17, >4GB heaps, and 2.7–3.4x faster JSON. **Targeting V1 rather than old Hermes removes the single ugliest constraint the existing Linux prototype hit.**

### 1d. How RN consumes Hermes **[V]**

- iOS/macOS: `packages/react-native/sdks/hermes-engine/hermes-engine.podspec` — vendored `hermesvm.xcframework` / `hermesvm.framework`, downloaded prebuilt. `spec.platforms = { :osx => "10.13", :ios => "15.1", :visionos => "1.0", :tvos => "15.1" }` — **no Linux, obviously**.
- Android: `packages/react-native/ReactAndroid/hermes-engine/build.gradle.kts` — downloads a Hermes source tarball, builds `--target hermesvm`, and packages headers as an **NDK prefab** AAR under Maven group `com.facebook.hermes`.
- Version pin: **`sdks/.hermesversion` no longer exists**; it was replaced by `sdks/hermes-engine/version.properties` **[V]**. Anything relying on `.hermesversion` (including the prior-art `FetchHermes.cmake`) is now stale.
- Note the **library target rename**: the public JSI library is now `hermesvm`, previously `libhermes` **[V]** — confirmed both in the Android gradle (`--target hermesvm`) and by the prior-art CMake shim that probes `hermesvm` → `libhermes` → `hermes` in order.

### 1e. JIT — ARM64 only **[V]**

`lib/VM/JIT/` contains exactly one backend directory: **`arm64`**. The gate is in `include/hermes/VM/JIT/Config.h`:

```c
#if !defined(HERMESVM_JIT) && (defined(__aarch64__) || defined(_M_ARM64)) && \
    (!defined(HERMESVM_COMPRESSED_POINTERS) ||                               \
     defined(HERMESVM_CONTIGUOUS_HEAP))
#define HERMESVM_JIT 1
#else
#define HERMESVM_JIT 0
#endif
```

- https://github.com/facebook/hermes/blob/static_h/include/hermes/VM/JIT/Config.h
- `CMakeLists.txt` line 310–313: `HERMESVM_ALLOW_JIT` = 0 (off) / 1 (auto) / 2 (force). Default is **0**.
- CI confirms the arch split: the `-DHERMESVM_ALLOW_JIT=2 -Xjit=force` matrix rows exist **only** for `macos-15` and `ubuntu-24.04-arm`. There is no x86-64 JIT row.

**Consequence for desktop Linux: x86-64 gets interpreter + AOT bytecode only; arm64 Linux gets the baseline JIT.** That is backwards from what most desktop-Linux users would want.

One Linux-friendly touch **[V]**: `Config.h` also defines `HERMES_ENABLE_PERF_PROF` when JIT is on, non-mobile, and `__linux__`/`__ANDROID__` — with `lib/VM/JIT/PerfJitDump.cpp` emitting `perf` jitdump. Meta clearly runs Hermes on Linux for profiling (see also `.github/workflows/perf-build-rpi.yml`, which builds release ARM binaries on `arm64v8/debian:trixie` for Raspberry Pi perf testing).

### 1f. Debugger / CDP on Linux **[V]**

The CDP implementation is **pure, portable C++ inside Hermes**: `API/hermes/cdp/` (`CDPAgent`, `DebuggerDomainAgent`, `RuntimeDomainAgent`, `HeapProfilerDomainAgent`, `ProfilerDomainAgent`, `MessageTypes`…), plus `API/hermes/AsyncDebuggerAPI.h`, `DebuggerAPI.h`, and the `tools/hcdp` CLI. Nothing there is Apple/Android-specific. So **CDP on Linux is achievable.**

**But there is a real, verified papercut**: `HERMES_ENABLE_DEBUGGER` defaults to **OFF** (`CMakeLists.txt:242`), and the Linux CI jobs never turn it on. So **the CDP/debugger code path is not continuously compiled on Linux upstream.** The prior-art project hit exactly this and had to work around it:

> "Hermes' CDP sources (e.g. `API/hermes/cdp/DomainState.cpp`) rely on `std::string` / `std::vector` / `std::memory` being pulled in transitively by other headers — true for older libstdc++ but not for the libstdc++ 13 that ships in Ubuntu 24.04. The result is 'incomplete type `std::__cxx11::basic_string<char>`' errors."
> — `vnext/cmake/FetchHermes.cmake`, https://github.com/lucid-softworks/react-native-linux/blob/main/vnext/cmake/FetchHermes.cmake

**Expect to carry small include-hygiene patches, and expect them to keep recurring** until someone adds a Linux `HERMES_ENABLE_DEBUGGER=ON` CI job upstream. That is a cheap, high-value upstream contribution for a Linux platform to make.

---

## 2) Why `microsoft/hermes-windows` exists

Repo: https://github.com/microsoft/hermes-windows — active (last push 2026-08-20), 89 stars.

**It tracks the same upstream line you'd use.** `sync-config.json` **[V]**:
```json
{ "repo": "https://github.com/facebook/hermes", "branch": "static_h",
  "commit": "6abc4a3efd82ad37eb4a4e9e79e6e9500e28aa39", "lastSync": "2026-03-31T..." }
```
Its `AGENTS.md` says plainly: *"This is the **hermes-windows** fork of facebook/hermes, adapted for Windows... The fork syncs with the upstream `static_h` branch."*

The README is a near-verbatim copy of upstream (the divergence is **not** in the docs). The real divergence is in `API/`:

| `facebook/hermes` `API/` | `microsoft/hermes-windows` `API/` |
|---|---|
| `hermes`, `hermes_abi`, `hermes_sandbox`, `jsi`, `napi` | `hermes`, `hermes_abi`, `hermes_sandbox`, `jsi`, **`hermes_node_api`**, **`hermes_node_api_jsi`**, **`hermes_shared`** |

**Yes — its existence does imply upstream desktop gaps.** Specifically **[V]** for the contents, **[I]** for the "why":

1. **ABI-stable DLL packaging.** `API/hermes_shared/` contains `hermes_api.h`/`.cpp`, `js_runtime_api.h`, and `version.rc.in` (a Windows DLL version resource). Upstream's own equivalent, `API/hermes_abi/README.md`, is explicit that this is unfinished: *"This directory contains ongoing work to develop a stable C-based ABI for Hermes... **It is a work in progress and is not supported for general use.**"* Desktop wants engine-updatable-independently-of-app; mobile bundles everything, so upstream never needed it.
2. **Node-API surface.** `API/hermes_node_api/` implements Node-API on Hermes. Complemented by the separate `microsoft/node-api-jsi` repo, whose README states the goal cleanly: *"The JSI implementation on top of Node-API enables use of JS engines that implement Node-API with React Native."* That is an ABI-safe C boundary instead of the C++ `jsi::Runtime` vtable.
3. **Bytecode caching (`ScriptStore.h` in `API/hermes_shared/`).** On mobile, bytecode is produced AOT at build time. On desktop, where bundles arrive at runtime, you need a runtime script cache. Same file appears in `microsoft/v8-jsi` (`src/public/ScriptStore.h`) — clearly a shared desktop-shaped need. **[I]**
4. **Toolchain/architecture work.** From `AGENTS.md`: Clang-from-VS2026 builds for x64/x86/arm64/**arm64ec**, `softintrin.lib` linkage, ARM64EC macro quirks, `HermesWindows.cmake`, BinSkim security validation, and a native-ARM64 agent pool. Also a Windows-specific `BOOST_CONTEXT_IMPLEMENTATION=winfib` flag (visible even in upstream's `build.yml` Windows matrix).
5. **Inspector** — `API/hermes_shared/inspector/`.

**Nothing here is V8-style API** — that's a different thing (see §3). And note that Windows *is* upstream-CI-tested by Meta (`test-windows-test262` on `windows-2022` and `windows-11-arm`), so the fork is not about basic buildability — **it is about productization: stable ABI, DLL versioning, Node-API, script caching, security compliance.** **A Linux platform would face the same class of gaps.** **[I]**

---

## 3) What engine does `react-native-windows` actually use in 2026?

**Hermes by default; V8 as a supported opt-in; Chakra completely gone.** **[V]**

Primary source — `vnext/PropertySheets/JSEngine.props` (https://github.com/microsoft/react-native-windows/blob/main/vnext/PropertySheets/JSEngine.props):

```xml
<!-- Enabling this will (1) Include hermes glues in the Microsoft.ReactNative binaries AND (2) Make hermes the default engine -->
<UseHermes Condition="'$(UseHermes)' == ''">true</UseHermes>
<HermesVersion Condition="'$(HermesVersion)' == ''">0.0.0-2608.12001-35d34796</HermesVersion>
<HermesPackageName Condition="'$(HermesPackageName)' == ''">Microsoft.JavaScript.Hermes</HermesPackageName>
...
<UseV8 Condition="'$(UseV8)' == ''">false</UseV8>
<V8Version Condition="'$(V8Version)' == ''">0.71.8</V8Version>
<V8PackageName>ReactNative.V8Jsi.Windows</V8PackageName>
```

Corresponding source files **[V]**:
- `vnext/Shared/HermesRuntimeHolder.{h,cpp}`
- `vnext/Shared/V8JSIRuntimeHolder.{h,cpp}`
- `vnext/Shared/Hermes/HermesRuntimeTargetDelegate.{h,cpp}`, `HermesRuntimeAgentDelegate.{h,cpp}`

**Chakra: I grepped the entire `react-native-windows` main tree (4,697 paths) for `chakra` — zero hits.** ChakraCore is fully removed. **[V]**

**`microsoft/v8-jsi`** (https://github.com/microsoft/v8-jsi) — active (2026-08-25), 189 stars, "React Native V8 JSI adapter". Notably it is **mid-migration**: from a `depot_tools`/`gclient` V8 checkout producing `ReactNative.V8Jsi.Windows`, to a **vendored Node.js build** (`deps/nodejs/`) producing a new `Microsoft.JavaScript.V8` NuGet. `config.json` pins `nodejs_version: 24.15.1`, `v8jsi_version: 24.1.2`.

Also worth noting for RNW's Hermes: `HermesNoLink` defaults true in Release, relying on "HermesShim" — i.e. RNW loads Hermes through a **shim/dynamic boundary** rather than static linkage. That is the DLL-ABI concern from §2 showing up in practice. **[V]** file, **[I]** interpretation.

---

## 4) Alternative JSI engines viable on Linux

### 4a. The JSI contract itself **[V]**

- **Location: `packages/react-native/ReactCommon/jsi/jsi/jsi.h`** — https://github.com/facebook/react-native/blob/v0.87.1/packages/react-native/ReactCommon/jsi/jsi/jsi.h
- **Size: 2,266 lines; ~120 `= 0;` pure-virtual declarations.** That is the raw cost of a new engine binding. It is a *large* surface.
- Sibling files: `jsi-inl.h`, `decorator.h`, `instrumentation.h`, `threadsafe.h`, `JSIDynamic.{h,cpp}`, `jsilib-posix.cpp` (**a POSIX impl already exists** — good for Linux), `jsilib-windows.cpp`.
- **`hermes-interfaces.h` now lives inside the generic jsi folder** — including `IEventLoopControl` (`scheduleTask`, `registerTaskQueueSource`, `unregisterTaskQueueSource`). Its presence in `jsi/jsi/` rather than a Hermes-only folder suggests upstream is generalizing engine↔host event-loop integration. **[V]** location, **[I]** intent.

**Mandatory microtask APIs** (pure virtual — a new engine *must* implement them) **[V]**:
```cpp
virtual void queueMicrotask(const jsi::Function& callback) = 0;      // jsi.h:388
virtual bool drainMicrotasks(int maxMicrotasksHint = -1) = 0;        // jsi.h:417
```
The doc comment explicitly ties `drainMicrotasks` to the WHATWG "perform a microtask checkpoint".

**Stability**: there is **no version/ABI guarantee in the header**. The only stability mechanism is the `JSI_UNSTABLE` macro **[V]**:
> "`JSI_UNSTABLE` gates features that will be released with a Hermes version in the future. Until released, these features may be subject to change. After release, these features will be moved out of `JSI_UNSTABLE` and become frozen."

Note the phrasing — stability is defined **relative to Hermes releases**. And `microsoft/v8-jsi`'s README confirms the practical pain **[V]**:
> "Until the JSI headers find a more suitable home, they're currently duplicated between the various repos. Code in `jsi\jsi` should be synchronized with the matching version of JSI from react-native."

**Treat JSI as source-compatible-ish but ABI-unstable, and expect to re-sync headers every RN release.**

**Conformance suite exists and is reusable** **[V]**: `ReactCommon/jsi/jsi/test/testlib.{h,cpp}` provides `JSITestBase` parameterized over a `RuntimeFactory = std::function<std::shared_ptr<Runtime>()>`. **Any new engine can plug straight into RN's own JSI test suite.** This is the highest-leverage thing to wire up first for a new engine.

### 4b. JavaScriptCore — **not removed from core, but effectively dead** **[V]**

- `packages/react-native/ReactCommon/jsc/JSCRuntime.{h,cpp}` **still exists in v0.87.1 and on `main`**. But I found **zero references to it from any podspec, CMakeLists, or gradle file** in the tree. It is vestigial dead code, not a supported path. **[V]** for existence + absence of build refs; **[I]** for "dead".
- Also still present: `ReactCommon/react/runtime/platform/ios/ReactCommon/RCTJscInstance.{h,mm}` (iOS-only).
- **Supported path is `@react-native-community/javascriptcore`** (npm `0.2.0`; repo https://github.com/react-native-community/javascriptcore, last push 2026-02-12, 72 stars). Per its README: *"This library only supports React Native 0.79 and above with new architecture enabled."*
- Removal was governed by **Lean Core JSC RFC 0836** — https://github.com/react-native-community/discussions-and-proposals/blob/main/proposals/0836-lean-core-jsc.md. Its plan: 0.78 drop `jsc-android`; 0.79 introduce community package + warnings; **0.81 or 0.82 "completely remove JSC from the core"**. **That removal has evidently slipped — the code is still there in 0.87.** Worth flagging as a discrepancy between plan and reality.
- **Platform support in the community package is `android/` + `apple/` only** — no Linux. **However `common/JSCRuntime.cpp` is platform-neutral** and would be the porting target. **[V]**
- **JSCOnly / WPE WebKit on Linux**: I could **not** verify current practicality. **[?]** No primary source read. Directionally: you'd be linking a GTK/WPE-flavored JavaScriptCore, which is a heavy dependency with its own GLib entanglement, and you'd own the port yourself. **Not recommended.**

### 4c. V8

- **`Kudo/react-native-v8`** — **effectively dormant. Last push 2024-08-20** (961 stars). README says *"Opt-in V8 runtime for React Native **Android**"*, min RN 0.71.2. **[V]** **Predates bridgeless/New Arch maturity, Android-only. Not viable.**
- **`microsoft/v8-jsi`** — **active and the far better bet.** Last push 2026-08-25. **And it has a real Linux target** **[V]**:
  - `localbuild.ps1`: `[ValidateSet('win32', 'android', 'linux', 'mac')] $AppPlatform`
  - `scripts/fetch_code.ps1`: `if ($AppPlatform -eq "linux") { ... v8/build/install-build-deps.sh ... }`
  - `scripts/build.ps1`: `$buildingWindows = !"android linux mac".contains($AppPlatform)`
  - `src/BUILD.gn`: `target("shared_library", "v8jsi")` with `v8jsi_enable_inspector` and `v8jsi_enable_node_api` declared args.
  - **Caveats**: the build is PowerShell-driven; `src/BUILD.gn` currently lists `jsi/jsilib-windows.cpp` and not the POSIX variant, so the Linux path is plausibly bit-rotted. It is not in the shipped NuGets, and I saw no Linux CI. **[V]** for the files, **[I]** for bit-rot.
  - **Upside**: JIT on x86-64 (which Hermes cannot give you), a mature CDP inspector, and Node-API. **Downside**: enormous build (`depot_tools`, ~15GB), huge binary, slow startup, and you inherit `LICENSE.v8.md` / Chromium build tooling.

### 4d. QuickJS — nothing production-grade **[V]**

Targeted GitHub search for `quickjs jsi`:
- `tudorms/QuickJSI` — **"(PROTOTYPE) QuickJS based JSI implementation"**, 9 stars, **last push 2020-08-04**. Dead.
- `ByteKen/react-native-quickjs-sandbox` (3 stars) — a *sandbox for untrusted JS inside* RN, **not** an RN runtime engine.
- `wxiaoguang/quickjs-jsi` — 1 star, 2025.
- npm `react-native-quickjs@0.0.2` — abandoned version number.
- `echosoar/jsi` — unrelated (a Rust JS interpreter that happens to be named JSI).

**No viable QuickJS JSI binding exists.** Note QuickJS is also interpreter-only, so it would be strictly worse than Hermes for RN (no bytecode-AOT ecosystem, no CDP, no RN integration) while costing you a full ~120-method JSI implementation.

**Re: "a generic jsi test/reference runtime in RN's OSS tree"** — there is **no generic reference *engine*** **[V]**. What exists is the *test harness* (`jsi/test/testlib.h`, §4a) and, in `jsinspector-modern/tests/engines/`, only `JsiIntegrationTestHermesEngineAdapter.{h,cpp}` — **Hermes is the sole in-tree engine adapter for inspector tests.**

### 4e. Anything else — no **[V]**

GitHub repo searches for `spidermonkey jsi`, `boa jsi react-native`, and `llrt jsi` returned **zero results each**. No SpiderMonkey, Boa, or LLRT JSI binding exists.

---

## 5) Bridgeless / `ReactInstance` C++ requirements

### 5a. What `ReactInstance` actually requires **[V]**

`packages/react-native/ReactCommon/react/runtime/ReactInstance.h`:
```cpp
ReactInstance(
    std::unique_ptr<JSRuntime> runtime,
    std::shared_ptr<MessageQueueThread> jsMessageQueueThread,
    std::shared_ptr<TimerManager> timerManager,
    JsErrorHandler::OnJsError onJsError,
    jsinspector_modern::HostTarget *parentInspectorTarget = nullptr);
```

**It takes a `JSRuntime`, not a `HermesRuntime`.** Hermes appears nowhere in this header.

### 5b. The engine contract — `JSRuntimeFactory.h` **[V]**

**Path moved in recent RN**: it is now at
`packages/react-native/ReactCommon/jsitooling/react/runtime/JSRuntimeFactory.h`
(not `react/runtime/`, which is where older docs point). https://github.com/facebook/react-native/blob/v0.87.1/packages/react-native/ReactCommon/jsitooling/react/runtime/JSRuntimeFactory.h

```cpp
class JSRuntime {
 public:
  virtual jsi::Runtime &getRuntime() noexcept = 0;
  virtual ~JSRuntime() = default;
  virtual jsinspector_modern::RuntimeTargetDelegate &getRuntimeTargetDelegate();
  virtual void unstable_initializeOnJsThread() {}
 private:
  std::optional<jsinspector_modern::FallbackRuntimeTargetDelegate> runtimeTargetDelegate_;
};

class JSRuntimeFactory {
 public:
  virtual std::unique_ptr<JSRuntime> createJSRuntime(
      std::shared_ptr<MessageQueueThread> msgQueueThread) noexcept = 0;
  virtual ~JSRuntimeFactory() = default;
};

class JSIRuntimeHolder : public JSRuntime { /* wraps a unique_ptr<jsi::Runtime> */ };
```

**The minimum viable new engine is tiny**: subclass `JSRuntimeFactory`, return a `JSIRuntimeHolder` wrapping your `jsi::Runtime`. `getRuntimeTargetDelegate()` and `unstable_initializeOnJsThread()` both have defaults. **All the real work is the ~120 JSI methods, not the RN integration.**

### 5c. **Yes, there is a non-Hermes fallback path, and it is first-class in 0.87** **[V]**

`packages/react-native/ReactCommon/jsinspector-modern/FallbackRuntimeTargetDelegate.h`:
> "A `RuntimeTargetDelegate` that **stubs out debugging functionality for a JavaScript runtime that does not natively support debugging.**"

```cpp
explicit FallbackRuntimeTargetDelegate(std::string engineDescription);
void addConsoleMessage(jsi::Runtime&, ConsoleMessage) override;
bool supportsConsole() const override;
std::unique_ptr<StackTrace> captureStackTrace(jsi::Runtime&, size_t framesToSkip) override;
void enableSamplingProfiler() override; /* ... */
```

Crucially, `JSRuntime` **default-constructs this for you** via the private `runtimeTargetDelegate_` member. So a new engine gets **console + basic DevTools for free**, with no CDP implementation, and can later override `getRuntimeTargetDelegate()` to light up full debugging. There is a matching `FallbackRuntimeAgentDelegate.{h,cpp}`.

### 5d. Is Hermes required anywhere? **No — it is cleanly isolated** **[V]**

`ReactCommon/react/runtime/CMakeLists.txt` builds the `bridgeless` target from `file(GLOB bridgeless_SRC "*.cpp")` — **the top-level directory only**. Its link list is:
```
jserrorhandler, jsi, jsitooling, jsireact, react_utils, jsinspector,
react_featureflags, react_performance_timeline
```
**No Hermes.** `HermesInstance.{h,cpp}` sits in the `hermes/` **subdirectory** with its own CMakeLists and its own `React-RuntimeHermes.podspec`, separate from `React-RuntimeCore.podspec`. `HermesInstance.h` is a 20-line file exposing a single static `createJSRuntime(...)`.

`react/runtime/platform/` contains **only `ios/`** — i.e. there is no crowded platform abstraction to satisfy.

### 5e. **There is a C API for exactly this use case** **[V]**

`packages/react-native/ReactCommon/jsitooling/react/runtime/JSRuntimeFactoryCAPI.h`:
```c
typedef void *JSRuntimeFactoryRef;
void js_runtime_factory_destroy(JSRuntimeFactoryRef factory);
```

And the community JSC package demonstrates the intended consumption pattern **[V]** (from its README):
```swift
override func createJSRuntimeFactory() -> JSRuntimeFactoryRef {
  jsrt_create_jsc_factory() // Use JavaScriptCore runtime
}
```

**This is the sanctioned, load-bearing extension point for a third-party engine in the New Architecture.** A `react-native-linux` should expose `jsrt_create_hermes_factory()` (or similar) through it.

### 5f. Other required pieces **[V]**

- `TimerManager` + `PlatformTimerRegistry.h` — the platform must supply timers (GLib main loop on GTK).
- `RuntimeScheduler` — `ReactCommon/react/renderer/runtimescheduler/`, with `RuntimeScheduler_Modern.cpp` / `_Legacy.cpp`, `RuntimeSchedulerCallInvoker`, `Task.cpp`. Docs at `runtimescheduler/__docs__/README.md`. This consumes a `RuntimeExecutor` and drives the event loop; it depends on `drainMicrotasks` semantics, not on Hermes.
- `MessageQueueThread`, `BufferedRuntimeExecutor`, `BridgelessNativeMethodCallInvoker`, `JsErrorHandler`.

---

## 6) Prior art you should read before deciding

**`lucid-softworks/react-native-linux`** — https://github.com/lucid-softworks/react-native-linux (31 stars, last push **2026-08-11**, active). **[V]**

> "React Native for Linux desktop. Renders to **GTK4**, runs JS on **Hermes**, uses the **new architecture (Fabric + TurboModules)** from day one. Inspired by and structurally modeled after `microsoft/react-native-windows`."

Key files:
- `vnext/cmake/FetchHermes.cmake` — **the single most useful file for this question**. Full `FetchContent` Hermes-from-source recipe, plus a `REACT_NATIVE_LINUX_USE_SYSTEM_HERMES` escape hatch.
- `vnext/src/jsi/HermesRuntimeFactory.{h,cpp}`, `JsThread.{h,cpp}`, `BundleLoader.{h,cpp}`, `RnLinuxBindings.{h,cpp}`, `TurboModuleRegistry.cpp`
- `.github/workflows/ci.yml` — **builds Hermes from source on `ubuntu-24.04` x86-64 in CI**, empirically proving desktop-Linux Hermes viability. Native deps: `libgtk-4-dev libsoup-3.0-dev libboost-all-dev libdouble-conversion-dev libevent-dev libgoogle-glog-dev libgflags-dev`.

**Three concrete Hermes-on-Linux hazards it documents** **[V]**:
1. `HERMES_ENABLE_TOOLS=ON` is **required even if you don't ship hermesc** — Hermes precompiles `InternalBytecode` at build time with its own hermesc, and Ninja fails with a missing-rule error otherwise.
2. The libstdc++ 13 / CDP missing-include problem (§1f), worked around by injecting `-include cstdint -include cstring -include string -include memory -include vector` around the Hermes `add_subdirectory`.
3. *"Hermes' bundled llvh gtest collides with upstream googletest; tests build locally via a Hermes-free configure"* — **your C++ unit tests and Hermes cannot easily coexist in one CMake configure.**

Its pin is stale in two ways worth correcting if you fork it: it pins Hermes to RN 0.81's commit and reads `sdks/.hermesversion`, **a file that no longer exists** (now `version.properties`, §1d).

---

## 7) Recommendation

**Use Hermes, built from source. It is not close.**

- It is the only engine with a **verified, CI-tested, upstream-maintained full-runtime story on both x86-64 and arm64 Linux**.
- It is the only one where the **RN integration already exists** (`HermesInstance`, `HermesRuntimeTargetDelegate`, full CDP in-engine).
- The prior art independently reached the same conclusion.
- Hermes V1 removes the language-feature objection that made old Hermes painful on desktop.

**Accept these costs, with eyes open:**
1. **Build from source. There is no prebuilt Linux runtime and there is no sign one is coming** — the RN release pipeline deliberately builds Linux `hermesc` only.
2. **No JIT on x86-64.** Interpreter + AOT bytecode only. If your workload is compute-heavy on desktop x86-64, benchmark this early — it is the strongest single argument for V8, and the only one I'd consider serious.
3. **You own the desktop-shaped gaps** that `hermes-windows` had to fork for: no stable C ABI (upstream's `hermes_abi` is self-declared WIP), no runtime script/bytecode cache, no `.so` versioning story.
4. **Budget for recurring small upstream patches**, especially around CDP and libstdc++ include hygiene, until a Linux `HERMES_ENABLE_DEBUGGER=ON` CI job exists.

**Suggested sequencing:**
1. Wire your `JSRuntimeFactory` subclass in through **`JSRuntimeFactoryCAPI.h`** (`jsrt_create_*_factory`), matching how `@react-native-community/javascriptcore` does it — this is the sanctioned seam and keeps you off private C++ ABI.
2. Take the **free `FallbackRuntimeTargetDelegate`** for console/DevTools on day one; defer real CDP.
3. Stand up **`jsi/test/testlib.h`** against your runtime immediately — it is RN's own conformance suite and costs almost nothing to adopt.
4. Pin Hermes via `version.properties` (**not** `.hermesversion`) and probe the `hermesvm` target name (**not** `libhermes`).
5. **Cheapest high-value upstream contribution**: a Linux CI job with `-DHERMES_ENABLE_DEBUGGER=ON`. It would permanently fix hazard #4 for everyone.

**Keep `microsoft/v8-jsi` as a documented secondary path** behind a build flag, mirroring RNW's `UseHermes`/`UseV8` split — it is the only credible route to JIT on x86-64 Linux, its Linux target already nominally exists, and RNW proves the two-engine pattern is maintainable.

---

## 8) What I could not confirm

- **[?] JSCOnly / WPE WebKit JavaScriptCore on Linux practicality** — no primary source read. WebSearch budget exhausted before I got to it. This is the one genuine gap in this report; if JSC-on-Linux matters to your decision, it needs a follow-up pass.
- **[?] Whether `microsoft/v8-jsi`'s Linux path currently compiles.** The scripts and GN target exist; `src/BUILD.gn` listing `jsilib-windows.cpp` (not the POSIX variant) is a bad sign. Needs an actual build attempt.
- **[?] Whether `hermes-engine-cli` (full Linux `hermes`/`hdb`/`hbcdump`) is still published anywhere.** The manifest exists in-repo; I found no current npm/release publication.
- **[?] Any official Meta statement on RN out-of-tree platform engine requirements.** I found the mechanism (C API, fallback delegate) but no policy doc.
- **[?] Exact RN version where `JSRuntimeFactory.h` moved to `jsitooling/`** — verified present there at v0.87.1, but I did not bisect.
- **Discrepancy flagged**: RFC 0836 planned JSC core removal in 0.81/0.82; `ReactCommon/jsc/` is **still present in 0.87.1 and on main**, though unreferenced by any build file. I could not find the decision record explaining the slip.
