import { expect, it, vi } from "vitest";

vi.mock("react-native", () => ({ Platform: { OS: "linux" } }));

const sourceDirectory = "../upstream/packages/react-native-gesture-handler/src";
const modulePath = `${sourceDirectory}/RNGestureHandlerModule.linux.ts`;
const routerPath = `${sourceDirectory}/web/tools/LinuxPointerRouter.ts`;
const jsFunctionNewApi = 4;
const stateBegan = 2;
const stateFailed = 1;
const moduleIdWithoutNativeRegistry = -1;
const clickedHandlerTag = 1;
const detachedHandlerTag = 2;
const unknownHandlerTag = 3;
const refusedHandlerTag = 4;
const rect = { bottom: 100, left: 0, right: 100, top: 0 };
const element = { contains: (): boolean => true, getBoundingClientRect: (): typeof rect => rect };
const pointerEvent = { nativeEvent: { clientX: 10, clientY: 10, pointerId: 1 }, target: element, timeStamp: 1 };

type Method = (...methodArguments: unknown[]) => unknown;
type StateChangeMock = ReturnType<typeof vi.fn>;

const propertyOf = (owner: unknown, name: string): unknown =>
  typeof owner === "object" && owner !== null ? Reflect.get(owner, name) : null;

const methodOf = (owner: unknown, name: string): Method => {
  const method = propertyOf(owner, name);

  if (typeof method !== "function") {
    throw new TypeError(`${name} must be a function`);
  }

  return (...methodArguments) => Reflect.apply(method, owner, methodArguments);
};

const loadGestureModule = async (): Promise<unknown> => propertyOf(await import(modulePath), "default");

const loadRoute = async (): Promise<Method> => methodOf(await import(routerPath), "routeLinuxPointerEvent");

const statesOf = (stateChange: StateChangeMock): unknown[] =>
  stateChange.mock.calls.map(([event]: unknown[]) => propertyOf(propertyOf(event, "nativeEvent"), "state"));

const attachNativeRecognizer = (gestureModule: unknown, handlerTag: number, stateChange: StateChangeMock): void => {
  const propsRef = { current: { onGestureHandlerEvent: vi.fn(), onGestureHandlerStateChange: stateChange } };

  methodOf(gestureModule, "createGestureHandler")("NativeViewGestureHandler", handlerTag, {});
  methodOf(gestureModule, "attachGestureHandler")(handlerTag, element, jsFunctionNewApi, propsRef);
  methodOf(gestureModule, "updateGestureHandlerConfig")(handlerTag, { shouldCancelWhenOutside: true });
  methodOf(gestureModule, "configureRelations")(handlerTag, { simultaneousHandlers: [], waitFor: [] });
};

it("begins a Native recognizer on a Linux view and fails it on a click, through the 3.x module contract", async () => {
  const gestureModule = await loadGestureModule();
  const route = await loadRoute();
  const stateChange = vi.fn();

  attachNativeRecognizer(gestureModule, clickedHandlerTag, stateChange);
  route("pointerdown", pointerEvent);
  route("pointerup", pointerEvent);

  expect(statesOf(stateChange)).toStrictEqual([stateBegan, stateFailed]);
});

it("routes nothing to a detached recognizer and forgets a dropped one", async () => {
  const gestureModule = await loadGestureModule();
  const route = await loadRoute();
  const stateChange = vi.fn();
  attachNativeRecognizer(gestureModule, detachedHandlerTag, stateChange);
  methodOf(gestureModule, "detachGestureHandler")(detachedHandlerTag);
  route("pointerdown", pointerEvent);
  methodOf(gestureModule, "dropGestureHandler")(detachedHandlerTag);

  expect(stateChange).not.toHaveBeenCalled();
  expect(() => methodOf(gestureModule, "getGestureHandlerNode")(detachedHandlerTag)).toThrow(
    `No handler for tag ${detachedHandlerTag}`,
  );
});

it("answers the native module's queries without a native gesture registry", async () => {
  const gestureModule = await loadGestureModule();

  expect(methodOf(gestureModule, "configureRelations")(unknownHandlerTag, {})).toBeUndefined();
  expect(methodOf(gestureModule, "getModuleId")()).toBe(moduleIdWithoutNativeRegistry);
  expect(methodOf(gestureModule, "installUIRuntimeBindings")()).toBe(true);
  expect(methodOf(gestureModule, "flushOperations")()).toBeUndefined();
});

it("names the platform when asked for a recognizer the engine does not have", async () => {
  const gestureModule = await loadGestureModule();

  expect(() =>
    methodOf(gestureModule, "createGestureHandler")("ForceTouchGestureHandler", refusedHandlerTag, {}),
  ).toThrow("ForceTouchGestureHandler is not supported on linux");
});
