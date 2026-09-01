interface ReactNativeVersionLinux {
  readonly major: number;
  readonly minor: number;
  readonly patch: number;
  readonly prerelease: string | null;
}

interface PlatformConstantsLinux {
  readonly isTesting: boolean;
  readonly isDisableAnimations?: boolean;
  readonly reactNativeVersion: ReactNativeVersionLinux;
  readonly osVersion: string;
}

interface NativePlatformConstantsLinuxSpec {
  readonly getConstants: () => PlatformConstantsLinux;
}

export type { NativePlatformConstantsLinuxSpec, PlatformConstantsLinux };
