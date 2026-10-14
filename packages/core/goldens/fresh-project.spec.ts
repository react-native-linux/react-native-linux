import { describe, expect, it } from "vitest";
import { existsSync, readFileSync } from "node:fs";
import { PNG } from "pngjs";
import { compareImagesPerceptually } from "./perceptual-diff.ts";
import path from "node:path";

const renderedPath = path.join(import.meta.dirname, "..", "..", "..", "build", "fresh-project", "fresh-project.png");
const goldenPath = path.join(import.meta.dirname, "fresh-project.png");

const decodePng = (filePath: string): PNG => PNG.sync.read(readFileSync(filePath));

/** #57: `pnpm test:fresh-project` renders the scaffolded app; CI runs it first, so a missing render fails there. */
describe.skipIf(!existsSync(renderedPath))("fresh project", () => {
  it("renders the template's first screen exactly as fresh-project.png", () => {
    expect(compareImagesPerceptually(decodePng(renderedPath), decodePng(goldenPath))).toBeNull();
  });
});
