// The upstream Flow module fetch.ts re-exports, typed as what it passes through untouched.
declare module "react-native/Libraries/Network/fetch" {
  export const fetch: unknown;
}
