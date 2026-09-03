# ADR-0002: Reach the ecosystem with a C++-first compatibility layer and fork-and-overlay packages

- Status: Accepted
- Date: 2026-09-03
- Deciders: Vitalii Yehorov
- Amends: ADR-0001's M4 paragraph ("Ecosystem porting programme, not a single milestone")
- Research: `docs/research/ecosystem-compatibility.md`
- Epic: [#96](https://github.com/react-native-linux/react-native-linux/issues/96)

## Context

ADR-0001 named M4 an ecosystem porting programme and stopped there. This ADR decides *how* the
ecosystem is reached, and records the number that shapes the answer.

**Of the fifty most-downloaded React Native libraries with native code, one runs on Linux today with
zero Linux-specific code.** That library is `@shopify/flash-list` 2.3.2, and it qualifies because
version 2 deleted its native side. Zero are portable C++ TurboModules or C++ Nitro modules that would
link unchanged. The survey behind that count was taken on 2026-09-02 over last-month npm download
figures and the libraries' own repositories; the method and the per-library classification are in
`docs/research/ecosystem-compatibility.md` §2.

The flagship is the same story from the other end: of its thirty direct native dependencies, nineteen
are Expo modules, which are a module system with no C++ author surface at all — `expo-modules-core`
is 238 Kotlin and 181 Swift files against 29 shared C++ files, and the `Module { … }` DSL exists only
in Kotlin and Swift. `react-native-gesture-handler` has no shared C++ whatsoever: 62 Kotlin files and
57 Objective-C files, a third implementation.

The second premise is about us, not about them. **This project is in its validation phase.** No
library maintainer has asked for Linux support, none has agreed to review a port, and none owes us
anything. Every integration is ours to build, carry and prove before it can be offered.

## Decision

Two halves, and they are not alternatives to each other.

### 1. A C++-first compatibility layer

Any library whose native side is already portable C++ must run on Linux with no Linux-specific code:
C++ TurboModules (codegen `modulesCxx`), Nitro modules declared `"all": {"language": "c++"}`, Fabric
components whose shadow nodes are C++, and JSI libraries. The layer is discovery (`platforms.linux`
plus a pure-C++ fallback rule), a CMake consumer that adds a discovered library's sources to our
build, generated registration, a documented Linux support contract for library authors, and a
conformance kit they can run without our machinery. Issues [#146](https://github.com/react-native-linux/react-native-linux/issues/146)–[#160](https://github.com/react-native-linux/react-native-linux/issues/160).

The honest accounting: the layer buys one library in fifty *today*. Its value is that it shrinks every
port and makes libraries written after it free. It is an investment, not a shortcut into the existing
ecosystem, and anyone reading this in a year should hold it to that claim rather than to a larger one.

### 2. Fork-and-overlay packages, one per library

Everything else — which is to say almost everything — is ours: `packages/<lib>` in this monorepo,
published as `@react-native-linux/<lib>`. The library's own source is vendored at a pinned tag, our
integration lives beside it as an ordered patch queue plus `linux/` native sources, and Metro aliases
the upstream package name to ours on `linux`. `pnpm upstream:bump <lib> <tag>` re-vendors and replays
the queue; `pnpm upstream:check` fails the build when the vendored tree or the queue drifts.
See `docs/ecosystem/upstream-streaming.md` and `docs/ecosystem/package-template.md`.

Community catch-up comes *after* a package works: the ask upstream carries the diff, the conformance
evidence and an explicit statement of who carries what, and it asks for a seam rather than for
adoption. See `docs/ecosystem/community-catch-up.md`.

### This reverses §6.5 of the survey, deliberately

`docs/research/ecosystem-compatibility.md` §6.5 rejected "fork every library into a
`@react-native-linux/*` scope" on the grounds that it converts twenty one-time ports into twenty
permanent merge obligations. That reasoning was sound under an assumption that no longer holds: it
assumed ports could be contributed upstream where a maintainer will take them, and carried as patch
sets only where they will not. In the validation phase **no maintainer takes anything yet**, so the
choice is not "upstream or fork" — it is "fork or nothing".

Two things also changed the cost of the rejected option:

- The merge obligation is now mechanised. [#179](https://github.com/react-native-linux/react-native-linux/issues/179)
  landed a pinned-tag, patch-queue, drift-gated pipeline. A rebase is a command, a conflict is a named
  patch and a rejected hunk, and the day a rebase stops being free is a CI failure rather than a
  discovery. The survey did not assume that pipeline existed, because it did not.
- A patch queue in `git apply -p1` shape against an upstream tag *is* the upstream pull request. The
  overlay is not a detour away from contributing; it is the contribution, held until it is provable.

The obligation is real and recurring, and it is now the second one this project accepts knowingly —
the first being the `react-native` JS overrides in ADR-0001. Each package's README carries its patch
table and each patch its deletion trigger, so the obligation stays countable.

## Rejected, with the evidence

- **Emulate the Objective-C runtime so `react-native-macos` code loads.** The runtime is not the
  dependency; the frameworks are. No surveyed library ships an Objective-C binary — they ship sources
  importing UIKit, AppKit, Metal, AVFoundation and CoreGraphics. `react-native-svg`'s Apple half alone
  is 212 files and ships compiled `.metallib` resources. Emulation buys the ability to load code that
  fails at its first framework symbol, and it would make our paint model UIKit's, reversing ADR-0001's
  central decision. (§6.1)
- **Emulate the Android JNI ABI.** The shim is the easy half; ART, `android.*`, `androidx.*`, Play
  services and the resource system are the library. `react-native-gesture-handler`'s Android half is
  62 Kotlin files typed in `MotionEvent`, `ViewGroup` and `ViewConfiguration` — running them means
  running Android. Amazon Vega is the proof by example: a Linux OS running React Native that ported
  React Common, Yoga and Hermes instead. (§6.2)
- **Route native libraries through `react-native-web`.** It makes `Platform.OS` a lie for any library
  that branches on it, and most `.web` implementations are stubs. Retained as a per-library shim
  *source*, recorded per use, never as policy. (§6.3)
- **Wait for an upstream out-of-tree module contract.** `react-native-community/discussions-and-proposals#195`
  asked in 2020 and the answer in the thread is still literally `?`. We file the one-line upstream PRs
  that cost nothing and compound; nothing on the roadmap waits on them. (§6.4)

## Deferred, on the record

- **The Expo host.** A C++ Expo module host, a JavaScript shim tier, or neither, decided in
  [#155](https://github.com/react-native-linux/react-native-linux/issues/155). The survey recommends
  the shim tier first because it reaches eleven flagship modules without adopting a new module system.
- **The Tier B scene extension point** for custom-paint third-party components, deliberately gated on
  two consumers landing first ([#151](https://github.com/react-native-linux/react-native-linux/issues/151)),
  per the Prime Directive.

## Consequences

- The first ten ports have an execution order, cheap unblockers first: Nitro platform files,
  `react-native-safe-area-context`, the Expo shim tier, `react-native-unistyles`, `expo-sqlite`,
  masked-view, `expo-blur` and `expo-linear-gradient`, `react-native-screens`, `react-native-svg`,
  `react-native-gesture-handler`. (§7)
- Every package is a standing rebase cost. The pipeline makes it a command and the drift gate makes it
  visible, but it does not make it zero, and the count of packages is the count of obligations.
- `packages/*/upstream` is vendored third-party code: the lint, format, typo and duplication gates
  ignore it, and our own sources never mix into it.
- The first package is `@react-native-linux/reanimated`
  ([#181](https://github.com/react-native-linux/react-native-linux/issues/181)), whose first patch is
  the JavaScript bring-up rung decided in [#133](https://github.com/react-native-linux/react-native-linux/issues/133).
  It is also the first test of whether the model survives contact with a large, fast-moving library.
