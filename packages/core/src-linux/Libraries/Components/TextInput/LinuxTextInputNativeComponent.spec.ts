import { describe, expect, it, vi } from "vitest";

const { nativeComponent, viewConfig } = vi.hoisted(() => ({
  nativeComponent: vi.fn((name: string, provider: () => unknown) => ({ config: provider(), name })),
  viewConfig: { directEventTypes: {}, uiViewClassName: "AndroidTextInput", validAttributes: { text: true } },
}));

vi.mock("react-native/Libraries/Components/TextInput/AndroidTextInputNativeComponent", () => ({
  __INTERNAL_VIEW_CONFIG: viewConfig,
}));
vi.mock("react-native/Libraries/NativeComponent/NativeComponentRegistry", () => ({ get: nativeComponent }));

describe("LinuxTextInputNativeComponent", () => {
  it("registers the Linux host with the upstream TextInput props and events", async () => {
    const { default: component } = await import("./LinuxTextInputNativeComponent.linux.ts");

    expect(component).toStrictEqual({ config: { ...viewConfig, uiViewClassName: "TextInput" }, name: "TextInput" });
    expect(viewConfig.uiViewClassName).toBe("AndroidTextInput");
  });
});
