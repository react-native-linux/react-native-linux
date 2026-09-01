import type { PlatformConstantsLinux } from "./NativePlatformConstantsLinux.ts";

type PlatformOSType = "ios" | "android" | "macos" | "windows" | "web" | "linux" | "native";

type OptionalPlatformSelectSpec<SelectedValue> = Partial<Record<PlatformOSType, SelectedValue>>;

type PlatformSelectSpec<SelectedValue> = OptionalPlatformSelectSpec<SelectedValue> & {
  readonly default: SelectedValue;
};

interface LinuxPlatform {
  readonly OS: "linux";
  readonly Version: string;
  readonly constants: PlatformConstantsLinux;
  readonly isTV: boolean;
  readonly isVision: boolean;
  readonly isTesting: boolean;
  readonly isDisableAnimations: boolean;
  select: <SelectedValue>(spec: PlatformSelectSpec<SelectedValue>) => SelectedValue;
}

export type { LinuxPlatform, PlatformOSType, PlatformSelectSpec };
