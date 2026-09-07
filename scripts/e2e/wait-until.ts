import { setTimeout as delay } from "node:timers/promises";

const POLL_INTERVAL_MS = 50;

/**
 * Polls `isReady` until it returns true or `timeoutMilliseconds` elapses, instead of a fixed sleep: every wait in
 * the e2e driver — the wayland socket, the bundle's ready line, the window's first-presented-frame line, keyboard
 * focus, the compositor's stdio closing — is bounded this same way, so a slow CI runner gets more time rather
 * than a flake.
 */
const waitUntil = async (isReady: () => boolean, timeoutMilliseconds: number): Promise<boolean> => {
  const deadline = Date.now() + timeoutMilliseconds;

  while (!isReady()) {
    if (Date.now() > deadline) {
      return false;
    }

    await delay(POLL_INTERVAL_MS);
  }

  return true;
};

export { waitUntil };
