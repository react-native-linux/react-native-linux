const linuxPlatform = "linux";
const subpathSeparator = "/";
const packageNameStart = 0;
const scopedPackageNameEnd = 2;

interface PackageAlias {
  readonly linux: string;
  readonly upstream: string;
}

interface PackageAliasRequest {
  readonly aliases: readonly PackageAlias[];
  readonly isPackageResolvable: (packageName: string) => boolean;
  readonly moduleName: string;
  readonly platform: string | null;
}

/**
 * The overlay packages a `linux` bundle resolves instead of their upstream originals, decided in issue #96 and
 * shaped by issue #180: `packages/gesture-handler` (#168) and `packages/reanimated` (#181). `react-native-worklets`
 * is not aliased because no overlay package vendors it yet. Each Expo module maps to its shim inside
 * `packages/expo`, the JavaScript shim tier of ADR-0004. An alias whose package is not installed would rewrite a
 * working upstream import into an unresolvable one, which is why the rule also asks whether the linux package
 * resolves before it rewrites anything.
 */
const packageAliases: readonly PackageAlias[] = [
  { linux: "@react-native-linux/gesture-handler", upstream: "react-native-gesture-handler" },
  { linux: "@react-native-linux/reanimated", upstream: "react-native-reanimated" },
  { linux: "@react-native-linux/expo/expo-constants", upstream: "expo-constants" },
];

const linuxPackageName = (alias: PackageAlias): string =>
  alias.linux.split(subpathSeparator).slice(packageNameStart, scopedPackageNameEnd).join(subpathSeparator);

const matchesUpstreamPackage = (moduleName: string, alias: PackageAlias): boolean =>
  moduleName === alias.upstream || moduleName.startsWith(`${alias.upstream}${subpathSeparator}`);

const resolveLinuxPackageAlias = (request: PackageAliasRequest): string | null => {
  if (request.platform !== linuxPlatform) {
    return null;
  }

  const alias = request.aliases.find((candidate) => matchesUpstreamPackage(request.moduleName, candidate)) ?? null;

  if (alias === null || !request.isPackageResolvable(linuxPackageName(alias))) {
    return null;
  }

  return `${alias.linux}${request.moduleName.slice(alias.upstream.length)}`;
};

export { packageAliases, resolveLinuxPackageAlias };
