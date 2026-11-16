import { afterEach, describe, expect, it, vi } from "vitest";
import { NitroModules } from "./react-native-nitro-modules.ts";
import type { TurboModule } from "react-native";

const { registeredModules } = vi.hoisted(() => ({ registeredModules: new Map<string, object>() }));

vi.mock("react-native", () => ({
  TurboModuleRegistry: {
    get: (name: string): object | null => registeredModules.get(name) ?? null,
    getEnforcing: (name: string): object => {
      const registered = registeredModules.get(name) ?? null;

      if (registered === null) {
        throw new Error(`TurboModuleRegistry.getEnforcing(...): '${name}' could not be found.`);
      }

      return registered;
    },
  },
}));

/** The part of T3 Code's `T3Highlights` that `lib/highlights.ts` calls. */
interface Highlights extends TurboModule {
  readonly tokenize: (code: string, languageId: number) => ArrayBuffer;
}

const HIGHLIGHTS = "T3Highlights";
const SOURCE = "const greeting = 1;\n";
const TSX_LANGUAGE_ID = 139;
const RECORD_BYTES = 8;

afterEach(() => {
  registeredModules.clear();
});

describe("the react-native-nitro-modules stand-in", () => {
  it("answers a registered T3Highlights with the TurboModule, whose tokenize is called through synchronously", () => {
    const records = new ArrayBuffer(RECORD_BYTES);
    const tokenize = vi.fn((_code: string, _languageId: number): ArrayBuffer => records);
    const highlights: Highlights = { tokenize };
    registeredModules.set(HIGHLIGHTS, highlights);

    const created = NitroModules.createHybridObject<Highlights>(HIGHLIGHTS);

    expect([NitroModules.hasHybridObject(HIGHLIGHTS), created]).toStrictEqual([true, highlights]);
    expect(created.tokenize(SOURCE, TSX_LANGUAGE_ID)).toBe(records);
    expect(tokenize).toHaveBeenCalledWith(SOURCE, TSX_LANGUAGE_ID);
  });

  it("reports a module nobody registered as absent", () => {
    expect(NitroModules.hasHybridObject(HIGHLIGHTS)).toBe(false);
  });

  it("throws getEnforcing's error when asked to create a module nobody registered", () => {
    expect(() => NitroModules.createHybridObject(HIGHLIGHTS)).toThrow(`'${HIGHLIGHTS}' could not be found`);
  });
});
