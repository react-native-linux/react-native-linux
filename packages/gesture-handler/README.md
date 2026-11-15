# @react-native-linux/gesture-handler

The Linux overlay of https://github.com/software-mansion/react-native-gesture-handler, vendored at `v3.5.0`, the
version T3 Code pins (#634).

## Layout

- `upstream/`: the vendored tree at the locked tag, with the patch queue applied. It is checked in and generated:
  change it through `pnpm upstream:*`, never by hand.
- `patches/`: our deviations from upstream, applied in numeric order.
- `test-bundles/`, `e2e/`: the conformance scenarios `pnpm e2e` discovers. A `.tsx` bundle is a React application
  entry, which the e2e driver builds with Metro through the harness app's config before it runs.

## How Linux runs gestures

The library has no shared C++: its Android and Apple halves are two independent implementations of one gesture
engine. It does ship a third, in JavaScript, for the web (`src/web`): the handlers, `GestureHandlerOrchestrator`,
`InteractionManager` and velocity tracking, behind one `GestureHandlerDelegate` interface. The owner decided on #168
that Linux runs that engine, so no fourth implementation exists. Linux supplies only what the DOM supplies on web:

- `RNGestureHandlerModule.linux.ts` creates the web handlers with `GestureHandlerLinuxDelegate`, behind the module
  contract `RNGestureHandlerModule.web` answers in 3.x (`setGestureHandlerConfig`, `updateGestureHandlerConfig`,
  `configureRelations`, `detachGestureHandler`, `getModuleId`).
- `findNodeHandle.linux.ts` answers the host element, a `ReactNativeElement`, whose `getBoundingClientRect` and
  `contains` come from React Native's own DOM APIs.
- `GestureHandlerRootView.linux.tsx` captures every pointer event under the root, and `LinuxPointerRouter` hands a
  down to each attached view that contains its target, and every later event for that pointer to the views that
  took the down: the routing `setPointerCapture` gives a web view.
- `LinuxPointerEventManager` is the web pointer manager, with its offsets read from the bounding rectangle rather
  than from a computed CSS scale React Native does not have.
- `attachHandlers` passes Linux the same callback ref as web, so a `runOnJS(true)` gesture's callbacks run straight
  from the engine.

- A gesture that activates takes the pointer from React's responder: while any gesture is active, the root view
  claims the responder in the capture phase, so a `Pressable` under it ends with `pressOut` and no `press`, as
  RNGH cancels the JavaScript responder on Android and iOS. The platform sends each pointer event before the touch
  event of the same step, so the move that activates a gesture is the move that takes the pointer
  (`gesture-pan-over-pressable`).

Recognition therefore runs on the JavaScript thread, the trade-off the decision accepted. The builder API's worklet
callbacks (`REANIMATED_WORKLET`) are #95's; the hook API's run as described below.

3.x has two APIs, and the above is the builder one (`Gesture.Pan()` with `GestureDetector`). The hook API
(`usePanGesture`, `useNativeGesture`, …, and `ReanimatedSwipeable`, which is built on it) renders through
`HostGestureDetector`, a native component on Android and iOS. On Linux, `HostGestureDetector.linux.tsx` is the web
detector, a `display: contents` view that attaches the handlers to itself (or, for a Native gesture, to its child).
That view has no box of its own, so `GestureHandlerLinuxDelegate` marks a handler attached to its own detector view,
and `LinuxPointerRouter` bounds that view by its children, as the web engine's `getEffectiveBoundingRect` does. Any
other view keeps its own bounds, even an empty one.

A hook gesture with worklet callbacks renders through `ReanimatedNativeDetector`. On Android and iOS, Reanimated
registers its worklet handler with the native detector view. `ReanimatedNativeDetector.linux.tsx` instead schedules
each event the engine sends onto the UI runtime, flattened and named as Reanimated's web `jsListener` sends it
(`{ ...nativeEvent, eventName }`). The hooks key each gesture's last update by the event's `handlerTag`, and the
composed handler of `InterceptingGestureDetector` routes by `eventName`. The flat event is also a copy, which matters
because the engine reuses an activation's state-change event for the update after it. A SharedValue in a hook gesture's config is pushed from the UI runtime,
where Android and iOS answer with a JSI host function; `NativeProxy.linux.ts` sends that update back to the React
Native runtime, where the Linux module lives.

The `gesture-hook-api` e2e drives `usePanGesture` through began, active and end, and moves its view with the
translation. A `useExclusiveGestures` double/single tap pair arbitrates as the builder pair does, and
`useNativeGesture` over a `ScrollView` begins on a click while the wheel keeps scrolling. An
`InterceptingGestureDetector`'s worklet pan ends on the UI runtime, and a builder tap on a zero-size view rejects a
click on the child it overflows into. In `gesture-reanimated-swipeable`, a `ReanimatedSwipeable` row (the subpath T3
Code imports) sits in a `ScrollView` under a `useNativeGesture` detector, as in T3 Code's thread list, and a drag
springs it open on its right actions. Beside it, a toast-like worklet pan with `activeOffsetX` and `failOffsetY`
fails on a vertical drag and ends on a horizontal one. It does both again when the two drags reach the app in one
batch, as a busy JavaScript thread receives them: the web orchestrator resets finished handlers in a microtask, which a
browser runs between two events and React Native only after the whole batch, so `LinuxPointerRouter` resets them
before it routes each press.

The `gesture-pan-over-pressable` e2e verifies mouse drags report upstream `PointerType.MOUSE`. Wheel input before
and between pointer presses must not begin Pan recognition or create another press. Touchpad and touchscreen
runtime source distinctions remain acceptance work on #168.

The `gesture-long-press` e2e exercises the upstream LongPress recognizer through the Linux pointer delegate:
a short primary-button click and movement beyond `maxDistance` fail, while a hold beyond `minDuration` activates
and ends successfully. Recognition uses the existing JavaScript engine and its timers.

The `gesture-native-view` e2e covers the upstream Native recognizer's default pointer lifecycle around a Linux
Pressable. A click without movement fails recognition and delivers one press. Movement inside the Pressable
activates Native, cancels the press through responder capture and releasing ends the gesture without another press.
Leaving the view cancels an active Native gesture without another press; a subsequent click proves pointer ownership
is released. The trace distinguishes failed, ended and cancelled final states. Linux skips browser style and attribute
access. A Native recognizer attached directly to TextInput preserves click focus and typing, completes a mouse drag,
and leaves keyboard editing functional afterwards. With `disallowInterruption(true)`, its activation cancels an
enclosing Pan before that contender reaches its movement threshold; the Native drag still ends successfully.
A Native recognizer attached to ScrollView preserves wheel scrolling before and after a click; wheel events never begin or activate the recognizer. The same scenario sends
continuous touchpad-style axis deltas, then changes axis after a stop. Horizontal movement after vertical scrolling
requires the axis lock to reset, while neither continuous gesture starts another pointer recognizer.
This is virtual Wayland protocol coverage; physical touchpad and touchscreen source verification remain on #168.
A Native recognizer attached directly to a controlled public Switch preserves click toggling and focused keyboard
Space toggling; keyboard input never begins or activates another pointer recognizer.
Other native controls, Android's `shouldActivateOnStart` option and the flagship board remain acceptance work on #168.

The `gesture-compositions` e2e proves two upstream Pan recognizers activate and end successfully on the same
mouse drag under `Simultaneous`. An `Exclusive` double/single Tap pair allows a single tap after the double tap
fails, and recognizes a double tap without also delivering the lower-priority single tap. No Linux-specific
composition algorithm is added; this exercises upstream orchestration through the Linux pointer delegate.
The same scenario covers `requireExternalGestureToFail` between nested views: the outer tap remains pending
before the inner double tap's failure deadline, succeeds after that deadline, and stays suppressed when the
inner double tap succeeds.

## Upstream

| Field | Value |
| --- | --- |
| Repository | https://github.com/software-mansion/react-native-gesture-handler |
| Tag | `v3.5.0` |
| Sparse paths | `packages/react-native-gesture-handler/src` |

```bash
pnpm upstream:bump gesture-handler <tag>       # re-vendor at a new tag and replay the patch queue
pnpm upstream:patch gesture-handler <name>     # capture the current edits to upstream/ as the next patch
pnpm upstream:check gesture-handler            # prove the vendored tree still matches tag plus queue
```

## Patches

| Patch | What it changes | Deletion trigger |
| --- | --- | --- |
| `0001-linux-platform` | Adds the five Linux files above, takes the web branch of `attachHandlers` on Linux, and asks `hasDisplayContents` for a computed style only where a DOM exists | Upstream accepting a `linux` platform: the files are already in the shape of that contribution |
| `0002-linux-pointer-ownership` | Tracks which gestures are active, and makes the Linux root view claim the responder while any is | The same as `0001` |
| `0003-linux-native-view` | Skips the `rnghtext` attribute and the DOM lifecycle events for a Native recognizer on a Linux view | The same as `0001` |
| `0004-linux-shared-value-config` | Adds `NativeProxy.linux.ts`: the web proxy, with SharedValue config updates scheduled back onto the React Native runtime | The same as `0001` |
| `0005-linux-hook-detectors` | Adds `HostGestureDetector.linux.tsx` and `ReanimatedNativeDetector.linux.tsx` for the hook API, and bounds a detector's own `display: contents` view by its children | The same as `0001` |
