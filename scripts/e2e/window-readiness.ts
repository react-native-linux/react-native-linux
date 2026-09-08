/**
 * `WindowMain.cpp`'s `announceFirstPresentedFrameOnce` prints this unconditionally, independent of
 * `--window-debug`, the first time `WaylandWindow::hasPresentedFirstFrame` turns true — the first
 * `wp_presentation_feedback.presented`, or, with no `wp_presentation` bound, the first `wl_surface.frame`
 * callback. See *E2E driver (#7)* in docs/cpp-toolchain.md. `waitForWindowReadyFailures` below waits for it in
 * addition to the bundle's own ready line, which is #373's fix: a bundle can call `console.log` before the
 * window has ever put a pixel on screen, so the ready line alone proves the bundle mounted, not that the window
 * is visible.
 */
const FIRST_PRESENTED_FRAME_TRACE_LINE = "[rnl-present] first frame presented";

/** Whether `trace` already shows the window presented its first frame. */
const hasPresentedFirstFrame = (trace: string): boolean => trace.includes(FIRST_PRESENTED_FRAME_TRACE_LINE);

/** The accumulated trace text `scripts/e2e.ts`'s compositor process writes to. */
interface TraceSource {
  readonly text: string;
}

/**
 * Waits for the bundle's own ready line and the window's first-presented-frame line concurrently — neither's
 * arrival depends on the other having been observed first — and returns the empty array once both have appeared,
 * or an array naming whichever one (or both) never did. A scenario that expects the window to exit on an injected
 * fault (`expectsExitAfter`) may never present at all, so its expected-exit line satisfies the presented-frame
 * wait too. `wait` is `scripts/e2e.ts`'s own `waitUntil`, already bound to its timeout, injected so this stays
 * free of a real timer in its own tests.
 */
/** The two trace lines a scenario names: the bundle's own ready line, and the exit it expects, if any. */
interface ReadyTraceLines {
  readonly bundleReadyTraceLine: string;
  readonly expectedExitTraceLine: string | null;
}

const waitForWindowReadyFailures = async (
  { bundleReadyTraceLine, expectedExitTraceLine }: ReadyTraceLines,
  trace: TraceSource,
  wait: (isReady: () => boolean) => Promise<boolean>,
): Promise<readonly string[]> => {
  const hasExpectedExit = (): boolean => expectedExitTraceLine !== null && trace.text.includes(expectedExitTraceLine);
  const [bundleReady, framePresented] = await Promise.all([
    wait(() => trace.text.includes(bundleReadyTraceLine)),
    wait(() => hasPresentedFirstFrame(trace.text) || hasExpectedExit()),
  ]);

  return [
    ...(bundleReady ? [] : [`the bundle never printed "${bundleReadyTraceLine}"`]),
    ...(framePresented ? [] : [`the window never printed "${FIRST_PRESENTED_FRAME_TRACE_LINE}"`]),
  ];
};

export { FIRST_PRESENTED_FRAME_TRACE_LINE, hasPresentedFirstFrame, waitForWindowReadyFailures };
