/* oxlint-disable import/group-exports -- Each upstream module has its own export namespace. */
declare module "react-native/Libraries/Components/TextInput/AndroidTextInputNativeComponent" {
  const viewConfig: Readonly<Record<string, unknown>>;
  export { viewConfig as __INTERNAL_VIEW_CONFIG };
}

declare module "react-native/Libraries/NativeComponent/NativeComponentRegistry" {
  import type { HostComponent } from "react-native";

  export function get<Props extends object>(
    name: string,
    viewConfigProvider: () => Readonly<Record<string, unknown>>,
  ): HostComponent<Props>;
}
