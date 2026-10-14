import { describe, expect, it } from "vitest";
import { mkdirSync, mkdtempSync, rmSync, writeFileSync } from "node:fs";
import { linuxResolverConfig } from "./linux-resolver-config.ts";
import path from "node:path";
import { tmpdir } from "node:os";

const requestedFor = (projectRoot: string): string => {
  const requested: string[] = [];
  const context = {
    resolveRequest: (_context: unknown, name: string): unknown => {
      requested.push(name);

      return null;
    },
  };

  linuxResolverConfig(projectRoot).resolveRequest(context, "react-native-gesture-handler", "linux");

  return requested.join(",");
};

describe("linuxResolverConfig", () => {
  it("looks up packages in the application's own node_modules", () => {
    const projectRoot = mkdtempSync(path.join(tmpdir(), "rnl-resolver-"));

    expect(linuxResolverConfig(projectRoot)).toMatchObject({
      nodeModulesPaths: [path.join(projectRoot, "node_modules")],
      platforms: ["linux", "android", "ios"],
    });
    expect(requestedFor(projectRoot)).toBe("react-native-gesture-handler");

    const aliasDirectory = path.join(projectRoot, "node_modules", "@react-native-linux", "gesture-handler");

    mkdirSync(aliasDirectory, { recursive: true });
    writeFileSync(path.join(aliasDirectory, "package.json"), "{}");

    expect(requestedFor(projectRoot)).toBe("@react-native-linux/gesture-handler");
    rmSync(projectRoot, { force: true, recursive: true });
  });
});
