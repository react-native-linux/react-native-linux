const ExpoKeepAwakeTag = "ExpoKeepAwakeDefaultTag";

const keepNothingAwake = async (): Promise<void> => {
  await Promise.resolve();
};

/**
 * The desktop feature profile's `expo-keep-awake` (docs/research/t3code-linux.md §2): its native module,
 * ExpoKeepAwake, is left out with voice input, its only caller in T3 Code, so the calls resolve and keep nothing
 * awake. `useKeepAwake` and `ExpoKeepAwakeTag` answer Expo's own development `withDevTools`, which requires them once
 * the module resolves.
 */
const useKeepAwake = (): void => undefined;

export {
  ExpoKeepAwakeTag,
  keepNothingAwake as activateKeepAwakeAsync,
  keepNothingAwake as deactivateKeepAwake,
  useKeepAwake,
};
