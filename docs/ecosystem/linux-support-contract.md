# The Linux support contract

You maintain a React Native library with native code. You do not own a Linux machine, you have not
been asked about Linux, and you have a backlog. This page is what we ask of you anyway — four rules,
each of which is already true of at least one library in the ecosystem — and what we guarantee back.

It is [issue #158](https://github.com/react-native-linux/react-native-linux/issues/158). It is for an
*existing* library adding Linux; if you are starting a new one, read
[the package template](package-template.md) instead. The research behind every claim is
`docs/research/ecosystem-compatibility.md` §4.4, and a change to one requires a change to the other.

## What we ask

### 1. Put the logic in `cpp/`

If a module's behaviour is expressible in C++, put it there. `create-react-native-library`'s
`cpp-library` template and a C++ Nitro hybrid object are both portable by construction: they compile
on any platform with a C++ toolchain, and a platform like ours picks them up with no code of its own.

**The proof.** `react-native-mmkv` declares its factory as `"all": {"language": "c++"}` in `nitro.json`,
and Tencent's MMKV ships a POSIX port with its own CMake. Everything standing between it and Linux is
in *our* repository, not in theirs.

### 2. Do not put platform code behind an unguarded `#ifdef`

A header that reads

```cpp
#ifdef ANDROID
using SyncFn = /* … */;
#elif __APPLE__
using SyncFn = /* … */;
#endif

struct PlatformDepMethodsHolder {
  SyncFn synchronouslyUpdateUIProps;   // does not compile on a third platform
};
```

does not fail to *link* on a new platform. It fails to **compile**, and it fails inside your header,
which means a port cannot even begin without editing your source.

**The example.** That is `react-native-reanimated`'s `PlatformDepMethodsHolder.h`, and behind it sit 99
`ANDROID`/`__APPLE__` conditionals in `Common/cpp`. An `#else` with a documented default — a no-op, an
`std::nullopt`, a `static_assert` with a message naming what a platform must provide — costs you
nothing and is the whole difference between "needs a port" and "needs a fork".

### 3. Declare the platform seam

State what a new platform owes, in a form a machine can read. Nitro is the model: `nitro.json` says
which members are `"all"` and which are per-platform, and the generated `Hybrid*Spec.hpp` types the
rest. `react-native-unistyles`' Linux obligation is therefore not "port unistyles" but "implement 18
typed methods" — a countable job with an end.

A declared seam is also what lets us tell you, honestly, how much of your library works here before we
ask you for anything.

### 4. Keep the C++ half of a Fabric component in `common/cpp`

Shadow nodes, props, state and component descriptors are platform-free. Six of the fifty
most-downloaded native libraries already keep them in shared C++, and those six get their layout,
their prop parsing and their descriptor registration on Linux for free — the only remaining work is
the paint, which is ours.

`react-native-safe-area-context` and `react-native-screens` are both in this shape today.

## What we guarantee back

- **Stable CMake target names.** Link `jsi` and `react_codegen_<Name>Spec` and your existing
  `android/CMakeLists.txt` works here unchanged. The third name `create-react-native-library` emits,
  `reactnative`, is **not exported yet** — it is owed by
  [#147](https://github.com/react-native-linux/react-native-linux/issues/147), the CMake autolinking
  consumer. This page will say "exported" when it is; until then, that is one line you would have to
  add, and we would rather tell you than surprise you.
- **Generated registration.** No library ships a Linux `OnLoad`, `+load` or `JNI_OnLoad`. Registration
  is generated from your spec ([#148](https://github.com/react-native-linux/react-native-linux/issues/148)).
- **A codegen output shape you can rely on**, produced by the same `@react-native/codegen` you already
  use, checked in and drift-gated
  ([#21](https://github.com/react-native-linux/react-native-linux/issues/21)).
- **A conformance kit you run in your own CI**
  ([#157](https://github.com/react-native-linux/react-native-linux/issues/157)): it resolves your
  package the way our autolinking does and says which discovery rule matched or why none did, compiles
  your C++ against our headers with the conditional-compile traps as errors, loads it into a headless
  Wayland harness and calls every generated spec method. Its failure output names the rule above that
  you are on the wrong side of.
- **A stated ABI boundary** ([#89](https://github.com/react-native-linux/react-native-linux/issues/89)).
  We will not promise anything that issue has not stated.

## What we do not ask

- We do not ask you to add Linux to your CI matrix until we can offer you a runner for it.
- We do not ask you to accept a port you did not want. Until you do, Linux support for your library
  lives in our repository as an overlay package and is our problem — see
  [the community catch-up playbook](community-catch-up.md).
- We do not ask you to support Wayland, Skia, or anything else about how we draw. That is the point of
  the four rules: none of them mentions our renderer.

## If you want to help in one commit

The cheapest useful change, in order:

1. Add the missing `#else` to any platform `#ifdef` in a header (rule 2).
2. Move a pure-logic file from `android/` and `ios/` into `cpp/` (rule 1).
3. Mark a Nitro member `"all"` when its implementation is already portable (rule 3).

Each of those is a change we would otherwise carry as a patch against your tag, forever.
