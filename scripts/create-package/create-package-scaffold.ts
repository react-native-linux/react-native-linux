const SPARSE_FLAG = "--sparse";
const NOT_FOUND_INDEX = -1;
const NEXT_ARGUMENT = 1;
const FIRST_ARGUMENT = 0;
const LIBRARY_NAME_PATTERN = /^[a-z][a-z0-9-]*$/u;
const DEFAULT_SPARSE_PATHS: readonly string[] = ["src"];

interface CreatePackageRequest {
  readonly libraryName: string;
  readonly repo: string;
  readonly sparsePaths: readonly [string, ...string[]];
  readonly tag: string;
}

interface ScaffoldFile {
  readonly contents: string;
  readonly relativePath: string;
}

const readSparsePathArguments = (commandArguments: readonly string[], flagIndex: number): readonly string[] =>
  flagIndex === NOT_FOUND_INDEX ? DEFAULT_SPARSE_PATHS : commandArguments.slice(flagIndex + NEXT_ARGUMENT);

const readPositionalArguments = (commandArguments: readonly string[], flagIndex: number): readonly string[] =>
  flagIndex === NOT_FOUND_INDEX ? commandArguments : commandArguments.slice(FIRST_ARGUMENT, flagIndex);

const parseCreatePackageArguments = (commandArguments: readonly string[]): CreatePackageRequest | null => {
  const flagIndex = commandArguments.indexOf(SPARSE_FLAG);
  const [libraryName = "", repo = "", tag = "", unexpectedArgument = ""] = readPositionalArguments(
    commandArguments,
    flagIndex,
  );
  const [firstSparsePath = "", ...remainingSparsePaths] = readSparsePathArguments(commandArguments, flagIndex);

  if (!LIBRARY_NAME_PATTERN.test(libraryName) || repo === "" || tag === "") {
    return null;
  }

  if (unexpectedArgument !== "" || firstSparsePath === "") {
    return null;
  }

  return { libraryName, repo, sparsePaths: [firstSparsePath, ...remainingSparsePaths], tag };
};

const packageNameOf = (libraryName: string): string => `@react-native-linux/${libraryName}`;

const upstreamEntryOf = (request: CreatePackageRequest): string =>
  `upstream/${request.sparsePaths[FIRST_ARGUMENT]}/index.ts`;

const buildPackageManifest = (request: CreatePackageRequest): string => `{
  "name": "${packageNameOf(request.libraryName)}",
  "version": "0.0.0",
  "private": true,
  "type": "module",
  "exports": {
    ".": "./src/index.ts"
  },
  "peerDependencies": {
    "@react-native-linux/core": "workspace:*",
    "react-native": "*"
  }
}
`;

const formatSparsePaths = (sparsePaths: readonly string[]): string =>
  sparsePaths.map((sparsePath) => `    ${JSON.stringify(sparsePath)}`).join(",\n");

const buildUpstreamLock = (request: CreatePackageRequest): string => `{
  "repo": ${JSON.stringify(request.repo)},
  "sha256": {},
  "sparsePaths": [
${formatSparsePaths(request.sparsePaths)}
  ],
  "tag": ${JSON.stringify(request.tag)}
}
`;

const buildEntryModule = (request: CreatePackageRequest): string => `export * from "../${upstreamEntryOf(request)}";\n`;

const buildLinuxReadme = (request: CreatePackageRequest): string => `# linux/

The native Linux sources of \`${packageNameOf(request.libraryName)}\`, ours and never upstream's:

- C++ TurboModules implementing this library's native module specs.
- Fabric components: component descriptors, shadow nodes, and the Skia drawing of its views.
- \`CMakeLists.txt\`, the fragment that builds both into the platform.

\`@react-native-linux/cli\` registers this directory as the package's \`sourceDir\`
(\`platformConfig.platforms.linux\`), which is where autolinking looks for it. Native code that belongs to upstream
goes into a patch instead, so that the next bump replays it.
`;

const buildReadme = (request: CreatePackageRequest): string => `# ${packageNameOf(request.libraryName)}

The Linux overlay of ${request.repo}, vendored at \`${request.tag}\`.

## Layout

- \`upstream/\` — the vendored tree at the locked tag with the patch queue applied. Checked in, so \`pnpm install\`
  needs no network, and generated: change it through the commands below, never by hand.
- \`patches/\` — our deviations from upstream, applied in numeric order.
- \`linux/\` — our native Linux sources; see \`linux/README.md\`.
- \`src/\` — our JavaScript, and the entry point this package exports.
- \`e2e/\` — conformance scenarios, discovered by \`pnpm e2e\`.

## Upstream

| Field | Value |
| --- | --- |
| Repository | ${request.repo} |
| Tag | \`${request.tag}\` |
| Sparse paths | ${request.sparsePaths.map((sparsePath) => `\`${sparsePath}\``).join(", ")} |

\`\`\`bash
pnpm upstream:bump ${request.libraryName} <tag>       # re-vendor at a new tag and replay the patch queue
pnpm upstream:patch ${request.libraryName} <name>     # capture the current edits to upstream/ as the next patch
pnpm upstream:check ${request.libraryName}            # prove the vendored tree still matches tag plus queue
\`\`\`

## Patches

Every patch records what it changes and what deletes it, so that carrying one forever is a decision instead of an
accident. A patch that upstream merges is deleted on the bump that first contains it.

| Patch | What it changes | Deletion trigger |
| --- | --- | --- |
| _none yet_ | | |

## Conformance

Scenarios live in \`e2e/*.json\`, run against a real bundle under the headless compositor by
\`pnpm e2e --scenario <scenario name>\`, and grade against \`e2e/goldens\` and \`test-bundles\` of this package.

## Before the first release

- [ ] Point \`src/index.ts\` and the \`exports\` of \`package.json\` at this library's real entry points; the scaffold
      guesses \`${upstreamEntryOf(request)}\` and maps only \`.\`.
- [ ] Register \`{ linux: "${packageNameOf(request.libraryName)}", upstream: "<upstream package name>" }\` in
      \`packages/cli/src/package-aliases.ts\` so that a \`linux\` bundle resolves this package instead.
- [ ] Drop \`"private": true\` when the package is published as \`${packageNameOf(request.libraryName)}\`, versioned
      with the platform.
`;

const buildPackageFiles = (request: CreatePackageRequest): readonly ScaffoldFile[] => [
  { contents: buildPackageManifest(request), relativePath: "package.json" },
  { contents: buildUpstreamLock(request), relativePath: "upstream.lock.json" },
  { contents: "", relativePath: "patches/.gitkeep" },
  { contents: buildLinuxReadme(request), relativePath: "linux/README.md" },
  { contents: buildEntryModule(request), relativePath: "src/index.ts" },
  { contents: buildReadme(request), relativePath: "README.md" },
];

export { buildPackageFiles, parseCreatePackageArguments };
