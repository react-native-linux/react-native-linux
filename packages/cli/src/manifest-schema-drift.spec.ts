import { describe, expect, it } from "vitest";
import { describeManifestSchemaDrift, renderManifestSchemaJson } from "./manifest-schema-drift.ts";

describe("renderManifestSchemaJson", () => {
  it("renders the manifest JSON schema as formatted JSON ending in a newline", () => {
    const rendered = renderManifestSchemaJson();

    expect(rendered.endsWith("\n")).toBe(true);
    expect(JSON.parse(rendered)).toMatchObject({ title: "LinuxBundleManifest", type: "object" });
  });
});

describe("describeManifestSchemaDrift", () => {
  it("reports no drift when the on-disk contents match the rendered schema", () => {
    expect(describeManifestSchemaDrift(renderManifestSchemaJson())).toBeNull();
  });

  it("reports drift when the on-disk contents differ from the rendered schema", () => {
    expect(describeManifestSchemaDrift("{}")).toBe(
      "packages/cli/schemas/linux-bundle-manifest.schema.json is stale. Regenerate it with: pnpm manifest-schema:generate",
    );
  });

  it("reports drift when nothing is on disk yet", () => {
    expect(describeManifestSchemaDrift(null)).not.toBeNull();
  });
});
