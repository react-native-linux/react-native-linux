interface PartialViewConfig {
  readonly bubblingEventTypes?: Readonly<Record<string, unknown>>;
  readonly directEventTypes?: Readonly<Record<string, unknown>>;
  readonly validAttributes?: Readonly<Record<string, unknown>>;
}

/**
 * #22: the base view config of a Linux view is both upstream ones at once. Neither is a superset of the other —
 * Android's carries `outline*`, `backgroundImage` and the `nextFocus*` keyboard-focus props, iOS's carries
 * `cursor`, the pointer-event handlers and the accessibility actions — and a desktop platform needs both halves,
 * because a prop missing from `validAttributes` is dropped by the diff before it reaches the native side. Where
 * both declare a key, Android's wins: this platform's C++ props are the `platform/cxx` ones Android shares.
 */
/**
 * The press and release handlers neither half carries for this platform: Android's never lists them, and iOS's
 * wraps its event props in `ConditionallyIgnoredEventHandlers`, which drops them everywhere but iOS. Without them
 * React never sets the `events` bits upstream's `PointerEventsProcessor` filters `topPointerDown` and `topPointerUp`
 * by, so no React view would see a press (#168).
 */
const pointerButtonAttributes = {
  onPointerDown: true,
  onPointerDownCapture: true,
  onPointerUp: true,
  onPointerUpCapture: true,
};

const mergeBaseViewConfigs = (android: PartialViewConfig, ios: PartialViewConfig): PartialViewConfig => ({
  bubblingEventTypes: { ...ios.bubblingEventTypes, ...android.bubblingEventTypes },
  directEventTypes: { ...ios.directEventTypes, ...android.directEventTypes },
  validAttributes: { ...ios.validAttributes, ...android.validAttributes, ...pointerButtonAttributes },
});

export { mergeBaseViewConfigs };
