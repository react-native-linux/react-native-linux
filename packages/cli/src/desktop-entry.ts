interface DesktopEntryManifest {
  readonly applicationIdentifier: string;
  readonly displayName: string;
  readonly comment?: string;
  readonly exec: string;
  readonly icon: string;
  readonly categories: readonly string[];
  readonly urlSchemes?: readonly string[];
  readonly fileAssociationMimeTypes?: readonly string[];
}

interface GeneratedDesktopEntry {
  readonly basename: string;
  readonly contents: string;
}

class InvalidIconValueError extends Error {
  public constructor(iconValue: string) {
    super(`Icon value "${iconValue}" is neither an absolute path nor a themed icon name without a path separator`);
    this.name = "InvalidIconValueError";
  }
}

const escapeDesktopEntryValue = (value: string): string =>
  value
    .replaceAll("\\", String.raw`\\`)
    .replaceAll("\n", String.raw`\n`)
    .replaceAll("\t", String.raw`\t`)
    .replaceAll("\r", String.raw`\r`);

const escapeDesktopEntryListItem = (item: string): string =>
  escapeDesktopEntryValue(item).replaceAll(";", String.raw`\;`);

const emptyLength = 0;

const isNonEmpty = (items: readonly string[]): boolean => items.length > emptyLength;

const joinDesktopEntryList = (items: readonly string[]): string =>
  `${items.map((item) => escapeDesktopEntryListItem(item)).join(";")};`;

const executableNeedsQuoting = /[\s"'\\><~|&;$*?#()`]/u;

const quoteExecutable = (exec: string): string => {
  if (!executableNeedsQuoting.test(exec)) {
    return exec;
  }
  const escaped = exec
    .replaceAll("\\", String.raw`\\`)
    .replaceAll('"', String.raw`\"`)
    .replaceAll("$", String.raw`\$`)
    .replaceAll("`", String.raw`\``);
  return `"${escaped}"`;
};

const validateIconValue = (icon: string): string => {
  if (icon.startsWith("/")) {
    return icon;
  }
  if (icon !== "" && !icon.includes("/")) {
    return icon;
  }
  throw new InvalidIconValueError(icon);
};

const resolveMimeTypesAndFieldCode = (
  urlSchemes: readonly string[] | undefined,
  fileAssociationMimeTypes: readonly string[] | undefined,
): { readonly mimeTypes: readonly string[]; readonly fieldCode: "" | " %f" | " %u" } => {
  const schemeMimeTypes = (urlSchemes ?? []).map((scheme) => `x-scheme-handler/${scheme}`);
  const associationMimeTypes = fileAssociationMimeTypes ?? [];
  const mimeTypes = [...schemeMimeTypes, ...associationMimeTypes];
  if (isNonEmpty(schemeMimeTypes)) {
    return { fieldCode: " %u", mimeTypes };
  }
  if (isNonEmpty(associationMimeTypes)) {
    return { fieldCode: " %f", mimeTypes };
  }
  return { fieldCode: "", mimeTypes };
};

const generateDesktopEntry = (manifest: DesktopEntryManifest): GeneratedDesktopEntry => {
  const icon = validateIconValue(manifest.icon);
  const { fieldCode, mimeTypes } = resolveMimeTypesAndFieldCode(manifest.urlSchemes, manifest.fileAssociationMimeTypes);
  const { comment } = manifest;

  const lines = [
    "[Desktop Entry]",
    "Type=Application",
    `Name=${escapeDesktopEntryValue(manifest.displayName)}`,
    ...(typeof comment === "string" ? [`Comment=${escapeDesktopEntryValue(comment)}`] : []),
    `Exec=${quoteExecutable(manifest.exec)}${fieldCode}`,
    `Icon=${icon}`,
    `Categories=${joinDesktopEntryList(manifest.categories)}`,
    "Terminal=false",
    `StartupWMClass=${manifest.applicationIdentifier}`,
    ...(isNonEmpty(mimeTypes) ? [`MimeType=${joinDesktopEntryList(mimeTypes)}`] : []),
  ];

  return {
    basename: `${manifest.applicationIdentifier}.desktop`,
    contents: `${lines.join("\n")}\n`,
  };
};

export type { DesktopEntryManifest, GeneratedDesktopEntry };
export { InvalidIconValueError, generateDesktopEntry };
