import type { AppOwnership, ExecutionEnvironment, UserInterfaceIdiom } from "./expo-constants.linux.ts";
import { afterEach, describe, expect, it, vi } from "vitest";
import type Constants from "./expo-constants.linux.ts";

interface ConstantsModule {
  readonly AppOwnership: typeof AppOwnership;
  readonly ExecutionEnvironment: typeof ExecutionEnvironment;
  readonly UserInterfaceIdiom: typeof UserInterfaceIdiom;
  readonly default: typeof Constants;
}

const NO_STATUS_BAR_HEIGHT = 0;

const importConstants = async (appManifest?: string): Promise<ConstantsModule> => {
  vi.resetModules();
  if (typeof appManifest === "string") {
    vi.stubEnv("APP_MANIFEST", appManifest);
  }

  return await import("./expo-constants.linux.ts");
};

afterEach(() => {
  vi.unstubAllEnvs();
});

describe("the app config of expo-constants on Linux", () => {
  it("serves the inlined app config as the manifest and the Expo config", async () => {
    const appConfig = { extra: { appVariant: "production" }, scheme: "t3code", version: "1.2.3" };
    const { default: Constants } = await importConstants(JSON.stringify(appConfig));

    expect(Constants.expoConfig).toStrictEqual(appConfig);
    expect(Constants.manifest).toStrictEqual(appConfig);
    expect(Constants.expoGoConfig).toStrictEqual(appConfig);
    expect(Constants.easConfig).toStrictEqual(appConfig);
    expect(Constants.manifest2).toBeNull();
  });

  it("answers null when no app config was inlined, as upstream does in a bare app without its build script", async () => {
    const { default: Constants } = await importConstants();

    expect(Constants.expoConfig).toBeNull();
    expect(Constants.manifest).toBeNull();
  });

  it("answers null when the inlined app config is not an object", async () => {
    const { default: Constants } = await importConstants("[]");

    expect(Constants.expoConfig).toBeNull();
  });

  it("rejects an inlined app config that is not JSON, as upstream's JSON.parse does", async () => {
    await expect(importConstants("{")).rejects.toThrow(SyntaxError);
  });
});

describe("the device constants of expo-constants on Linux", () => {
  it("reports a bare desktop app with no status bar, system font list or web view", async () => {
    const { default: Constants, ExecutionEnvironment } = await importConstants();

    expect(Constants.appOwnership).toBeNull();
    expect(Constants.executionEnvironment).toBe(ExecutionEnvironment.Bare);
    expect(Constants.statusBarHeight).toBe(NO_STATUS_BAR_HEIGHT);
    expect(Constants.systemFonts).toStrictEqual([]);
    expect(Constants).not.toHaveProperty("deviceName");
    await expect(Constants.getWebViewUserAgentAsync()).resolves.toBeNull();
  });

  it("identifies the launch with a session id in upstream's web format", async () => {
    const { default: Constants } = await importConstants();

    expect(Constants.sessionId).toMatch(/^\d+-\d+$/u);
  });

  it("exports upstream's enum values", async () => {
    const { AppOwnership, ExecutionEnvironment, UserInterfaceIdiom } = await importConstants();

    expect(AppOwnership).toStrictEqual({ Expo: "expo" });
    expect(ExecutionEnvironment).toStrictEqual({ Bare: "bare", Standalone: "standalone", StoreClient: "storeClient" });
    expect(UserInterfaceIdiom).toStrictEqual({
      Desktop: "desktop",
      Handset: "handset",
      TV: "tv",
      Tablet: "tablet",
      Unsupported: "unsupported",
    });
  });
});
