import { describe, expect, it, vi } from "vitest";

const { reactNativeFetch } = vi.hoisted(() => ({ reactNativeFetch: vi.fn() }));

vi.mock("react-native/Libraries/Network/fetch", () => ({ fetch: reactNativeFetch }));

describe("expo's fetch on Linux", () => {
  it("is React Native's own fetch", async () => {
    const { fetch } = await import("./fetch.ts");

    expect(fetch).toBe(reactNativeFetch);
  });
});
