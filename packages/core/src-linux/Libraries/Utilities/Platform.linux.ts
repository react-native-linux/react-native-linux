import type { LinuxPlatform, PlatformSelectSpec } from "./PlatformTypes.ts";
import type { NativePlatformConstantsLinuxSpec, PlatformConstantsLinux } from "./NativePlatformConstantsLinux.ts";

const staticLinuxPlatformConstants: PlatformConstantsLinux = {
  isTesting: false,
  osVersion: "unknown",
  reactNativeVersion: { major: 0, minor: 0, patch: 0, prerelease: null },
};

const isNativePlatformConstantsLinux = (module: unknown): module is NativePlatformConstantsLinuxSpec =>
  typeof module === "object" &&
  module !== null &&
  "getConstants" in module &&
  typeof module.getConstants === "function";

/**
 * #23: the `PlatformConstants` TurboModule the host registers, which answers the React Native version it was built
 * from (a development bundle compares it with its own) and the kernel release. `null` where no host serves it, a
 * unit test for instance, so the static fallback answers instead.
 */
const readNativePlatformConstantsLinux = (): NativePlatformConstantsLinuxSpec | null => {
  const nativeModuleProxy: unknown = Reflect.get(globalThis, "nativeModuleProxy");
  const module: unknown =
    typeof nativeModuleProxy === "object" && nativeModuleProxy !== null
      ? Reflect.get(nativeModuleProxy, "PlatformConstants")
      : null;

  return isNativePlatformConstantsLinux(module) ? module : null;
};

const resolveLinuxPlatformConstants = (nativeModule: NativePlatformConstantsLinuxSpec | null): PlatformConstantsLinux =>
  nativeModule?.getConstants() ?? staticLinuxPlatformConstants;

let cachedLinuxPlatformConstants: PlatformConstantsLinux | null = null;

const readLinuxPlatformConstants = (): PlatformConstantsLinux => {
  if (cachedLinuxPlatformConstants === null) {
    cachedLinuxPlatformConstants = resolveLinuxPlatformConstants(readNativePlatformConstantsLinux());
  }

  return cachedLinuxPlatformConstants;
};

const resolveIsDisableAnimations = (constants: PlatformConstantsLinux): boolean =>
  constants.isDisableAnimations ?? constants.isTesting;

const selectLinuxPlatform = <SelectedValue>(spec: PlatformSelectSpec<SelectedValue>): SelectedValue => {
  if ("linux" in spec) {
    return spec.linux;
  }

  if ("native" in spec) {
    return spec.native;
  }

  return spec.default;
};

const Platform: LinuxPlatform = {
  OS: "linux",
  get Version(): string {
    return readLinuxPlatformConstants().osVersion;
  },
  get constants(): PlatformConstantsLinux {
    return readLinuxPlatformConstants();
  },
  get isDisableAnimations(): boolean {
    return resolveIsDisableAnimations(readLinuxPlatformConstants());
  },
  get isTV(): boolean {
    return false;
  },
  get isTesting(): boolean {
    return readLinuxPlatformConstants().isTesting;
  },
  get isVision(): boolean {
    return false;
  },
  select: selectLinuxPlatform,
};

export {
  Platform,
  readNativePlatformConstantsLinux,
  resolveIsDisableAnimations,
  resolveLinuxPlatformConstants,
  selectLinuxPlatform,
};
// React Native's own modules import this file as `import Platform from './Platform'` (#22).
export default Platform;
