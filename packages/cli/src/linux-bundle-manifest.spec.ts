import {
  LinuxBundleManifestValidationError,
  linuxBundleManifestJsonSchema,
  readValidatedString,
  readValidatedStringArray,
  validateLinuxBundleManifest,
} from "./linux-bundle-manifest.ts";
import { describe, expect, it } from "vitest";

const validManifest: Readonly<Record<string, unknown>> = {
  applicationIdentifier: "org.example.LinuxApp",
  categories: ["Utility"],
  displayName: "Linux App",
  homepage: "https://example.org",
  iconSourcePaths: ["assets/icon.png"],
  licence: "MIT",
  longDescription: "A longer description of what the application does.",
  shortDescription: "A short description.",
  version: "1.0.0",
};

interface ManifestTableCase {
  readonly name: string;
  readonly manifest: Readonly<Record<string, unknown>>;
  readonly invalidField: string | null;
}

const withoutField = (fieldName: string): Readonly<Record<string, unknown>> =>
  Object.fromEntries(Object.entries(validManifest).filter(([key]) => key !== fieldName));

const manifestTable: readonly ManifestTableCase[] = [
  { invalidField: null, manifest: validManifest, name: "a valid manifest" },
  {
    invalidField: "applicationIdentifier",
    manifest: withoutField("applicationIdentifier"),
    name: "a missing identifier",
  },
  {
    invalidField: "applicationIdentifier",
    manifest: { ...validManifest, applicationIdentifier: "not-reverse-dns" },
    name: "an identifier that is not reverse-DNS",
  },
  {
    invalidField: null,
    manifest: { ...validManifest, displayName: "Linux App Two" },
    name: "a display name with a space",
  },
  {
    invalidField: null,
    manifest: { ...validManifest, displayName: "Café Linux App" },
    name: "a display name with non-ASCII characters",
  },
  {
    invalidField: "unknownField",
    manifest: { ...validManifest, unknownField: "surprise" },
    name: "an unknown key",
  },
  {
    invalidField: "version",
    manifest: { ...validManifest, version: "1.0" },
    name: "a version that is not semantic",
  },
  {
    invalidField: "categories",
    manifest: { ...validManifest, categories: [] },
    name: "an empty categories array",
  },
  {
    invalidField: "categories",
    manifest: { ...validManifest, categories: ["Not Valid!"] },
    name: "a category with characters outside the freedesktop pattern",
  },
  {
    invalidField: "licence",
    manifest: { ...validManifest, licence: "" },
    name: "an empty licence",
  },
  {
    invalidField: "homepage",
    manifest: { ...validManifest, homepage: "not a url" },
    name: "a homepage that is not an absolute URL",
  },
  {
    invalidField: "homepage",
    manifest: { ...validManifest, homepage: "" },
    name: "an empty homepage",
  },
  {
    invalidField: "homepage",
    manifest: { ...validManifest, homepage: "ftp://example.org" },
    name: "a homepage that is not http or https",
  },
  {
    invalidField: "displayName",
    manifest: { ...validManifest, displayName: "" },
    name: "an empty display name",
  },
  {
    invalidField: "urlSchemes",
    manifest: { ...validManifest, urlSchemes: ["Not Valid"] },
    name: "a URL scheme with invalid characters",
  },
  {
    invalidField: "urlSchemes",
    manifest: { ...validManifest, urlSchemes: "linuxapp" },
    name: "a URL schemes value that is not an array",
  },
];

describe("validateLinuxBundleManifest", () => {
  it.each(manifestTable)("$name", ({ invalidField, manifest }) => {
    if (invalidField === null) {
      expect(() => validateLinuxBundleManifest(manifest)).not.toThrow();
      return;
    }

    try {
      validateLinuxBundleManifest(manifest);
      expect.unreachable("expected validation to throw");
    } catch (error) {
      if (!(error instanceof LinuxBundleManifestValidationError)) {
        throw error;
      }
      expect(Object.keys(error.fieldErrors)).toContain(invalidField);
    }
  });

  it("returns a manifest that carries every optional field when present", () => {
    const manifest = validateLinuxBundleManifest({
      ...validManifest,
      extraRuntimeDependencies: ["libnotify"],
      fileAssociationMimeTypes: ["text/markdown"],
      urlSchemes: ["linuxapp"],
    });

    expect(manifest.extraRuntimeDependencies).toStrictEqual(["libnotify"]);
    expect(manifest.fileAssociationMimeTypes).toStrictEqual(["text/markdown"]);
    expect(manifest.urlSchemes).toStrictEqual(["linuxapp"]);
  });

  it("omits optional fields entirely when absent", () => {
    const manifest = validateLinuxBundleManifest(validManifest);

    expect(manifest.extraRuntimeDependencies).toBeUndefined();
    expect(manifest.fileAssociationMimeTypes).toBeUndefined();
    expect(manifest.urlSchemes).toBeUndefined();
  });
});

describe("readValidatedString", () => {
  it("returns the value when it is a string", () => {
    expect(readValidatedString("org.example.LinuxApp")).toBe("org.example.LinuxApp");
  });

  it("throws when the invariant that the field was already validated does not hold", () => {
    const notAString = 123;

    expect(() => readValidatedString(notAString)).toThrow();
  });
});

describe("readValidatedStringArray", () => {
  it("returns the value when it is a string array", () => {
    expect(readValidatedStringArray(["Utility"])).toStrictEqual(["Utility"]);
  });

  it("throws when the invariant that the field was already validated does not hold", () => {
    expect(() => readValidatedStringArray("Utility")).toThrow();
  });
});

describe("linuxBundleManifestJsonSchema", () => {
  it("requires every non-optional field and forbids unknown properties", () => {
    expect(linuxBundleManifestJsonSchema.additionalProperties).toBe(false);
    expect(linuxBundleManifestJsonSchema.required).toStrictEqual([
      "applicationIdentifier",
      "categories",
      "displayName",
      "homepage",
      "iconSourcePaths",
      "licence",
      "longDescription",
      "shortDescription",
      "version",
    ]);
  });

  it("does not require the optional fields", () => {
    expect(linuxBundleManifestJsonSchema.required).not.toContain("urlSchemes");
    expect(linuxBundleManifestJsonSchema.required).not.toContain("fileAssociationMimeTypes");
    expect(linuxBundleManifestJsonSchema.required).not.toContain("extraRuntimeDependencies");
  });
});
