interface LinuxBundleManifest {
  readonly applicationIdentifier: string;
  readonly displayName: string;
  readonly version: string;
  readonly categories: readonly string[];
  readonly shortDescription: string;
  readonly longDescription: string;
  readonly homepage: string;
  readonly licence: string;
  readonly iconSourcePaths: readonly string[];
  readonly urlSchemes?: readonly string[];
  readonly fileAssociationMimeTypes?: readonly string[];
  readonly extraRuntimeDependencies?: readonly string[];
}

interface JsonSchemaFragment {
  readonly type: "string" | "array";
  readonly pattern?: string;
  readonly minItems?: number;
  readonly items?: { readonly type: "string"; readonly pattern?: string };
}

interface FieldDescriptor {
  readonly required: boolean;
  readonly jsonSchema: JsonSchemaFragment;
  readonly describeError: (value: unknown) => string | null;
}

const reverseDnsPattern = /^(?<firstSegment>[A-Za-z][A-Za-z0-9]*)(?:\.(?<nextSegment>[A-Za-z][A-Za-z0-9]*)){1,}$/u;
const semanticVersionPattern =
  /^(?<core>\d+\.\d+\.\d+)(?:-(?<prerelease>[0-9A-Za-z-.]+))?(?:\+(?<build>[0-9A-Za-z-.]+))?$/u;
const freedesktopCategoryPattern = /^[A-Za-z]+$/u;
const licenceIdentifierPattern = /^[A-Za-z0-9.+-]+$/u;
const urlSchemePattern = /^[a-z][a-z0-9+.-]*$/u;
const mimeTypePattern = /^[a-z0-9][a-z0-9!#$&^_.+-]*\/[a-z0-9][a-z0-9!#$&^_.+-]*$/iu;
const minimumArrayLength = 1;
const emptyLength = 0;
const noFieldErrors = 0;

const isString = (value: unknown): value is string => typeof value === "string";

const isStringArray = (value: unknown): value is readonly string[] =>
  Array.isArray(value) && value.every((item) => isString(item));

const nonEmptyStringError = (value: unknown): string | null =>
  isString(value) && value.length > emptyLength ? null : "must be a non-empty string";

const patternStringError = (pattern: RegExp, description: string) =>
  function describePatternStringError(value: unknown): string | null {
    if (!isString(value) || value.length === emptyLength) {
      return "must be a non-empty string";
    }
    return pattern.test(value) ? null : `must be ${description}`;
  };

const stringArrayError = (description: string, itemPattern?: RegExp) =>
  function describeStringArrayError(value: unknown): string | null {
    if (!isStringArray(value) || value.length < minimumArrayLength) {
      return `must be a non-empty array of strings (${description})`;
    }
    if (itemPattern instanceof RegExp && !value.every((item) => itemPattern.test(item))) {
      return `must be a non-empty array of strings (${description})`;
    }
    return null;
  };

const optionalStringArrayError = (description: string, itemPattern?: RegExp) =>
  function describeOptionalStringArrayError(value: unknown): string | null {
    if (!isStringArray(value)) {
      return `must be an array of strings (${description})`;
    }
    if (itemPattern instanceof RegExp && !value.every((item) => itemPattern.test(item))) {
      return `must be an array of strings (${description})`;
    }
    return null;
  };

const homepageError = (value: unknown): string | null => {
  if (!isString(value) || value.length === emptyLength) {
    return "must be a non-empty string";
  }
  try {
    const url = new URL(value);
    return url.protocol === "http:" || url.protocol === "https:" ? null : "must be an http or https URL";
  } catch {
    return "must be an absolute URL";
  }
};

const fieldDescriptors: Record<keyof LinuxBundleManifest, FieldDescriptor> = {
  applicationIdentifier: {
    describeError: patternStringError(reverseDnsPattern, "a reverse-DNS identifier, e.g. org.example.LinuxApp"),
    jsonSchema: { pattern: reverseDnsPattern.source, type: "string" },
    required: true,
  },
  categories: {
    describeError: stringArrayError("freedesktop main categories, e.g. Utility", freedesktopCategoryPattern),
    jsonSchema: {
      items: { pattern: freedesktopCategoryPattern.source, type: "string" },
      minItems: minimumArrayLength,
      type: "array",
    },
    required: true,
  },
  displayName: {
    describeError: nonEmptyStringError,
    jsonSchema: { type: "string" },
    required: true,
  },
  extraRuntimeDependencies: {
    describeError: optionalStringArrayError("package names required at runtime"),
    jsonSchema: { items: { type: "string" }, type: "array" },
    required: false,
  },
  fileAssociationMimeTypes: {
    describeError: optionalStringArrayError("MIME types, e.g. text/markdown", mimeTypePattern),
    jsonSchema: { items: { pattern: mimeTypePattern.source, type: "string" }, type: "array" },
    required: false,
  },
  homepage: {
    describeError: homepageError,
    jsonSchema: { type: "string" },
    required: true,
  },
  iconSourcePaths: {
    describeError: stringArrayError("paths to source PNG icons"),
    jsonSchema: { items: { type: "string" }, minItems: minimumArrayLength, type: "array" },
    required: true,
  },
  licence: {
    describeError: patternStringError(licenceIdentifierPattern, "an SPDX licence identifier, e.g. MIT"),
    jsonSchema: { pattern: licenceIdentifierPattern.source, type: "string" },
    required: true,
  },
  longDescription: {
    describeError: nonEmptyStringError,
    jsonSchema: { type: "string" },
    required: true,
  },
  shortDescription: {
    describeError: nonEmptyStringError,
    jsonSchema: { type: "string" },
    required: true,
  },
  urlSchemes: {
    describeError: optionalStringArrayError("URL scheme names, e.g. myapp", urlSchemePattern),
    jsonSchema: { items: { pattern: urlSchemePattern.source, type: "string" }, type: "array" },
    required: false,
  },
  version: {
    describeError: patternStringError(semanticVersionPattern, "a semantic version, e.g. 1.0.0"),
    jsonSchema: { pattern: semanticVersionPattern.source, type: "string" },
    required: true,
  },
};

const isFieldName = (name: string): name is keyof LinuxBundleManifest => Object.hasOwn(fieldDescriptors, name);

const fieldNames = Object.keys(fieldDescriptors).filter((name) => isFieldName(name));

class LinuxBundleManifestValidationError extends Error {
  public readonly fieldErrors: Readonly<Record<string, string>>;

  public constructor(fieldErrors: Readonly<Record<string, string>>) {
    const summary = Object.entries(fieldErrors)
      .map(([fieldName, message]) => `${fieldName}: ${message}`)
      .join("; ");
    super(`Linux bundle manifest is invalid — ${summary}`);
    this.name = "LinuxBundleManifestValidationError";
    this.fieldErrors = fieldErrors;
  }
}

const linuxBundleManifestJsonSchema = {
  $id: "https://react-native-linux.dev/schemas/linux-bundle-manifest.schema.json",
  additionalProperties: false,
  properties: Object.fromEntries(fieldNames.map((fieldName) => [fieldName, fieldDescriptors[fieldName].jsonSchema])),
  required: fieldNames.filter((fieldName) => fieldDescriptors[fieldName].required),
  title: "LinuxBundleManifest",
  type: "object",
} as const;

const readValidatedString = (value: unknown): string => {
  if (!isString(value)) {
    throw new Error("unreachable: field already validated as a string");
  }
  return value;
};

const readValidatedStringArray = (value: unknown): readonly string[] => {
  if (!isStringArray(value)) {
    throw new Error("unreachable: field already validated as a string array");
  }
  return value;
};

const collectUnrecognizedFieldErrors = (candidate: Readonly<Record<string, unknown>>): Record<string, string> => {
  const fieldErrors: Record<string, string> = {};

  for (const key of Object.keys(candidate)) {
    if (!isFieldName(key)) {
      fieldErrors[key] = "is not a recognized field";
    }
  }

  return fieldErrors;
};

const collectKnownFieldErrors = (candidate: Readonly<Record<string, unknown>>): Record<string, string> => {
  const fieldErrors: Record<string, string> = {};

  for (const fieldName of fieldNames) {
    const descriptor = fieldDescriptors[fieldName];
    const isPresent = Object.hasOwn(candidate, fieldName);

    if (isPresent) {
      const error = descriptor.describeError(candidate[fieldName]);
      if (error !== null) {
        fieldErrors[fieldName] = error;
      }
    } else if (descriptor.required) {
      fieldErrors[fieldName] = "is required";
    }
  }

  return fieldErrors;
};

const collectFieldErrors = (candidate: Readonly<Record<string, unknown>>): Record<string, string> => ({
  ...collectUnrecognizedFieldErrors(candidate),
  ...collectKnownFieldErrors(candidate),
});

type OptionalArrayFieldName = "extraRuntimeDependencies" | "fileAssociationMimeTypes" | "urlSchemes";

const readOptionalStringArrayField = (
  candidate: Readonly<Record<string, unknown>>,
  fieldName: OptionalArrayFieldName,
): Pick<LinuxBundleManifest, OptionalArrayFieldName> =>
  Object.hasOwn(candidate, fieldName) ? { [fieldName]: readValidatedStringArray(candidate[fieldName]) } : {};

const buildValidatedManifest = (candidate: Readonly<Record<string, unknown>>): LinuxBundleManifest => ({
  applicationIdentifier: readValidatedString(candidate["applicationIdentifier"]),
  categories: readValidatedStringArray(candidate["categories"]),
  displayName: readValidatedString(candidate["displayName"]),
  homepage: readValidatedString(candidate["homepage"]),
  iconSourcePaths: readValidatedStringArray(candidate["iconSourcePaths"]),
  licence: readValidatedString(candidate["licence"]),
  longDescription: readValidatedString(candidate["longDescription"]),
  shortDescription: readValidatedString(candidate["shortDescription"]),
  version: readValidatedString(candidate["version"]),
  ...readOptionalStringArrayField(candidate, "extraRuntimeDependencies"),
  ...readOptionalStringArrayField(candidate, "fileAssociationMimeTypes"),
  ...readOptionalStringArrayField(candidate, "urlSchemes"),
});

const validateLinuxBundleManifest = (candidate: Readonly<Record<string, unknown>>): LinuxBundleManifest => {
  const fieldErrors = collectFieldErrors(candidate);

  if (Object.keys(fieldErrors).length > noFieldErrors) {
    throw new LinuxBundleManifestValidationError(fieldErrors);
  }

  return buildValidatedManifest(candidate);
};

export type { LinuxBundleManifest };
export {
  LinuxBundleManifestValidationError,
  linuxBundleManifestJsonSchema,
  readValidatedString,
  readValidatedStringArray,
  validateLinuxBundleManifest,
};
