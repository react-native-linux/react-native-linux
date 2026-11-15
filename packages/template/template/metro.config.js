const { getDefaultConfig, mergeConfig } = require("@react-native/metro-config");
const { babelCacheVersion } = require("@react-native-linux/cli/babel-cache-version.ts");
const { linuxResolverConfig } = require("@react-native-linux/cli/linux-resolver-config.ts");

module.exports = mergeConfig(getDefaultConfig(__dirname), {
  cacheVersion: babelCacheVersion(__dirname),
  resolver: linuxResolverConfig(__dirname),
});
