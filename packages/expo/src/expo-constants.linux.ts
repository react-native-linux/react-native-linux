type ExpoConfig = Readonly<Record<string, unknown>>;

const AppOwnership = { Expo: "expo" } as const;

const ExecutionEnvironment = { Bare: "bare", Standalone: "standalone", StoreClient: "storeClient" } as const;

const UserInterfaceIdiom = {
  Desktop: "desktop",
  Handset: "handset",
  TV: "tv",
  Tablet: "tablet",
  Unsupported: "unsupported",
} as const;

const SESSION_SUFFIX_RANGE = 1_000_000_000;

const isExpoConfig = (value: unknown): value is ExpoConfig =>
  typeof value === "object" && value !== null && !Array.isArray(value);

const parseAppManifest = (appManifest?: string): ExpoConfig | null => {
  if (typeof appManifest !== "string") {
    return null;
  }

  const parsed: unknown = JSON.parse(appManifest);

  return isExpoConfig(parsed) ? parsed : null;
};

// oxlint-disable-next-line node/no-process-env -- Upstream's build-time contract for the public app config, read by https://github.com/expo/expo/blob/main/packages/expo-constants/src/ExponentConstants.web.ts
const { APP_MANIFEST: appManifest } = process.env;
const expoConfig = parseAppManifest(appManifest);

/**
 * The constants the Android module serves in a bare app, answered for a desktop window: no status bar, no
 * device name or system font list without a native module behind them, and no web view.
 */
const Constants = {
  appOwnership: null,
  easConfig: expoConfig,
  executionEnvironment: ExecutionEnvironment.Bare,
  expoConfig,
  expoGoConfig: expoConfig,
  getWebViewUserAgentAsync: (): Promise<null> => Promise.resolve(null),
  manifest: expoConfig,
  manifest2: null,
  sessionId: `${Date.now()}-${Math.floor(Math.random() * SESSION_SUFFIX_RANGE)}`,
  statusBarHeight: 0,
  systemFonts: [],
};

export { AppOwnership, ExecutionEnvironment, UserInterfaceIdiom };
export default Constants;
