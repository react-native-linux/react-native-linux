# Upstream streaming

Every ecosystem package under `packages/` follows one model: **fork and overlay with a streamed
upstream**. The library's own source is vendored at a pinned tag, our Linux integration lives beside
it as an ordered patch queue, and both are reproducible from the lock file. The model, and why the
ecosystem is a set of monorepo packages rather than a set of forked repositories, is decided in
[issue #96](https://github.com/react-native-linux/react-native-linux/issues/96); this pipeline is
[issue #179](https://github.com/react-native-linux/react-native-linux/issues/179).

```text
packages/<lib>/
├── upstream.lock.json   # repo, tag, sparse paths, sha256 of every vendored file
├── upstream/            # the vendored tree, checked in, patches already applied
├── patches/             # 0001-*.patch … applied in numeric order
└── src/                 # our own Linux sources, never mixed into upstream/
```

## Why patches and not silent edits

A vendored tree that anyone can edit in place is a fork the day it lands: nobody can tell our change
from theirs, `git log` in this repository shows one commit for a hundred upstream files, and the next
release is a manual merge that only the person who did it understands.

The patch queue makes every deviation from upstream a reviewable, ordered, replayable artifact:

- A code review reads `patches/0003-linux-shadow-nodes.patch`, not a 40,000-line vendored diff.
- `pnpm upstream:bump` replays the queue against a new tag and fails loudly on the patches that no
  longer apply, so upgrade cost is visible per change instead of being discovered at runtime.
- A patch is already the shape of an upstream contribution — see *Graduating a patch* below.
- CI can prove nobody edited `upstream/` behind the queue's back: `pnpm upstream:check`.

`upstream/` is therefore generated output that happens to be checked in. Treat it exactly like
`packages/core/generated`: change it through the generator, never by hand.

## The lock file

```json
{
  "repo": "https://github.com/software-mansion/react-native-reanimated.git",
  "tag": "3.19.0",
  "sparsePaths": ["packages/react-native-reanimated/Common", "packages/react-native-reanimated/src"],
  "sha256": {
    "packages/react-native-reanimated/src/index.ts": "9f2c…"
  }
}
```

- `repo` and `tag` are what gets cloned: `git clone --filter=blob:none --sparse --depth 1 --branch <tag>`.
- `sparsePaths` is a non-empty cone-mode sparse checkout. Root-level files come along with any sparse
  checkout; everything else has to be listed.
- `sha256` describes the **pristine** upstream tree at that tag, before any patch is applied — one
  digest per vendored file. It is what makes a moved or re-cut tag a hard failure instead of a silent
  content change, and its diff between two bumps is the honest answer to "what did upstream change".

## Commands

```bash
pnpm upstream:vendor <lib>          # clone the locked tag, verify sha256, apply the queue
pnpm upstream:bump <lib> <tag>      # re-vendor at a new tag, replay the queue, rewrite the lock
pnpm upstream:patch <lib> <name>    # capture upstream/'s current diff as the next numbered patch
pnpm upstream:check [<lib>]         # regenerate into a temp dir and diff — the CI drift gate
```

All four are `node scripts/upstream.ts <command>`; the implementation is pure functions over injected
filesystem, git and patch-applier seams in `scripts/upstream/`, unit tested at 100% against a real
throwaway git repository.

Every command works the same way: clone the locked tag into a temporary directory, apply the patch
queue there with `git apply --3way -p1`, and only then touch `packages/<lib>/upstream`. Nothing runs
git inside this repository's working tree, and a failed clone can never delete a vendored tree.

### Bootstrapping a new package

Write the lock with the repo, the tag and the sparse paths, leave `"sha256": {}`, then run
`pnpm upstream:bump <lib> <tag>` with the same tag. Bump does not verify digests — it writes them —
so the first run fills the lock in and vendors the tree. From then on, `vendor` and `check` verify.

### Capturing a patch

Edit files in `packages/<lib>/upstream/` as you would any source, then:

```bash
pnpm upstream:patch reanimated linux-shadow-nodes
```

The pristine reference is re-cloned into a temporary directory and the existing queue replayed onto
it, so what you get is exactly your edits and nothing else, as `patches/000N-linux-shadow-nodes.patch`.
Patch names are lowercase kebab case; the number is assigned in order.

Passing a `<name>` that an existing patch already has **refreshes that patch in place**: the queue is
replayed only up to the patch before it, and the file is rewritten with the same number. That is how
a conflict gets resolved without renumbering the queue.

## Bumping, and the conflict playbook

```bash
pnpm upstream:bump reanimated 3.20.0
```

On success the lock is rewritten (new tag, new digests), the tree is replaced, and the summary says
how many upstream files were added, removed and changed, and how many patches replayed.

When a patch stops applying, the bump stops on it:

```text
0003-linux-shadow-nodes.patch does not apply to 3.20.0:
error: patch failed: Common/cpp/ShadowNodes.cpp:112
packages/reanimated/upstream holds 3.20.0 with the queue applied up to 0003-linux-shadow-nodes.patch.
Resolve the conflict there, then run:
  pnpm upstream:patch reanimated linux-shadow-nodes
  pnpm upstream:bump reanimated 3.20.0
```

The lock is already at the new tag and the tree already holds the new tag with the queue applied up
to and including the failing patch — with conflict markers where `--3way` could merge, and unapplied
where it could not. So:

1. Resolve the conflict in `packages/<lib>/upstream/` by hand. That tree is the new upstream plus
   everything ahead of the failing patch, so you are resolving one patch, not the whole queue.
2. `pnpm upstream:patch <lib> <name>` — refreshes that patch in place against the new tag.
3. `pnpm upstream:bump <lib> <tag>` — replays from the top; the refreshed patch now applies and the
   run continues to the next one, which may conflict in turn. Repeat until it succeeds.
4. `pnpm upstream:check` to confirm, then commit the lock, the tree and the refreshed patches
   together in one commit.

If a patch turns out to be obsolete — upstream fixed it — delete the file and renumber the ones after
it before re-running the bump.

## The drift gate

`pnpm upstream:check` runs in the `validate` job after the codegen check. For each
`packages/*/upstream.lock.json` it re-clones, verifies the pristine digests against the lock, replays
the queue and compares the result with the checked-in tree, printing every `added`, `removed` and
`changed` path and exiting 1 on any of them. It also fails when the upstream tag no longer hashes to
the lock.

With no `packages/*/upstream.lock.json` anywhere, discovery finds nothing, the command prints
`no packages/*/upstream.lock.json found; nothing to check` and exits 0. That is the zero-package
baseline: the gate is wired before the first ecosystem package exists.

## What the first package still has to add

`upstream/` is third-party source held to upstream's standards, not ours. The package that lands
first ([issue #180](https://github.com/react-native-linux/react-native-linux/issues/180)) has to keep
it out of the repository-wide quality gates that assume our own code: the `ignorePatterns` of
`.oxlintrc.json` and `.oxfmtrc.json`, and the `ignore` list of `.jscpd.json`, all of which already
exclude `third_party` for the same reason. Our Linux sources in `packages/<lib>/src` stay inside every
gate.

## Graduating a patch to an upstream PR

Carrying a patch forever is the fallback, not the goal. A patch is ready to be proposed upstream when
it survives at least one bump unchanged, it does not depend on anything Linux-specific that upstream
has no concept of, and the behavior it adds is covered by our test layers.

Patches are captured as `-p1` diffs against the upstream repository root with `a/` and `b/` prefixes —
the same shape `git format-patch` produces — so they apply directly to a clone of the library:

```bash
git -C ~/src/react-native-reanimated checkout -b linux-shadow-nodes
git -C ~/src/react-native-reanimated apply /path/to/packages/reanimated/patches/0003-linux-shadow-nodes.patch
```

Open the PR with the evidence the library maintainers need: what breaks on Linux without it, the test
that covers it here, and the platform's out-of-tree status. When it merges, delete the patch on the
bump that first contains it — the file disappears from the queue and the tree keeps the same content,
which `pnpm upstream:check` confirms in the same commit.
