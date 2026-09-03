# linux/

The native Linux sources of `@react-native-linux/reanimated`, ours and never upstream's:

- C++ TurboModules implementing this library's native module specs.
- Fabric components: component descriptors, shadow nodes, and the Skia drawing of its views.
- `CMakeLists.txt`, the fragment that builds both into the platform.

`@react-native-linux/cli` registers this directory as the package's `sourceDir`
(`platformConfig.platforms.linux`), which is where autolinking looks for it. Native code that belongs to upstream
goes into a patch instead, so that the next bump replays it.
