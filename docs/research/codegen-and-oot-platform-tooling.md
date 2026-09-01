# Codegen, CLI, and Out-of-Tree Platform Tooling

> Research report, 2026-09-01, verified against react-native 0.87-stable. Legend: [V] verified in source, [I] inferred.

# Codegen + CLI + Tooling for an Out-of-Tree `react-native-linux` Platform

All RN file paths verified against tag/branch **`0.87-stable`** (`facebook/react-native`, which redirects to `react/react-native`, commit `2a41d8cb82`). Marked **[V]** = verified by reading source, **[I]** = inferred.

---

## 1) `@react-native/codegen`

### 1.1 Location and generator inventory **[V]**

Lives at `packages/react-native-codegen/` — https://github.com/facebook/react-native/tree/0.87-stable/packages/react-native-codegen

`src/` layout: `CodegenSchema.js`, `SchemaValidator.js`, `cli/`, `generators/`, `parsers/`.

**`src/generators/components/`** (23 entries):

| File | Target |
|---|---|
| `GenerateComponentDescriptorH.js` / `Cpp.js` | C++ (platform-neutral Fabric) |
| `GenerateEventEmitterH.js` / `Cpp.js` | C++ (neutral) |
| `GeneratePropsH.js` / `Cpp.js` | C++ (neutral) |
| `GenerateStateH.js` / `Cpp.js` | C++ (neutral) |
| `GenerateShadowNodeH.js` / `Cpp.js` | C++ (neutral) |
| `GenerateComponentHObjCpp.js` | **iOS/ObjC++** |
| `GenerateThirdPartyFabricComponentsProviderH.js` / `ObjCpp.js` | **iOS/Apple only** |
| `GeneratePropsJavaInterface.js`, `GeneratePropsJavaDelegate.js`, `GeneratePropsJavaPojo/` | **Android/Java** |
| `GenerateViewConfigJs.js` | **JS (neutral)** |
| `GenerateTests.js` | C++ gtest (neutral) |
| `CppHelpers.js`, `JavaHelpers.js`, `ComponentsGeneratorUtils.js`, `ComponentsProviderUtils.js` | helpers |

**`src/generators/modules/`**:

| File | Target |
|---|---|
| `GenerateModuleH.js` | **C++ JSI TurboModule spec — platform-neutral** |
| `GenerateModuleJavaSpec.js`, `GenerateModuleJniCpp.js`, `GenerateModuleJniH.js` | **Android** |
| `GenerateModuleObjCpp/` | **iOS** |

Roughly **60% of generators emit platform-neutral C++** (Fabric ShadowNode/Props/State/EventEmitter/ComponentDescriptor + JSI TurboModule header). Only the ObjC++/Java/JNI ones are platform-bound. This is the single most important fact for the ADR.

### 1.2 Is the platform set hardcoded? **YES — in two places [V]**

**(a) The authoritative gate** — `packages/react-native/scripts/codegen/generate-artifacts-executor/index.js`, lines 74–84:

```js
const supportedPlatforms = ['android', 'ios'];
if (
  targetPlatform !== 'all' &&
  !supportedPlatforms.includes(targetPlatform)
) {
  throw new Error(
    `Invalid target platform: ${targetPlatform}. Supported values are: ${supportedPlatforms.join(
      ', ',
    )}, all`,
  );
}
```

`--platform linux` hard-throws here. `'all'` means **"android + ios"**, not "any platform".

**(b) The generator dispatch table** — `packages/react-native/scripts/codegen/generate-specs-cli-executor.js`, lines 17–30:

```js
const GENERATORS = {
  all:        { android: ['componentsAndroid','modulesAndroid','modulesCxx'],
                ios:     ['componentsIOS','modulesIOS','modulesCxx'] },
  components: { android: ['componentsAndroid'], ios: ['componentsIOS'] },
  modules:    { android: ['modulesAndroid','modulesCxx'], ios: ['modulesIOS','modulesCxx'] },
};
```

Indexed as `GENERATORS[libraryType][platform]` (line 95). An unknown platform yields `undefined` → throws on iteration. Same file also has `assumeNonnull: platform === 'ios'` (line 90) and an Android-only JNI folder-restructuring hack at lines 99–110, whose comment admits: *"the generators don't support platform option yet"*.

**(c) `RNCodegen.js`** itself — `src/generators/RNCodegen.js` — is NOT platform-switched. It takes a **named generator-bundle list**:

```js
export type LibraryGenerators =
  | 'componentsAndroid' | 'componentsIOS' | 'descriptors' | 'events'
  | 'props' | 'states' | 'tests' | 'shadow-nodes'
  | 'modulesAndroid' | 'modulesCxx' | 'modulesIOS';
```

Note the neutral bundles: `descriptors`, `events`, `props`, `states`, `shadow-nodes`, `modulesCxx`.

**(d) Schema extraction is fully platform-agnostic [V].** `src/cli/combine/combine-utils.js` `filterJSFile(originalFilePath, currentPlatform, excludeRegExp)` treats `currentPlatform` as an arbitrary string and matches it against the `<name>.<platform>.js` suffix. `src/cli/combine/combine-js-to-schema-cli.js` exposes `-p/--platform` with `default: null` and **no validation** (just `.toLowerCase()`). So `NativeFoo.linux.ts` → schema works today, unmodified.

### 1.3 Extension points for third-party platforms

**No documented extension point.** The `packages/react-native-codegen/README.md` is 5 lines and mentions nothing. The official https://reactnative.dev/docs/the-new-architecture/using-codegen documents only android/ios.

**But there IS a de-facto injection seam [V]**, in `RNCodegen.js`:

- `module.exports.allGenerators = ALL_GENERATORS` (line 242) — every raw generator function is publicly exported by name.
- `generate()` accepts an optional `libraryGenerators` override (line 253): `libraryGenerators = LIBRARY_GENERATORS`, typed as `LibraryGeneratorsFunctions = Readonly<{[string]: Array<GenerateFunction>}>`. Present since at least `0.82-stable`.

⚠️ **Caveat [V]:** `RNCodegen.d.ts` has drifted from the JS. Its `LibraryOptions` does **not** include `libraryGenerators`, and it declares `export declare const libraryGenerators` / `schemaGenerators` which **do not exist** in `module.exports`. TypeScript consumers cannot use the seam without an escape hatch. Flag this in the ADR.

⚠️ **Packaging caveat [V]:** `packages/react-native-codegen/package.json` (v0.87.1) has **no `main` and no `exports` field**, only `"files": ["lib"]`. Deep imports like `@react-native/codegen/lib/generators/components/GeneratePropsH` work only because there's no exports map to block them — there is *no* public API surface at all. Every consumer is depending on unversioned build output.

**Issues/PRs found [V]** (searched `react/react-native` and `react-native-community/*`):

- https://github.com/react/react-native/pull/42360 — "Ignore TM specs with out-of-tree platform suffix". Summary: *"In cases where you merge out-of-tree platforms like react-native-windows with react-native mobile JS files, codegen awareness of the Windows suffix is useful."*
- https://github.com/react/react-native/pull/42047 — "feat: make codegen take OOT Apple platforms into account" (this is where `supportedApplePlatforms` in `SchemasOptions` came from).
- https://github.com/react-native-community/discussions-and-proposals/issues/195 — Turbo Modules developer scenarios (2020, stmoy/Microsoft). Scenario 7 asks *"At a high-level, what steps are involved in making RN4W (or any out of tree platform) work with TMs? A: ?"* — **still literally unanswered**.

**Conclusion: there has never been an RFC or accepted design for third-party codegen generators.** Every OOT platform reimplemented it.

### 1.4 `codegenConfig` fields in 0.87 **[V]**

Modern (single-object) form, per https://reactnative.dev/docs/the-new-architecture/using-codegen:

```json
"codegenConfig": {
  "name": "<SpecName>",
  "type": "all" | "modules" | "components",
  "jsSrcsDir": "<source_dir>",
  "android": { "javaPackageName": "<java.package.name>" },
  "ios": {
    "modules":    { "<Name>": { "className": "...", "unstableRequiresMainQueueSetup": false,
                                "conformsToProtocols": ["RCTImageURLLoader", "..."] } },
    "components": { "<Name>": { "className": "..." } }
  }
}
```

Additional **undocumented** fields read by the executor **[V]**:

- `includesGeneratedCode` — `generate-artifacts-executor/utils.js:24`
- `outputDir` — may be a string OR a `{[platform]: string}` map (`index.js:203-211`, `readOutputDirFromPkgJson`)
- `libraries: [...]` — deprecated array form; RN core's own `packages/react-native/package.json` **still uses it**.

**Is there a generic/"all" option? No.** `type: "all"` means *modules + components*, not *all platforms*. The `--platform all` flag means *android + ios only*. **There is no platform-generic mode.**

**However** — `codegenConfig` is unvalidated free-form JSON for unknown keys, which is how RNW added `codegenConfig.windows` (see §2.1). `codegenConfig.linux` is a safe, precedented extension.

### 1.5 Is the schema platform-neutral and reusable? **YES — this is the ADR's key enabler [V]**

- Type: `packages/react-native-codegen/src/CodegenSchema.js` (480 lines) + hand-written `src/CodegenSchema.d.ts`.
- `SchemaType = Readonly<{libraryName?: string, modules: {[hasteModuleName]: ComponentSchema | NativeModuleSchema}}>` — **zero platform fields**.
- The **only** platform leak in the whole schema is line 13:

  ```js
  export type PlatformType = 'iOS' | 'android';
  ```

  used in exactly two optional opt-out fields: `OptionsShape.excludedPlatforms` (line 131) and `NativeModuleSchema.excludedPlatforms` (line 291). Never required, only exclusionary.
- **Publicly consumable? Yes in practice, no by contract.** No `exports` map blocks it; there is also no supported entry point. Both RNW and RNOH deep-import `@react-native/codegen/lib/CodegenSchema` and `lib/parsers/*` (see §2).

**A new platform can absolutely consume the schema and write its own generators. That is exactly what every existing OOT platform does. [V]**

---

## 2) What other out-of-tree platforms actually did

### 2.1 react-native-windows — "reuse neutral C++ generators, fork the binding layer" **[V]**

Package: `packages/@react-native-windows/codegen` — https://github.com/microsoft/react-native-windows/tree/main/packages/@react-native-windows/codegen
`package.json` description is explicit: *"Generators for react-native-codegen targeting react-native-windows"*. Ships bin `react-native-windows-codegen`. `peerDependencies: {"react-native": "*"}`.

**Own generators** (`src/generators/`): `GenerateNM2.ts` (REACT_MODULE C++/C# macros), `GenerateComponentWindows.ts`, `GenerateTypeScript.ts`, `AliasGen.ts`, `AliasManaging.ts`, `ObjectTypes.ts`, `ParamTypes.ts`, `PropObjectTypes.ts`, `ReturnTypes.ts`, `ValidateConstants.ts`, `ValidateMethods.ts`.

**Reused RN generators** — `src/index.ts` resolves RN's codegen dynamically and deep-requires 10 neutral C++ generators (lines 23–27, 255–294):

```ts
const rnPath = path.dirname(require.resolve('react-native/package.json'));
const rncodegenPath = path.dirname(
  require.resolve('@react-native/codegen/package.json', {paths: [rnPath]}),
);
// ...
const generateJsiModuleH = require(path.resolve(
  rncodegenPath, 'lib/generators/modules/GenerateModuleH')).generate;
const generatorPropsH = require(path.resolve(
  rncodegenPath, 'lib/generators/components/GeneratePropsH')).generate;
// + PropsCpp, ShadowNodeH/Cpp, ComponentDescriptorH, EventEmitterH/Cpp, StateH/Cpp
```

Parsers reused too (lines 29–45): `lib/parsers/typescript/parser` (`TypeScriptParser`), `lib/parsers/flow/parser` (`FlowParser`), `lib/SchemaValidator`. Type imported from `@react-native/codegen/lib/CodegenSchema`.

They **reimplement** `parseFile()` and `combineSchemas()` rather than calling RN's `combine-js-to-schema` — with a documented hack (line 179): *"This is a temporary implementation until such information is added to the schema in facebook/react-native"* — sniffing `TurboModuleRegistry.get<` vs `getEnforcing<` from raw source because the schema has no optionality bit.

**CLI wiring:** own command `codegen-windows` (`packages/@react-native-windows/cli/src/commands/codegenWindows/codegenWindows.ts`), reading a private `codegenConfig.windows` sub-object: `namespace`, `cppStringType` (`std::string`|`std::wstring`), `separateDataTypes`, `outputDirectory`, `generators` — while reusing standard `codegenConfig.name`/`type`/`jsSrcsDir`.

**Verdict: RNW does NOT fork the RN repo.** `react-native-windows` is a sibling npm package (`vnext/`) with a `src-win/` overlay + an override manifest.

### 2.2 react-native-harmony / OpenHarmony — "100% own codegen, no `platforms` key at all" **[V]**

Repo (mirror of the OpenHarmony SIG repo): https://github.com/AuroraMaster/ohos_react_native — **not a fork** of react-native. Packages: `react-native-harmony`, `react-native-harmony-cli`, `react-native-harmony-hvigor-plugin`, `react-native-harmony-sample-package`. npm name `@rnoh/react-native-harmony`, `peerDependencies: {"react-native": "0.72.5"}` in the snapshot I read — **substantially behind upstream**.

**Codegen** — `react-native-harmony-cli/src/codegen/`:

- `core/UberSchema.ts` imports `SchemaType as RawUberSchema`, `ComponentSchema`, `NativeModuleSchema` from `@react-native/codegen/lib/CodegenSchema`, and deep-imports the combiner with an escape hatch:

  ```ts
  // @ts-expect-error
  import extractUberSchemaFromSpecFilePaths_ from '@react-native/codegen/lib/cli/combine/combine-js-to-schema.js';
  ```

- `generators/`: `ArkTSComponentCodeGeneratorArkTS.ts`, `ArkTSComponentCodeGeneratorCAPI.ts`, `CppComponentCodeGenerator.ts`, `NativeModuleCodeGenerator.ts`, `SharedComponentCodeGenerator.ts`, `LibGlueCodeGenerator.ts`, `AppBuildTimeGlueCodeGenerator.ts`, `UberGeneratorV1.ts`, `UberGeneratorV2.ts`. **Zero reuse of RN generators** — they emit ArkTS/C-API, so nothing upstream applies.

**Platform registration — they do NOT use the `platforms` key [V].** `react-native-harmony/react-native.config.js` exports only `{commands: harmonyConfig.commands}`. Instead they ship `createHarmonyMetroConfig()` in `react-native-harmony/metro.config.js` with a hand-rolled `resolveRequest`. Three things worth noting for the ADR:

1. Same `react-native` → `react-native-harmony` remap as RN's built-in helper (so they duplicated it).
2. A **fallback hack**: for RN-internal relative imports they can't remap, they resolve *as iOS*:

   ```js
   } else if (moduleName.startsWith('react-native/')) {
     return ctx.resolveRequest(ctx, moduleName, 'ios');
   ```

   i.e. Harmony masquerades as iOS for un-overridden RN core internals.
3. A third-party redirect registry via `package.json` `"harmony": {"alias": "react-native-foo", "redirectInternalImports": true}` — their own parallel autolinking/aliasing scheme.

**Do they ship a patched RN?** Effectively yes — `react-native-harmony/Libraries/**` is a shipped overlay of RN core JS, resolved by their custom resolver. No `overrides.json`-style manifest; drift is managed manually.

### 2.3 react-native-macos and react-native-visionOS — full hard forks **[V]**

| | react-native-macos | react-native-visionOS |
|---|---|---|
| URL | https://github.com/microsoft/react-native-macos | https://github.com/callstack/react-native-visionos |
| GitHub `fork` flag | **true**, parent `react/react-native` | **true**, parent `react/react-native` |
| Repo size | **~490 MB** | ~328 MB |
| Branch model | `0.58-stable` … `0.87-merge` (per-RN-version merge branches) | `0.73-stable` … `0.79-stable`, `main` |
| Last push | 2026-08-17 (active) | **2025-09-03 (~12 months stale)**, not archived |
| Forks `packages/` | **all 26** incl. `react-native-codegen`, `gradle-plugin`, `community-cli-plugin`, `metro-config`, `rn-tester` | full RN monorepo (`build.gradle.kts`, `.flowconfig`, etc.) |
| Renaming | `packages/react-native/package.json` → `"name": "react-native-macos", "version": "1000.0.0"` | same pattern |
| `overrides.json` | **none** — the fork *is* the override | none |

**Quantified forking spectrum for the ADR:**

```
react-native-macos / visionOS   ── fork the ENTIRE monorepo, perpetual merge branches  (max cost, max power)
react-native-windows            ── sibling package + src-win overlay + 123 tracked overrides (medium)
react-native-harmony            ── sibling package + shipped Libraries overlay, no manifest (medium, untracked drift)
```

Note the survivorship signal: **the only fully-forked platform still keeping pace with upstream (0.87) is macOS, which is staffed by Microsoft.** visionOS, using the same model with fewer resources, stalled at 0.79.

---

## 3) `react-native-platform-override`

Repo: https://github.com/microsoft/react-native-windows/tree/main/packages/react-native-platform-override
README: *"Tools to manage 'platform overrides' for out of tree React Native platforms. Facilities for inventorying changes and integrating new upstream changes are included."*

**Manifest** — `overrides.json` at npm package root **[V]**:

| Field | Required | Meaning |
|---|---|---|
| `includePatterns` | opt | globs that MUST be listed in the manifest (default `["**"]`) |
| `excludePatterns` | opt | globs excluded from the above |
| `baseVersion` | opt | default upstream RN version for entries |
| `overrides` | **req** | the entry list |

Per-entry: `{type, file, baseFile, baseVersion, baseHash}` (a SHA of the upstream file at that version).

**Override types [V]:**

| Type | Semantics |
|---|---|
| `Platform` | platform-only logic, no upstream counterpart |
| `Derived` | derived from an upstream file; upstream changes get merged in |
| `Patch` | an upstream file with edits; upstream changes get merged in |
| `Copy` | byte-identical copy of an upstream file/dir |

**CLI:** `validate` (are overrides recorded + up to date, `--version`/`--manifest`), `add <override>`, `remove <override>`, `diff <override>`, `upgrade` (auto-merge new upstream changes, `--no-conflicts`). Hits the GitHub API to fetch base files — `--githubToken` / `PLATFORM_OVERRIDE_GITHUB_TOKEN`. Programmatic API in `src/Api.ts` (`validateManifest({manifestPath})`).

### Why it exists — YES, OOT platforms in 2026 still patch RN core JS **[V]**

Measured from `vnext/overrides.json` (`baseVersion: 0.86.0-nightly-20260408-8bac1df5a`):

**123 tracked overrides: 43 `patch`, 43 `derived`, 29 `platform`, 8 `copy`.** `includePatterns: ['.flowconfig', 'ReactCopies/**', 'src-win/**']`.

Two clear clusters:

**(a) RN core JS component layer** (`src-win/Libraries/**`) — *patch*: `View.windows.js`, `Image.windows.js`, `TextInput.windows.js`, `TextInputState.windows.js`, `Pressable.windows.js`, `Pressability.windows.js`, `HoverState.js`, `Touchable*.windows.js` (4 files), `RefreshControl`, `Alert`, `AccessibilityInfo`, `ViewAccessibility`, `ViewPropTypes`, `PaperUIManager`, `resolveAssetSource`, `loadBundleFromServer`, `ReactNativeTypes`, `CoreEventTypes`, `ReactNativeFeatureFlagsBase.js`, plus deprecated specs (`RCTModalHostViewNativeComponent`, `SwitchNativeComponent`).
*derived*: `index.windows.js`(+`.flow`), `Text.windows.js`, `TextProps`, `ScrollView.windows.js`, `Switch`, `Modal`, `Button`, `BaseViewConfig`, `ViewConfigIgnore`, `AssetSourceResolver`, `LogBox` UI, **`Platform.windows.js`**, **`PlatformTypes.js`**, `NativePlatformConstantsWin`, `.flowconfig`, and several `.d.ts` files.

**(b) ReactCommon C++** — 18 patched + 6 derived files under a folder literally named **`ReactCommon/TEMP_UntilReactCommonUpdate/`**: `TurboModule.h`, `Bridging.h`, `JSExecutor.cpp`, `JSIExecutor.cpp`, `ReactInstance.cpp`, `UIManager.cpp`, `ComponentDescriptorRegistry.cpp`, `TouchEventEmitter.h/.cpp`, `propsConversions.h`, `accessibilityPropsConversions.h`, `AccessibilityPrimitives.h`, `ParagraphShadowNode.cpp`, `BaseParagraphProps.h/.cpp`, `TextAttributes.cpp`, `MapBuffer.cpp`, `NativeDOM.h/.cpp`, `NetworkIOAgent.h/.cpp`, `HttpUtils.cpp`, `Utf8.h`, `AnimatedPropSerializer.cpp`.

**Two concrete, structural reasons a Linux platform will hit the same wall [V]:**

1. `packages/react-native/Libraries/Utilities/PlatformTypes.js` hardcodes:

   ```js
   export type PlatformOSType = 'ios' | 'android' | 'macos' | 'windows' | 'web' | 'native';
   ```

   No `'linux'`. `Platform.select({linux: …})` and `Platform.OS === 'linux'` do not typecheck. This is *precisely* why `PlatformTypes.js` appears in RNW's derived-override list.
2. `Libraries/Utilities/Platform.js` only exists as an `exports`-compat shim; the real implementations are `Platform.ios.js` / `Platform.android.js`. A Linux platform must supply `Platform.linux.js` (in the OOT package, resolvable via the remap) — but the *type* union still lives upstream.

---

## 4) Platform registration and resolution plumbing

### 4.1 `react-native.config.js` `platforms` key — exact 2026 schema **[V]**

Types: https://github.com/react-native-community/cli/blob/main/packages/cli-types/src/index.ts (`@react-native-community/cli-types@20.2.0`)

```ts
export interface PlatformConfig<
  ProjectConfig, ProjectParams, DependencyConfig, DependencyParams,
> {
  npmPackageName?: string;
  projectConfig: (projectRoot: string, projectParams: ProjectParams | void) => ProjectConfig | void;
  dependencyConfig: (dependency: string, params: DependencyParams) => DependencyConfig | void;
}

export interface Config {
  // ...
  platforms: {
    android: AndroidPlatformConfig;
    ios: IOSPlatformConfig;
    [name: string]: PlatformConfig<any, any, any, any>;   // ← arbitrary platform names
  };
}

export type UserDependencyConfig = {
  dependency: Omit<DependencyConfig, 'name' | 'root'>;
  commands: Command[];
  platforms: Config['platforms'];   // "An array of extra platforms to load"
  healthChecks: [];
};
```

Runtime validation — `packages/cli-config/src/schema.ts` lines 98–106:

```ts
platforms: map(
  t.string(),
  t.object({
    npmPackageName: t.string().optional(),
    dependencyConfig: t.func(),
    projectConfig:    t.func(),
    linkConfig:       t.func(),
  }),
).default({}),
```

Arbitrary string keys. **This is the supported extension point.** `linkConfig` is legacy (RNW passes `() => null`).

**Reference implementations:**

- RNW: https://github.com/microsoft/react-native-windows/blob/main/vnext/react-native.config.js → `platforms: {windows: {linkConfig: () => null, projectConfig, dependencyConfig, npmPackageName: 'react-native-windows'}}`
- RN core itself: `packages/react-native/react-native.config.js` registers `platforms.ios` / `platforms.android` by lazily `require.resolve`-ing `@react-native-community/cli-platform-{ios,android}` from `process.cwd()`, with a candid comment: *"React Native shouldn't be exporting itself like this… This is a temporary workaround."*

### 4.2 Metro `resolver.platforms` **[V]**

- RN default: `packages/metro-config/src/index.flow.js`, in `getDefaultConfig()`:

  ```js
  resolver: {
    resolverMainFields: ['react-native', 'browser', 'main'],
    platforms: ['android', 'ios'],
    unstable_conditionNames: ['react-native'],
  },
  ```

- Metro's own default is broader: `['ios', 'android', 'windows', 'web']` — https://metrobundler.dev/docs/configuration/#platforms
- Resolution order — https://metrobundler.dev/docs/resolution/ (`RESOLVE_FILE`), for `platform='android'`, `sourceExts=['js','jsx']`:

  ```
  1. moduleName + '.android.js'
  2. moduleName + '.native.js'   (if preferNativePlatform)
  3. moduleName + '.js'
  4. moduleName + '.android.jsx'
  5. moduleName + '.native.jsx'  (if preferNativePlatform)
  6. moduleName + '.jsx'
  ```

  Note it cycles **per sourceExt**, not all-platforms-then-all-exts. For Linux: `Foo.linux.js` → `Foo.native.js` → `Foo.js` → `Foo.linux.jsx` → …

**The official OOT hook lives in RN core, not the community CLI [V]** — `packages/community-cli-plugin/src/utils/loadMetroConfig.js`, `getCommunityCliDefaultConfig()`:

```js
const outOfTreePlatforms = Object.keys(ctx.platforms).filter(
  platform => ctx.platforms[platform].npmPackageName,
);
const resolver = { platforms: [...Object.keys(ctx.platforms), 'native'] };

if (outOfTreePlatforms.length) {
  resolver.resolveRequest = reactNativePlatformResolver(
    outOfTreePlatforms.reduce((result, platform) => {
      result[platform] = ctx.platforms[platform].npmPackageName;
      return result;
    }, {}),
    config.resolver?.resolveRequest,
  );
}
```

It also injects `${npmPackageName}/setup-env` into `serializer.getModulesRunBeforeMainModule` alongside `react-native/setup-env`.

And `packages/community-cli-plugin/src/utils/metroPlatformResolver.js`:

```js
/**
 * ...remap react-native imports to different npm packages based on the platform requested.
 * Ex: { windows: 'react-native-windows', macos: 'react-native-macos' }
 */
export function reactNativePlatformResolver(platformImplementations, customResolver) {
  return (context, moduleName, platform) => {
    let modifiedModuleName = moduleName;
    if (platform != null && platformImplementations[platform]) {
      if (moduleName === 'react-native') {
        modifiedModuleName = platformImplementations[platform];
      } else if (moduleName.startsWith('react-native/')) {
        modifiedModuleName = `${platformImplementations[platform]}/${modifiedModuleName.slice('react-native/'.length)}`;
      }
    }
    // ...
  };
}
```

**Net: setting `npmPackageName: 'react-native-linux'` in the platform entry gets you `platforms: [..., 'linux', 'native']`, full `react-native` → `react-native-linux` redirection (including subpaths), and `setup-env` injection — for free. [V]**

### 4.3 How `--platform linux` flows to `bundle` / `start`; who owns it in 2026 **[V]**

- **`bundle` and `start` are owned by RN core**, in `@react-native/community-cli-plugin` (`packages/community-cli-plugin/`), exported from `src/index.flow.js` as `bundleCommand` / `startCommand` and pushed into RN's own `react-native.config.js`.
- `--platform` is declared in `packages/community-cli-plugin/src/commands/bundle/index.js`:

  ```js
  { name: '--platform <string>', description: 'Either "ios" or "android"', default: 'ios' }
  ```

  **Arbitrary string, no validation** — the description is just stale. `'linux'` passes through to Metro. **[V]**
- ⚠️ **Asset gotcha [V]:** `packages/community-cli-plugin/src/commands/bundle/saveAssets.js:45`

  ```js
  platform === 'android' ? getAssetDestPathAndroid : getAssetDestPathIOS
  ```

  An unknown platform silently gets **iOS asset layout**. `filterPlatformAssetScales.js` is benign (`ALLOWED_SCALES` only has `ios`; unknown platforms keep all scales). RNW solved this via https://github.com/react-native-community/cli/pull/2002 ("Allow out of tree platforms to provide customized saveAssets experiences") — worth checking whether that hook survived the move into core; **I could not confirm a `saveAssets` override hook exists in the 0.87 `community-cli-plugin` copy.**

### 4.4 CLI ownership in 0.86/0.87 — the answer is nuanced **[V]**

**RN has NOT shipped a built-in CLI. `@react-native-community/cli` is still the entry point — but it is no longer bundled.**

- `packages/react-native/package.json` (0.87.1): `"bin": {"react-native": "cli.js"}`, and `@react-native-community/cli` is **absent from `dependencies`** — verified identical in `0.85-stable`, `0.86-stable`, `0.87-stable`. RN depends only on `@react-native/community-cli-plugin`, `@react-native/gradle-plugin`, `@react-native/codegen`, etc.
- `packages/react-native/cli.js` is a **shim**: it `require.resolve('@react-native-community/cli', {paths: [process.cwd()]})` and calls `.run(name)`. If missing it prints:
  > ⚠️ `react-native` depends on `@react-native-community/cli` for cli commands. To fix update your `package.json` to include: `"devDependencies": {"@react-native-community/cli": "latest"}`

  The module-level export path is even stubbed with `deprecated = () => { throw new Error('react-native/cli is deprecated, please use @react-native-community/cli instead'); }`.
- There is **no `@react-native/cli` package** in `packages/` at 0.87 (26 packages). `commander@^12` in RN's deps is not a new CLI framework entry point I could locate.
- Current community CLI: **`@react-native-community/cli@20.2.0`**, repo active (last push 2026-06-25). Its `packages/`: `cli`, `cli-config`, `cli-config-android`, `cli-config-apple`, `cli-types`, `cli-tools`, `cli-doctor`, `cli-clean`, `cli-link-assets`, `cli-server-api`, `cli-platform-android`, `cli-platform-apple`, `cli-platform-ios`.
- The 0.87 blog (https://reactnative.dev/blog/2026/08/11/react-native-0.87) still instructs `npx @react-native-community/cli@latest init …` and announces no CLI restructuring.

**What this means for registering a Linux platform:** the `platforms` extension point is *stable and still live*. But ownership is now **split** — `bundle`/`start`/`codegen` in RN core, `config`/`init`/`doctor`/platform configs in the community CLI. Your `react-native-linux` must be discovered by the community CLI's config loader while its Metro behavior is driven by RN-core code. **[V]** Watch the RN-core comment quoted in §4.1 — Meta explicitly wants to invert this ("codegen command should be inhoused into @react-native-community/cli"), so the seam may move.

### 4.5 Autolinking — how platforms hook in **[V]**

**Single source of truth for both platforms: the JSON emitted by `npx @react-native-community/cli config`.**

- **Android** — `packages/gradle-plugin/settings-plugin/src/main/kotlin/com/facebook/react/ReactSettingsExtension.kt`:

  ```kotlin
  private val defaultConfigCommand: List<String> =
      windowsAwareCommandLine(listOf("npx", "@react-native-community/cli", "config"))
  ```

  writes `build/generated/autolinking/autolinking.json`, cache-keyed on SHA-256 of `yarn.lock`/`package-lock.json`/`package.json`/**`react-native.config.js`**. Then:

  ```kotlin
  internal fun getLibrariesToAutolink(buildFile: File): Map<String, File> {
    val model = JsonUtils.fromAutolinkingConfigJson(buildFile)
    return model?.dependencies?.values
        ?.filter { it.platforms?.android?.sourceDir != null }
        ?.filterNot { it.platforms?.android?.isPureCxxDependency == true }
        ?.associate { ":${it.nameCleansed}" to File(it.platforms?.android?.sourceDir) } ?: emptyMap()
  }
  ```

  Model: `packages/gradle-plugin/shared/src/main/kotlin/com/facebook/react/model/ModelAutolinkingDependenciesJson.kt` → `data class(root, name, platforms)`.
- **iOS** — `packages/react-native/scripts/cocoapods/autolinking.rb`:

  ```ruby
  $default_command = ['node', '-e', "process.argv=['', '', 'config'];require('@react-native-community/cli').run()"];
  def use_native_modules!(config_command = $default_command)
  # ...
  next unless package_config = package["platforms"]["ios"]
  ```

  Same JSON, same `platforms.<name>` selector.
- **Per-dependency opt-out is already platform-generic [V]** — `generate-artifacts-executor/utils.js:466`:

  ```js
  function findDisabledLibrariesByPlatform(reactNativeConfig, platform) {
    const dependencies = reactNativeConfig.dependencies ?? {};
    return Object.keys(dependencies).filter(
      dependency => dependencies[dependency].platforms?.[platform] === null,
    );
  }
  ```

  So `dependencies: {foo: {platforms: {linux: null}}}` works today.

**Can a third platform hook in? Yes for *discovery*, no for *consumption*. [V]** Implementing `dependencyConfig` puts `platforms.linux` into the same `autolinking.json` automatically. But there is **no generic autolinking consumer** — you must write the Linux analogue of `ReactSettingsExtension.kt` / `autolinking.rb` for whatever build system you pick (CMake/Meson). RNW did exactly this: `packages/@react-native-windows/cli/src/commands/autolinkWindows/` (MSBuild). RNOH did it in `react-native-harmony-cli/src/autolinking/` + an hvigor plugin.

---

## 5) Official out-of-tree platform template? **No. [V]**

- `react-native-community/react-native-template-typescript` — README opens with **"# This template is deprecated"**; last push **2023-06-26**. Not archived, but dead.
- `react-native-community/template` — alive (2026-08-26), but described as *"Get started building RN apps for **Android & iOS**"*. Not OOT-capable.
- Templates were removed as a first-class CLI concept; https://github.com/react-native-community/cli/pull/2422 ("init now expects templates to have react-native version set") is part of that rework, and https://github.com/react-native-community/cli/pull/2170 added a `--platform-name` option to `init` **specifically for out-of-tree platform init** (closed/merged) — that flag, not a template repo, is the surviving mechanism.
- **There is no `react-native-template-linux`-style scaffold and no official OOT platform starter.** Each platform ships its own `init` command (RNW: `initWindows`; RNOH: their own CLI commands).

**Official OOT docs are stale [V]** — https://reactnative.dev/docs/out-of-tree-platforms still documents the pre-CLI-3 `rnpm.haste` format:

```json
{"rnpm": {"haste": {"providesModuleNodeModules": ["react-native-example"], "platforms": ["example"]}}}
```

and admits *"The process of creating a React Native platform from scratch is still not very well documented."* It lists partner platforms (macOS, Windows, visionOS, OpenHarmony) and community ones (tvOS, Web, **React Native Skia — "supports Linux and macOS"** ← directly relevant prior art for this ADR). **Do not use this page as a spec** — the real contract is `cli-types`/`cli-config/schema.ts` + `loadMetroConfig.js`.

---

## Summary for the ADR

**What works out of the box for `linux` (zero upstream changes) [V]:**

1. Schema extraction: `combine-js-to-schema` `--platform linux`, `NativeFoo.linux.ts` filtering.
2. The schema IR itself — platform-neutral, only `excludedPlatforms` leaks `'iOS'|'android'`.
3. ~10 C++ generators (Fabric ShadowNode/Props/State/EventEmitter/ComponentDescriptor + JSI TurboModule `GenerateModuleH`).
4. Platform registration: `react-native.config.js` `platforms.linux = {npmPackageName, projectConfig, dependencyConfig}`.
5. Metro: `resolver.platforms` gains `linux`; `react-native` → `react-native-linux` remap via `reactNativePlatformResolver`; `setup-env` injection.
6. `--platform linux` through `bundle`/`start`.
7. Autolinking *discovery* — `platforms.linux` lands in `autolinking.json`; `platforms: {linux: null}` opt-out works.

**What you must build or fork:**

1. Your own codegen driver + Linux-specific generators (like RNW/RNOH) — RN's `generate-artifacts-executor` **hard-throws** on `linux`. Use `codegenConfig.linux` + a `codegen-linux` command.
2. A build-system autolinking consumer (no CMake/Meson analogue of the Gradle plugin or CocoaPods hook exists).
3. Asset save path (`saveAssets` silently falls back to iOS layout).
4. Overrides of RN core JS — **at minimum `PlatformTypes.js`** (the `PlatformOSType` union has no `'linux'`), plus a `Platform.linux.js` and realistically the View/Text/Image/ScrollView/TextInput/Pressability cluster. Budget on the order of RNW's **~86 patched/derived files**, and adopt `react-native-platform-override` to track it.
5. An `init`/scaffold command (no template exists).

**Stability risks to record:**

- `@react-native/codegen` has **no `main`, no `exports`** — all reuse is deep-import into `lib/` build output, unversioned, no compat guarantee.
- `RNCodegen.d.ts` is out of sync with `RNCodegen.js` (declares non-existent exports; omits the `libraryGenerators` injection seam).
- RN core's own `react-native.config.js` calls its platform wiring *"a temporary workaround"* and states codegen should move into the community CLI — the registration seam is explicitly slated to change.
- No RFC, no accepted design, and a 2020 question about OOT codegen (discussions-and-proposals#195) that is **still literally answered with `?`**.

**Could not confirm:**

- Whether the OOT `saveAssets` customization hook (community CLI PR #2002) survived the move of `bundle` into `@react-native/community-cli-plugin`.
- Exact RN release in which `@react-native-community/cli` was dropped from `react-native`'s `dependencies` (absent already at `0.85-stable`; earlier than the checked range).
- Whether the OpenHarmony mirror read here (`AuroraMaster/ohos_react_native`, pinned to RN 0.72.5) reflects the current upstream Gitee SIG repo — the official repo is on Gitee and not reachable via `gh api`.
- Current `@rnoh` RN version parity in 2026.

Note: the WebSearch budget (200 calls) was exhausted mid-session, so §2.3's visionOS/macOS findings and §5's template findings rest on `gh api` repo metadata and direct file reads rather than search corroboration — those are the strongest evidence available anyway.

---

## Sources

- [react-native 0.87-stable tree](https://github.com/facebook/react-native/tree/0.87-stable)
- [Using Codegen · React Native](https://reactnative.dev/docs/the-new-architecture/using-codegen)
- [Out-of-Tree Platforms · React Native](https://reactnative.dev/docs/out-of-tree-platforms)
- [React Native 0.87 release blog](https://reactnative.dev/blog/2026/08/11/react-native-0.87)
- [Metro configuration — resolver.platforms](https://metrobundler.dev/docs/configuration/#platforms)
- [Metro module resolution](https://metrobundler.dev/docs/resolution/)
- [@react-native-windows/codegen](https://github.com/microsoft/react-native-windows/tree/main/packages/@react-native-windows/codegen)
- [react-native-platform-override](https://github.com/microsoft/react-native-windows/tree/main/packages/react-native-platform-override)
- [react-native-community/cli cli-types](https://github.com/react-native-community/cli/blob/main/packages/cli-types/src/index.ts)
- [ohos_react_native (OpenHarmony mirror)](https://github.com/AuroraMaster/ohos_react_native)
- [react-native-macos](https://github.com/microsoft/react-native-macos)
- [react-native-visionos](https://github.com/callstack/react-native-visionos)
- [RN PR #42360 — OOT platform suffix](https://github.com/react/react-native/pull/42360)
- [RN PR #42047 — OOT Apple platforms](https://github.com/react/react-native/pull/42047)
- [discussions-and-proposals #195](https://github.com/react-native-community/discussions-and-proposals/issues/195)
- [CLI PR #2170 — --platform-name for OOT init](https://github.com/react-native-community/cli/pull/2170)
- [CLI PR #2002 — OOT saveAssets](https://github.com/react-native-community/cli/pull/2002)
- [react-native-template-typescript (deprecated)](https://github.com/react-native-community/react-native-template-typescript)
