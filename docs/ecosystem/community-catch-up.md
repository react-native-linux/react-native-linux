# Community catch-up

This project is in its validation phase. No library maintainer has asked for Linux support, and none
owes us any. So every integration starts as ours: an overlay package under `packages/`, carried by
[the streaming pipeline](upstream-streaming.md). The ask comes later, and it comes with evidence.

This is the playbook for that ask — when to open it, what goes in it, and what happens to the package
afterwards. It is [issue #182](https://github.com/react-native-linux/react-native-linux/issues/182);
the model it serves is [issue #96](https://github.com/react-native-linux/react-native-linux/issues/96).

## When a package is ready to be offered

Not when it compiles. All four of these hold first:

1. **It runs.** The library's own public API works on Linux in an app, not only in a unit test. For a
   renderer-touching library that means a golden or an e2e scenario under `packages/<lib>/e2e/`.
2. **The patch queue is small and each patch has a reason.** A queue of thirty patches says the
   integration is still being discovered. A queue of three, each with a one-line rationale in the
   package README, is a proposal.
3. **`pnpm upstream:check` is green against the library's current release**, not a tag from six
   months ago. Asking a maintainer to adopt work against a stale base wastes their afternoon.
4. **The conformance kit passes** — see [package-template.md](package-template.md). It is the
   evidence that replaces "trust us".

Until then the package is ours and the ask does not exist.

## What the ask contains

One issue or pull request upstream, in this order:

- **What Linux needs, in their vocabulary.** Not "we built a platform"; rather "this file branches on
  `Platform.OS` and there is no branch that fits a platform without a native module for X".
- **The diff.** Our patch queue is already in `git apply -p1` shape against their tag, so it
  transfers as a pull request without translation. Link the patch files.
- **The evidence.** The conformance run and the scenario that proves the behaviour, with the command
  a maintainer can run.
- **What we will carry.** Explicitly: the native sources under `packages/<lib>/linux/`, the CI for
  them, and the answer to "who fixes this when it breaks". A maintainer's first question is who is on
  the hook, and the honest answer is us.
- **What we are asking them to own.** Usually far less than the whole port — most often a seam:
  a platform-agnostic branch instead of a hard-coded platform name, an extension point, or a
  published type. Ask for the seam, not for adoption.

The first instance of this is
[issue #145](https://github.com/react-native-linux/react-native-linux/issues/145) — the Software
Mansion RFC for Reanimated and Worklets, whose ask 5 is exactly a seam: key the existing JavaScript
fallback on "this platform has no native Worklets module" rather than on `Platform.OS === 'windows'`.

## What happens to the package afterwards

A package shrinks as upstream adopts pieces. Three outcomes, in order of preference:

- **Upstream takes the seam.** The corresponding patch is deleted from the queue on the next
  `pnpm upstream:bump`, and the package README records which release absorbed it. This is the goal:
  the package tends towards a thin `linux/` directory plus a lock file.
- **Upstream takes nothing, and the library keeps moving.** The package stays, the queue is
  rebased on every release by the pipeline, and the drift gate tells us the day a rebase stops being
  free.
- **Upstream takes the whole thing.** The package becomes an alias and is deleted; the alias entry in
  `packages/cli/src/package-aliases.ts` goes with it.

A package is never deleted because the ask was declined. Declining is the expected answer during the
validation phase — that is what "validation phase" means.

## The template

Copy this into the upstream issue and fill it in. Keep it short; a maintainer reads the first
paragraph and the diff.

```markdown
## What this is

`@react-native-linux/<lib>` carries <lib> on Linux (Wayland, Skia/Vulkan, Fabric-only, React Native
<version>). It works today as an overlay: your source at <tag> plus <N> patches. This is not a
request to support Linux — it is a request for one seam so the overlay can shrink.

## The seam we are asking for

<one paragraph, in their terms, naming the file and the branch>

## The diff

<links to patches/000N-*.patch>

## Evidence

<the conformance command and its output; the scenario; the screenshot or trace>

## Who carries what

We carry <linux/ sources, CI, breakage>. You would carry <the seam only>.
```

## What we do not do

- We do not open the ask before the package works. An unproven port is a support burden dressed as a
  contribution.
- We do not fork upstream repositories on GitHub. The overlay lives here, so their issue tracker
  stays theirs.
- We do not ask for a platform in their CI matrix until we can offer a runner for it.
