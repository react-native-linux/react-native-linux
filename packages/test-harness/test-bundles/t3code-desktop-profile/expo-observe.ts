/**
 * The desktop feature profile's `expo-observe` (docs/research/t3code-linux.md §2): its native module, ExpoAppMetrics,
 * is left out, so a report goes to the console, which the e2e event trace records.
 */
const Observe = {
  configure: (): void => undefined,
  dispatchEvents: async (): Promise<void> => {
    await Promise.resolve();
  },
  reportError: (error: unknown): void => {
    console.error("[observe]", error);
  },
};

export { Observe };
