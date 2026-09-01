import type { LinuxPlatform, PlatformSelectSpec } from "./PlatformTypes.ts";
import type { NativePlatformConstantsLinuxSpec, PlatformConstantsLinux } from "./NativePlatformConstantsLinux.ts";

const staticLinuxPlatformConstants: PlatformConstantsLinux = {
  isTesting: false,
  osVersion: "unknown",
  reactNativeVersion: { major: 0, minor: 0, patch: 0, prerelease: null },
};

const nativePlatformConstantsLinux: NativePlatformConstantsLinuxSpec | null = null;

const resolveLinuxPlatformConstants = (nativeModule: NativePlatformConstantsLinuxSpec | null): PlatformConstantsLinux =>
  nativeModule?.getConstants() ?? staticLinuxPlatformConstants;

let cachedLinuxPlatformConstants: PlatformConstantsLinux | null = null;

const readLinuxPlatformConstants = (): PlatformConstantsLinux => {
  if (cachedLinuxPlatformConstants === null) {
    cachedLinuxPlatformConstants = resolveLinuxPlatformConstants(nativePlatformConstantsLinux);
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

export { Platform, resolveIsDisableAnimations, resolveLinuxPlatformConstants, selectLinuxPlatform };
