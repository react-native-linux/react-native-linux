import { describe, expect, it } from "vitest";
import { waitUntil } from "./wait-until.ts";

const SHORT_TIMEOUT_MS = 20;
const NEVER_READY = (): boolean => false;

describe("waitUntil", () => {
  it("resolves to true immediately when isReady is already true", async () => {
    await expect(waitUntil(() => true, SHORT_TIMEOUT_MS)).resolves.toBe(true);
  });

  it("resolves to true once isReady turns true within the timeout", async () => {
    let readyAfterFirstPoll = false;
    const isReady = (): boolean => {
      const wasReady = readyAfterFirstPoll;
      readyAfterFirstPoll = true;

      return wasReady;
    };

    await expect(waitUntil(isReady, SHORT_TIMEOUT_MS)).resolves.toBe(true);
  });

  it("resolves to false once the timeout elapses without isReady ever turning true", async () => {
    await expect(waitUntil(NEVER_READY, SHORT_TIMEOUT_MS)).resolves.toBe(false);
  });
});
