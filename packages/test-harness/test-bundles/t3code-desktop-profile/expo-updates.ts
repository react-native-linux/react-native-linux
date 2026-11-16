class UpdatesDisabledError extends Error {
  readonly code = "ERR_UPDATES_DISABLED";
}

const rejectDisabled = async (): Promise<never> => {
  await Promise.resolve();
  throw new UpdatesDisabledError("Over-the-air updates are disabled on Linux, where the package manager updates the app");
};

/**
 * The desktop feature profile's `expo-updates` (docs/research/t3code-linux.md §2): a Linux build has no update channel,
 * so it answers as expo-updates does with updates disabled. `isEnabled` is false, which keeps T3 Code's launch and
 * About-screen update checks and its diagnostics log read from running, and the calls behind it reject with
 * `ERR_UPDATES_DISABLED`, the code T3 Code reads as "updates unavailable" rather than a failure.
 */
const isEnabled = false;

export {
  isEnabled,
  rejectDisabled as checkForUpdateAsync,
  rejectDisabled as fetchUpdateAsync,
  rejectDisabled as readLogEntriesAsync,
  rejectDisabled as reloadAsync,
};
