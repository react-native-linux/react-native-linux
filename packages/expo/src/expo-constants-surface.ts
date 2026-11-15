import type { NativeConstants } from "expo-constants";

type DeclaredConstants = {
  [Field in keyof NativeConstants as string extends Field ? never : Field]: NativeConstants[Field];
};

type RequiredField = {
  [Field in keyof DeclaredConstants]-?: object extends Pick<DeclaredConstants, Field> ? never : Field;
}[keyof DeclaredConstants];

/**
 * Every field SDK 58's `NativeConstants` declares required, kept complete by the type check: a field upstream adds
 * fails `pnpm ts` until it is listed here, and the `expo-constants` e2e then prints and asserts its value. `name`
 * is left out because upstream's own `Constants.ts` strips it from the object it exports, on every platform.
 */
const requiredConstantsSurface: Readonly<Record<Exclude<RequiredField, "name">, true>> = {
  appOwnership: true,
  debugMode: true,
  deviceYearClass: true,
  easConfig: true,
  executionEnvironment: true,
  experienceUrl: true,
  expoConfig: true,
  expoGoConfig: true,
  expoRuntimeVersion: true,
  expoVersion: true,
  getWebViewUserAgentAsync: true,
  isHeadless: true,
  linkingUri: true,
  manifest: true,
  manifest2: true,
  sessionId: true,
  statusBarHeight: true,
  systemFonts: true,
};

const requiredConstantsFields: readonly string[] = Object.keys(requiredConstantsSurface);

export { requiredConstantsFields };
