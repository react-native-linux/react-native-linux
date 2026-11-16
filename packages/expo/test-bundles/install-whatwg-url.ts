import { URL } from "whatwg-url-minimum";

// `expo`'s runtime installs this URL in an app; expo-file-system's `Paths.join` needs it, not React Native's.
Object.defineProperty(globalThis, "URL", { value: URL, configurable: true, writable: true });
