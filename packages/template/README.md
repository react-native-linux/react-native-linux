# @react-native-linux/template

The init template for a React Native application on Linux (#22). Our packages are not on npm yet
(ADR-0001 §10), so an application installs them from the tarballs `pnpm pack:local` writes to `build/pack`
(#626), and runs the prebuilt `rnl_window` inside `@react-native-linux/core`.

```bash
# in this repository, with a release build
cmake --preset release && cmake --build build/release --target rnl_window
pnpm --filter @react-native-linux/core vendor:fonts
pnpm pack:local

# anywhere else
npx @react-native-community/cli init MyApp --template file:///path/to/react-native-linux/packages/template --skip-install
cd MyApp
pnpm add -D /path/to/react-native-linux/build/pack/react-native-linux-cli-0.0.0.tgz \
  /path/to/react-native-linux/build/pack/react-native-linux-core-0.0.0.tgz
pnpm linux
```

`pnpm linux` bundles `index.tsx` with Metro for `--platform linux` and opens it in `rnl_window`. The binary links
the build host's system libraries, so build it on the distribution the application runs on (#357).

`template.config.js` and `metro.config.js` are plain JavaScript because the community CLI and Metro load them
that way; AGENTS.md names this as the one exception to the TypeScript rule. `packages/cli/src/init-template.spec.ts` pins the
template's dependencies to the versions the harness app is tested with.
