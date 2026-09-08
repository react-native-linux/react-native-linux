import { linuxBundleManifestJsonSchema } from "./linux-bundle-manifest.ts";

const jsonIndentSpaces = 2;

const renderManifestSchemaJson = (): string =>
  `${JSON.stringify(linuxBundleManifestJsonSchema, null, jsonIndentSpaces)}\n`;

const describeManifestSchemaDrift = (onDiskContents: string | null): string | null =>
  onDiskContents === renderManifestSchemaJson()
    ? null
    : "packages/cli/schemas/linux-bundle-manifest.schema.json is stale. Regenerate it with: pnpm manifest-schema:generate";

export { describeManifestSchemaDrift, renderManifestSchemaJson };
