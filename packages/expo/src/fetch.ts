/*
 * ADR-0004's escape hatch for `expo`'s `fetch` until #673. `Expo.fx` reads `Headers` before it installs its `fetch`,
 * which evaluates this React Native module first, so the global `fetch`, `Headers`, `Request` and `Response` stay one
 * implementation.
 */
export { fetch } from "react-native/Libraries/Network/fetch";
