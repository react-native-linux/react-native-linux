interface AccelerometerMeasurement {
  readonly timestamp: number;
  readonly x: number;
  readonly y: number;
  readonly z: number;
}

/**
 * The desktop feature profile's `expo-sensors` (docs/research/t3code-linux.md §2): a desktop has no accelerometer and
 * its native modules are left out, so the sensor reads unavailable and a listener never receives a sample. T3 Code's
 * device preview subscribes to toggle its controls on a shake, which never fires, so the controls stay as they are.
 */
const Accelerometer = {
  addListener: (_listener: (measurement: AccelerometerMeasurement) => void): { readonly remove: () => void } => ({
    remove: (): void => undefined,
  }),
  isAvailableAsync: async (): Promise<boolean> => {
    await Promise.resolve();

    return false;
  },
  setUpdateInterval: (_intervalMs: number): void => undefined,
};

export { Accelerometer };
