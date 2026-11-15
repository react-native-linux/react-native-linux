/**
 * What `src-linux/expo-modules-core/polyfill/index.linux.ts` uses of expo-modules-core, mapped in by tsconfig.json's
 * `paths`: the package's own declarations augment `NodeJS.ProcessEnv` with a required `NODE_ENV` for the whole
 * program, which every script that builds a child-process environment then fails.
 */
declare global {
  var expo: {
    getViewConfig: (
      moduleName: string,
      viewName?: string,
    ) => {
      readonly directEventTypes: Readonly<Record<string, { readonly registrationName: string }>>;
      readonly validAttributes: Readonly<Record<string, unknown>>;
    } | null;
  };
}

export function installExpoGlobalPolyfill(): void;
