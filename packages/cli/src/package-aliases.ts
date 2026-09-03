const linuxPlatform = "linux";
const subpathSeparator = "/";

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
 * shaped by issue #180. It is empty until the first overlay package ships: an alias whose package is not installed
 * would rewrite a working upstream import into an unresolvable one, which is why the rule also asks whether the
 * linux package resolves before it rewrites anything.
 */
const packageAliases: readonly PackageAlias[] = [];

const matchesUpstreamPackage = (moduleName: string, alias: PackageAlias): boolean =>
  moduleName === alias.upstream || moduleName.startsWith(`${alias.upstream}${subpathSeparator}`);

const resolveLinuxPackageAlias = (request: PackageAliasRequest): string | null => {
  if (request.platform !== linuxPlatform) {
    return null;
  }

  const alias = request.aliases.find((candidate) => matchesUpstreamPackage(request.moduleName, candidate)) ?? null;

  if (alias === null || !request.isPackageResolvable(alias.linux)) {
    return null;
  }

  return `${alias.linux}${request.moduleName.slice(alias.upstream.length)}`;
};

export { packageAliases, resolveLinuxPackageAlias };
