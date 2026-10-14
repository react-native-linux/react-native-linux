const { getDefaultConfig, mergeConfig } = require("@react-native/metro-config");
const { linuxResolverConfig } = require("@react-native-linux/cli/linux-resolver-config.ts");

module.exports = mergeConfig(getDefaultConfig(__dirname), { resolver: linuxResolverConfig(__dirname) });
