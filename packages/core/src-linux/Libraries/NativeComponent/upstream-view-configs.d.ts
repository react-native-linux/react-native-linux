// The two upstream Flow modules BaseViewConfig.linux.ts merges, typed as the shape it reads from them.
interface UpstreamPartialViewConfig {
  readonly bubblingEventTypes?: Readonly<Record<string, unknown>>;
  readonly directEventTypes?: Readonly<Record<string, unknown>>;
  readonly validAttributes?: Readonly<Record<string, unknown>>;
}

declare module "react-native/Libraries/NativeComponent/BaseViewConfig.android" {
  const config: UpstreamPartialViewConfig;
  export default config;
}

declare module "react-native/Libraries/NativeComponent/BaseViewConfig.ios" {
  const config: UpstreamPartialViewConfig;
  export default config;
}
