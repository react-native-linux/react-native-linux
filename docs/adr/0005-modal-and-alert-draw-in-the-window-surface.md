# ADR-0005: `Modal` and `Alert` draw inside the window's own surface, on an overlay layer of the retained scene

- Status: Proposed
- Date: 2026-11-16
- Deciders: Vitalii Yehorov
- Issues: [#62](https://github.com/react-native-linux/react-native-linux/issues/62) (Modal and overlay surfaces),
  [#262](https://github.com/react-native-linux/react-native-linux/issues/262) (Alert)
- Flagship: [#634](https://github.com/react-native-linux/react-native-linux/issues/634), T3 Code's Expo app

## Context

On Linux today every confirmation T3 Code asks for is skipped without a trace. React Native 0.88's `Alert.alert`
has an `ios` branch and an `android` branch and nothing else, so on `Platform.OS === 'linux'` it returns without
calling anything. The resolver already maps `RCTAlertManager` to its iOS variant (`linuxUpstreamVariantIndex` in
`packages/cli/src/metro-config.ts`), but `Alert.js` never reaches it. `Modal` is not registered: `FabricHost`
logs `[component] 'ModalHostView'` and mounts nothing.

What the flagship needs, counted in `t3code/apps/mobile/src` at `2386789`:

- **Confirmations.** 100 files call `Dialog.alert` (251 sites), `Dialog.confirm` (13), `Dialog.prompt` (4),
  `Alert.alert` (9) and `Alert.prompt` (1). On Linux `src/components/ui/Dialog.tsx` resolves, and it forwards all
  of these to `Alert.alert` and `Alert.prompt`. So `Alert` needs `prompt` as well as `alert`, which upstream ships
  only for iOS.
- **In-app sheets.** `ThreadArrangementSheet` is a `fullScreen` `Modal` with `animationType="slide"`.
  `CommandPalette` is a `transparent` `Modal` with `animationType="fade"`: a dimmed scrim over the app, with a
  `TextInput` that `onShow` focuses and that `onRequestClose` (Escape) dismisses. The route-level sheets
  (`formSheet`, `fullScreenModal`) are `react-native-screens` presentations, which #166 owns. They need the same
  layer this ADR decides.
- **What its Electron build does.** The desktop build draws its confirmations inside the page.
  `apps/web/src/localApi.ts`'s `dialogs.confirm` resolves through `requestConfirmDialog`, which an in-page host
  renders. About 70 files use `@base-ui/react/dialog` in-page dialogs. Electron's
  `dialog.showMessageBox` is used only from the main process. The application we are measured against never
  opens a second OS window to ask "Archive this thread?".

### How the reference platforms answered

- **react-native-windows (Fabric)**: a separate OS window. `WindowsModalHostViewComponentView.cpp` creates an
  `AppWindow` with a modal `OverlappedPresenter` and hosts a `ReactNativeIsland` portal in a
  `DesktopPopupSiteBridge`. The open bug trail is the cost of that choice:
  - [rnw#16307](https://github.com/microsoft/react-native-windows/issues/16307) (July 2026): transparent
    `flex: 1` content measures 0×0. The real OS title bar's close button only emits `onRequestClose`. Its own
    summary is that *"dim scrims, bottom sheets, popovers — anything that composites over the app window — cannot
    be expressed with native Modal"*.
  - [rnw#14604](https://github.com/microsoft/react-native-windows/issues/14604): focus navigation into the popup
    bridge fails, and the code that moves focus in is commented out.
  - [rnw#14805](https://github.com/microsoft/react-native-windows/issues/14805): the window is sized wrongly.
  - [rnw#14069](https://github.com/microsoft/react-native-windows/issues/14069): their harness could see only one
    window. Our [#93](https://github.com/react-native-linux/react-native-linux/issues/93) records the same lesson.
  - `transparent`, `animationType` and `onRequestClose` are open as rnw#13160, #13157 and #13158, since 2024.

  Their `Alert` is a native module (`AlertModule.cpp`, spec `NativeDialogManagerWindows`) that queues requests
  and shows them one at a time as an OS `MessageDialog`, in Android's positive, negative and neutral shape.
- **react-native-macos**: `Modal` is compiled out on macOS (`#if !TARGET_OS_OSX` around
  `RCTModalHostViewComponentView.mm` and `RCTModalHostView.m`). [rn-macos#481](https://github.com/microsoft/react-native-macos/issues/481)
  ("Modal not implemented") has been open since 2020. Its `Alert` keeps iOS's `alertWithArgs` shape and shows an
  `NSAlert` as a sheet attached to the key window (`beginSheetModalForWindow`), or app-modal with `modal: true`.
- **Flutter** draws `showDialog` in-surface: a `DialogRoute` on the `Navigator`'s `Overlay` with a
  `ModalBarrier` that blocks input beneath. Its Linux embedder has one `GtkWindow` per view.
- **GTK4 / libadwaita**: a `GtkWindow` with `transient_for` and `modal` is a second `xdg_toplevel` with
  `xdg_toplevel.set_parent`, marked modal through `xdg-dialog-v1`. libadwaita 1.5 then moved GNOME's own dialogs
  into the parent window: `AdwDialog` and `AdwAlertDialog` are presented inside the parent as a floating card or a
  bottom sheet, and fall back to a separate window only when the parent cannot host them. `GtkPopover` is the `xdg_popup` user.
- **Electron**: `dialog.showMessageBox(parent)` is a GTK dialog, a separate transient toplevel. Web content's
  `<dialog>.showModal()` uses the in-page top layer: everything else becomes inert, and Escape fires `cancel`.
- **lucid-softworks/react-native-linux** (GTK4): `Modal` is a JavaScript `View` with `position: absolute` in the
  same window (`apps/playground/runtime/modal.js`). That is in-surface, but in the tree: a parent's `overflow`
  clip, transform or a later sibling's `zIndex` paints over it or cuts it, which is the
  [rn-macos#914](https://github.com/microsoft/react-native-macos/issues/914) failure mode #62 names.

### What Wayland offers instead of a drawn overlay

- **A second `xdg_toplevel`** with `set_parent`, plus `xdg_wm_dialog_v1.set_modal` (Hyprland 0.56 implements
  `xdg-dialog-v1`):
  - The client cannot position it. Wayland has no global coordinates, so "centred over the window" or "a sheet
    pinned to the window's bottom edge" is not expressible.
  - Placement, open animation, decoration, tiling and focus follow the user's compositor rules.
  - Keyboard focus moves to another surface, so `zwp_text_input_v3` enters and leaves per surface, and the IME
    session has to follow it.
  - A scrim over the parent is impossible, which is rnw#16307 again.
- **`xdg_popup`**: it is positioned relative to the parent through `xdg_positioner`, and it is a different
  contract from a modal:
  - Creating a popup needs no input serial, but an explicit grab (`xdg_popup.grab`) does. Without a grab the
    popup gets no keyboard focus of its own, so a keyboard-operable popup cannot be raised without input. T3 Code
    raises alerts from failed network calls, which have no input serial.
  - With a grab, the compositor dismisses the popup on an outside click, where a modal must block that click.
- **`wl_subsurface`**: a second buffer inside the same window. It keeps placement in our hands, but it adds a
  second swapchain and synchronized commits. It buys a separately composited layer, which the 120 Hz numbers do
  not yet ask for (see *What would make us revisit*).
- **xdg-desktop-portal** has no message-dialog interface. Spawning `zenity` or `kdialog` would add a toolkit
  dependency, a separate window, unthemed UI and nothing a test under cage could see.

## Decision

**`Modal` is an in-surface overlay: a layer of the retained scene, painted above the surface's tree, hit-tested
and focused before it, and blocking everything beneath it. `Alert` is JavaScript on top of `Modal`.
`xdg_popup` stays reserved for content that must leave the window's bounds, which today is nothing. A second
`xdg_toplevel` stays reserved for multi-window (M5, #436), which is a different feature from a modal.**

The reasons, weighed in the order the brief named them:

1. **Focus and IME.** The compositor's keyboard focus never moves, so the one `TextInputSession` keeps working.
   The `TextInput` in `CommandPalette` composes exactly as one in the main tree does. Focus trapping is a filter
   over the focusable set `InputDispatcher` already computes, not a negotiation with Hyprland's focus rules.
2. **Input routing.** `InputDispatcher` already hit-tests the retained scene, not the shadow tree. An overlay is
   one more rule in that walk: the topmost overlay answers every point. The wheel and keyboard scrolling follow
   the same rule (see *Input and focus*). There is no second `wl_pointer` focus and no second `PointerRouter`.
3. **One surface against the 120 fps budget.** One swapchain acquire and present per frame, one frame-callback
   stream, one `wp_presentation` feedback, one damage region. An open modal costs the nodes it paints. Opening a
   full-surface scrim damages the whole surface once, or every frame of a fade. That is the same cost class as any
   full-window animation. The 120 Hz frame-time gate (#654) gets a scenario for exactly this case (see *Test
   layers*). A second toplevel is a second
   `VkSurfaceKHR` and swapchain with its own pacing loop. Its frame callbacks stop whenever Hyprland considers it
   hidden. Its time to first visible frame includes a compositor open animation we do not control.
4. **Headless testability.** `GoldenRenderer` renders one scene, and an overlay is part of that scene, so goldens
   need nothing new. The e2e harness drives one window under cage. A child toplevel would wait on #93 for every
   assertion.
5. **Accessibility.** An in-window dialog is a child of the frame's accessible, the way Firefox and libadwaita
   expose theirs. `DumpAccessibilityTree` projects it today, and the AT-SPI bridge (#347) inherits it unchanged.
6. **Stacking.** Overlays are an ordered list, so nested modals and an `Alert` over a `Modal` are list order. No
   compositor stacking policy is involved.
7. **What T3 Code needs.** In-app sheets and confirmations that dim and sit over the app, which is what its own
   Electron build draws. That rules out separate OS windows.

### Modal: the first slice

**Component.** Register `ModalHostView` with a Linux descriptor in `packages/core/src/ModalHostComponent.{h,cpp}`,
beside `SwitchComponent.h`. Upstream's `ModalHostViewComponentDescriptor` is `final`, and on the `cxx` platform
its initial state comes from `ModalHostViewScreenSize()`, which returns 0×0. That lays the first frame out at zero
size. It is the failure of [react-native#50442](https://github.com/facebook/react-native/issues/50442) ("Modal's
children are rendered in the top left corner for the first few frames"), #62's first criterion. The Linux
descriptor copies Android's answer rather than iOS's:

- `createInitialState` reads the surface's logical size from the per-surface `DimensionsSource`.
  `TurboModuleRegistry` already owns it as a `shared_ptr`, and `ReactHost::dimensions()` exposes it.
  `FabricHost` inserts that same pointer into the `ContextContainer`, so the first commit is already full-size,
  and there is no new size map. `DimensionsSource`'s mutex already makes `metrics()` safe from whichever thread
  commits.
- `adopt` is upstream's: the size comes from the state, and the position is absolute.
- A resize calls `updateState` on each presented modal's state with the new size, as iOS's component view does.
  It does so from the same `DimensionsSource` change that emits `didUpdateDimensions`.
- The shadow node, props, state and event emitter stay upstream's (`rrc_modal` is already built), so `Modal.js`
  runs unmodified.

**Scene.** `RetainedScene` keeps an ordered list of overlay roots per surface. When `insertChild` sees a
`ModalHostView`, it appends the node to that list, which is mount order. `removeChild` and `deleteNode` take it
out of the list in the same transaction. The node stays the parent's child for bookkeeping, but it is not painted
at its tree position. `ScenePainter` paints the surface tree first and then each overlay root in list order:

- An overlay root is composed from the surface origin with the identity matrix. Every ancestor's transform,
  opacity, `overflow` clip and `zIndex` is ignored, because `ModalHostView` is `RootNodeKind` and lays out as its
  own root.
- Presenting or removing an overlay damages the whole surface.
- The overlay covers the React surface, not the drawn title bar (#329), so the window can always be moved,
  resized and closed.

`findNodeAtPoint` asks the topmost overlay first and stops there. A point that misses every child of the overlay
lands on the overlay root itself, the backdrop, and never on anything beneath it. That is the rn-macos#914
criterion. `Modal.js` has no backdrop-press dismissal, so neither does the platform.

The hit path holds only overlays that are in the list now. A removed overlay leaves the hit test, the focus trap
and the wheel route in the commit that removes it, however fast a present follows a removal. That is #62's second
criterion, [react-native#50152](https://github.com/facebook/react-native/issues/50152) ("Invisible layer blocks UI
when rapidly opening and closing modals"). The dismissal snapshot that a later `animationType` slice adds only
paints. It is never hit-tested, never focus-trapped and never a wheel target.

**Input and focus.**

- **Focus trap, on focus only.** `collectFocusables` keeps walking the whole committed tree. It also fills
  `textInputNodes_` for `TextInputController::setMountedFields`, which drops every field it is not given, so a
  walk restricted to the overlay would throw away a composer draft the moment a confirmation opens. While an
  overlay is presented, the trap filters only the tags handed to `FocusModel::setFocusableTags`, down to the
  topmost overlay's subtree. `FocusModel` needs no new rule for the node that loses focus: a focused node outside
  the new set is blurred, which `setFocusableTags` already does.
- **Initial focus.** On present, focus moves to the overlay's first focusable. The focus ring keeps the state it
  had, so a keyboard user keeps the ring. That is `<dialog>.showModal()`'s rule and what base-ui's dialog does in
  T3 Code's Electron build. An application focus wins: a `ref.focus()` from `onShow`, as `CommandPalette` does,
  or an `autoFocus` field inside the modal, is applied after the platform's rule, because commands drain after the
  present. `Alert` sets its own initial focus this way (see *Alert*).
- **Restore.** On dismissal, focus returns to the tag focused when the overlay was presented, if that tag is
  still focusable. The restore stack, keyed by overlay tag, lives in `FocusModel`, because it is tag arithmetic.
- **Escape.** Escape is delivered as a `keyDown` to the focused node as today. Unless a composition is active, it
  then fires `onRequestClose` on the topmost overlay's `ModalHostViewEventEmitter`.
- **Close request.** The compositor's close request closes the window, as it does today. It is not routed to
  `onRequestClose`. Escape is the modal's close path. A modal whose app ignores `onRequestClose` must not be able to
  make the window impossible to close.
- **The wheel.** `ScrollController::scrollViewUnderPointer` runs its own `UIManager::findNodeAtPoint` walk from
  the surface root, which would scroll the page beneath an open modal. While an overlay is presented, that walk
  starts at the topmost overlay's `ModalHostView` shadow node instead. A point with no `ScrollView` under it
  inside the overlay scrolls nothing.
- **Keyboard scrolling.** `keyboardScrollTarget`, which picks the outermost `ScrollView` for a key pressed with
  nothing focused, picks it inside the topmost overlay for the same reason.
- **Presses.** A press in progress beneath a presenting overlay is cancelled with the existing `cancelTouch`.
- **`onShow`** fires after the frame that first paints the overlay is submitted.

**Accessibility.** While an overlay is presented, the `DumpAccessibilityTree` projection holds only the topmost
overlay's subtree, as `aria-modal` and iOS's `accessibilityViewIsModal` do. Its root is projected with role
`dialog` and the `modal` state, which become AT-SPI `ROLE_DIALOG` and `STATE_MODAL` when #347 lands.

**Props.**

- `transparent` and `backdropColor` are already a `backgroundColor` that `Modal.js` sets on its container.
  `transparent` wins, so `backdropColor` is ignored on a transparent modal, and a transparent modal's scrim is a
  child `View` of the application's.
- `presentationStyle` is accepted, and every style covers the surface, as on Android.
- `Modal.onDismiss` never fires on Linux, and `Modal.js` is not overridden for it. Upstream gates `onDismiss`,
  `onOrientationChange` and `allowSwipeDismissal` on `Platform.OS === 'ios'`, so Linux has Android's behaviour.
  `CommandPalette` already carries the 400 ms fallback that Android relies on. This is a different callback from
  `AlertOptions.onDismiss`, which Linux does fire (see *Alert*).
- `animationType` paints as `none` in the first slice.

**Out of the first slice, each named:**

- `animationType` `fade` and `slide`, on present and on dismissal. Dismissal is the hard half: `Modal.js` returns
  `null` at once on non-iOS, so the scene has to keep the deleted overlay's last picture for the animation's
  length, which is iOS's snapshot approach. It paints only, as above.
- `react-native-screens` modal presentations (#166).
- `@react-native-menu/menu`'s anchored menus (#646).

The last two are the second and third consumers of the overlay list. The list is generalised beyond
`ModalHostView` when the second one lands, not before.

### Alert: the first slice

**JavaScript on top of `Modal`, not a native module.** A native `Alert` would have to lay out text, buttons, a
text field, light and dark themes and translations in C++. That is a second UI toolkit inside a platform whose
whole point is one paint model. With `Modal` in place, an alert is a small React tree.

**Upstream's `Alert.js` on its iOS branch, over a JavaScript `RCTAlertManager`.**

- **`Alert.linux.ts`** is upstream's `Alert.js` with the `Platform.OS === 'ios'` branch taken unconditionally.
  `alert` calls `prompt` with type `default`, and `prompt` builds the iOS arguments: buttons keyed by their index,
  `cancelButtonKey`, `destructiveButtonKey`, `preferredButtonKey`, `defaultValue`, `keyboardType` and
  `userInterfaceStyle`. It also drops a last button with no text, as upstream does.
  - **The one Linux deviation.** It also passes Android's `cancelable` and `onDismiss` options, which the iOS
    branch drops, because the Escape table below needs them.
- **`RCTAlertManager.linux.ts`** implements `alertWithArgs(args, callback)` in JavaScript, ported from
  `React/CoreModules/RCTAlertManager.mm`. Its normalization is upstream's:
  - A call with neither a title nor a message is an error.
  - With no buttons, a `default` alert gets one `OK`, which is the cancel key. A prompt gets `OK` and `Cancel`,
    with `Cancel` as the cancel key.
  - `plain-text` and `secure-text` call back `(key, text)`. `login-password` calls back
    `(key, {login, password})`. `default` calls back `(key)`.
  - `userInterfaceStyle` overrides light or dark per call, and otherwise the card follows `Appearance`.
- **Overlay index.** Both files join `linuxOverlayIndex` and `packages/core/overrides.json`. The
  `"Libraries/Alert/RCTAlertManager": "ios"` entry in `linuxUpstreamVariantIndex` is removed, because `Alert.js`
  never reached it on Linux and the overlay replaces it. `metro-config-overlay.spec.ts`'s list loses that line.
- **No native module.** No `AlertManager` TurboModule is registered, because nothing crosses into C++.

**Requests and the host.**

- **Requests.** `alertWithArgs` puts each request into a module-level store, so an alert raised before the root
  mounts is not lost. Each request records its target root:
  - With one window, the first mounted root.
  - Under #436, the root that last held keyboard focus.

  There is no `rootTag` API.
- **Hosts.** Each `AlertHost` renders only its own root's requests, so an alert is never drawn twice across
  roots. It renders every pending request as a `transparent` `Modal`, newest on top. A second alert stacks over
  the first, as on iOS and Android, without a queue.
- **Mounting.** `AlertHost` is mounted once per root by an overlay of `Libraries/ReactNative/AppContainer`, which
  wraps upstream's dev and prod containers. The nested `AppContainer` that a development `Modal` renders shares its
  root's tag and does not mount a second host.
- **Why not `AppRegistry.setWrapperComponentProvider`.** It is a single slot the application owns. An app or
  framework that sets it would replace the platform's provider and silently remove the alert host, and every
  `Alert` after that would be skipped, which is today's bug again.

**Layout.**

- **Order.** Buttons render in a row in the order given, except that a `cancel`-style button is moved to the
  start, as UIAlertController does for two buttons and the GNOME HIG asks. The reordering is visual only: every
  button keeps upstream's key, which is its index in the caller's array, so the callback reaches the right
  `onPress`. The row mirrors in RTL and wraps into a column when it does not fit.
- **Size.** The card is centred on the surface, at most 400 logical pixels wide and the surface minus a margin.
  The message scrolls when the card would be taller than the surface. A size constraint the surface cannot
  satisfy resolves to that clamp, never to a failure (#262's second note).
- **Accessibility.** The card has role `alertdialog` (AT-SPI `ROLE_ALERT`), the title as its name and the message
  as its description.
- **Anchors.** `Alert` takes no anchor upstream and is centred. Anchoring belongs to action sheets and menus
  (#646), which can place against a view's scene rectangle in the same coordinate space without `xdg_popup`.

**Keyboard.** The rules below live in one pure function, unit-tested, and each is also proved through the real
input path (see *Test layers*):

- **Default button.** The `isPreferred` button unless it is destructive. Otherwise the last button that is
  neither `cancel` nor `destructive`. Otherwise none. Enter never activates a destructive button by default.
- **Initial focus.** This overrides `Modal`'s first-focusable rule explicitly. `AlertHost` applies it with a focus
  from `onShow`, the path an application's focus takes.
  - A prompt focuses its first text field: the login field for `login-password`.
  - Otherwise focus goes to the default button.
  - Otherwise to the cancel button.
  - Otherwise, when no safe button exists, to the card itself: a focusable target with no press handler, so
    Enter and Space do nothing until the user chooses a button with Tab.
- **Enter and Space** activate the focused button through the existing activation path (`isActivationKey`), so
  Enter needs no mapping of its own. In a prompt, `onSubmitEditing` presses the default button.
- **Escape**, through the `Modal`'s `onRequestClose`, takes the first rule that applies:
  1. A lone button that is a non-destructive acknowledgement is activated.
  2. Otherwise the cancel button is pressed, if there is one.
  3. Otherwise, if the alert is `cancelable`, it is dismissed: `AlertOptions.onDismiss` is called and no button's
     callback is, destructive or not.
  4. Otherwise nothing happens, and the alert keeps the focus trapped.

### Test layers per slice

| Slice | Unit | Golden | E2E |
| --- | --- | --- | --- |
| **Modal: scene and component** (renderer lane) | GoogleTest at 100%: overlay list order on mount, removal and nesting; hit test stops at the topmost overlay, backdrop included; the overlay ignores an ancestor's transform, clip and `zIndex`; rapid present and remove, in one transaction and across consecutive commits, leaves no overlay in the hit path; first-commit size from `createInitialState` over `DimensionsSource`; `updateState` on resize | Two fixtures, each declared inside a clipped, transformed, low-`zIndex` parent and painting full-surface and on top: an opaque modal with `backdropColor`, and a transparent modal whose child `View` is the scrim | The surface's tree beneath an open modal receives no press. A #654 frame-gate scenario (`modal-scrim-over-list-120hz`): a transparent scrim modal over a scrolling list, held to the 120 Hz budget |
| **Modal: focus and keys** (input lane) | GoogleTest at 100%: the trap filters only the focusable tags; a `TextInput` beneath keeps its buffer across present and dismiss; initial focus, and an `onShow` or `autoFocus` focus winning over it; the restore stack (nested); Escape → `onRequestClose` only on the topmost; no Escape during composition; touch cancel on present; the wheel route and `keyboardScrollTarget` start at the topmost overlay | — | Open with the keyboard; Tab and Shift+Tab cycle inside only; Escape closes; focus is back on the invoker. The wheel over an open modal scrolls the modal's list, not the page beneath. A `TextInput` in a transparent modal takes IME text. A `DumpAccessibilityTree` snapshot with the modal open holds only the modal, role `dialog` |
| **Alert** (modules lane) | Vitest at 100%: the default-button, initial-focus and Escape table; the visual reorder preserving callback keys; request store ordering, stacking and root targeting; `alertWithArgs` against `RCTAlertManager.mm`'s cases (no buttons, prompt OK and Cancel, `login-password` credentials, missing title and message) | Two-button and three-button alerts, a destructive confirmation, a prompt, each in light and dark | Keyboard scenarios through the real seat and `InputDispatcher` against the real `Alert.linux.ts`, one per rule above: Tab cycles inside the alert only; Enter presses the default; Enter on a destructive-only confirmation activates nothing; Escape on a lone OK, on cancel, on a cancelable alert with no cancel (`onDismiss`, no button callback) and on a non-cancelable one (nothing); an `onShow` focus wins. Open a prompt, type without clicking, press Enter: the callback gets the text. T3 Code's `Dialog.confirm` and `Dialog.prompt` called through the desktop profile reach their callbacks |

**Order.** Renderer → input → Alert. Implementation can run in parallel, but the input slice merges after the
renderer slice, and the Alert slice merges only after both: its keyboard rules stand on the trap, the restore and
Escape.

## Rejected

- **A second `xdg_toplevel` per modal** (react-native-windows' answer). Its placement, scrim, focus and test
  surface are compositor policy or impossible on Wayland, and RNW's open issues (#16307, #14604, #14805, #14069)
  are that cost paid on a platform that *can* position windows. It returns as multi-window under #436, where a
  separate window is the feature rather than the side effect.
- **`xdg_popup` for `Modal`**. A keyboard-operable popup needs a grab, the grab needs an input serial, and the
  grabbed popup is dismissed on an outside click. A modal must block the outside click, and it can be raised with
  no input at all.
- **A drawn overlay on a `wl_subsurface`**. It buys separately composited layers before any frame-time number
  asks for them, and costs a second swapchain and synchronized commits.
- **A JavaScript `Modal` that is an absolutely positioned `View`** (lucid-softworks'). It is clipped, transformed
  and out-stacked by its ancestors. Stacking above everything has to be a renderer rule, not a style.
- **A native `Alert` module drawing in C++**, or an external dialog process. Explained under *Alert*.
- **A Linux re-implementation of `Alert.js`'s argument handling.** Upstream's iOS branch and `RCTAlertManager.mm`
  already define it. A port keeps the same defaults, keys and callback shapes, and an upstream change shows up as
  an override diff.
- **Routing the compositor's close request to `onRequestClose`** while a modal is open. A modal whose app ignores
  `onRequestClose` would make the window impossible to close, which no desktop toolkit permits. #62's criterion is
  amended to match.

## Consequences

- `Modal` and `Alert` cost no new Wayland protocol, no second swapchain and no second IME session. T3 Code's
  confirmations, its command palette and its arrangement sheet run on the slices above.
- The overlay list is the platform's only stacking context above the surface tree. Menus, `react-native-screens`
  presentations and future toasts join it instead of inventing their own.
- An overlay can never extend beyond the window. A menu opened at the window's edge flips inward rather than
  overflowing, until a consumer proves it must overflow, which is when `xdg_popup` returns.
- `Alert` behaves identically in every app, because the platform owns the host. It also means the platform owns
  its look; the goldens fix it, and theming beyond light and dark is a later issue.
- `Alert`, `RCTAlertManager` and `AppContainer` join the upstream JavaScript files this platform overrides,
  tracked in `packages/core/overrides.json` (ADR-0001's recurring cost).

## What would make us revisit

- **`modal-scrim-over-list-120hz` misses the 120 Hz budget** that the same list meets without the modal, and a
  separately composited layer would not. That reopens `wl_subsurface`.
- **A consumer needs content outside the window**: a menu or tooltip larger than a small window, or a
  drag-and-drop preview. That brings in `xdg_popup` for that consumer only.
- **Multi-window lands (#436)** and an application wants a modal *window*. That is a new component or a
  `presentationStyle` value, not a change to `Modal`'s default.

## Questions the council settled

1. **Compositor close request.** It closes the window. Escape is the modal's close path. #62's criterion is
   amended to match.
2. **Which root hosts an `Alert`.** With one window, the first mounted root. Under #436, the root that last held
   keyboard focus. No `rootTag` API.
3. **Initial focus for `Modal`.** It follows `<dialog>.showModal()` and moves to the first focusable. A prompt
   focuses its field instead, and an application's own focus wins.
4. **`Modal.onDismiss`.** It never fires on Linux, which is Android's behaviour, and `Modal.js` is not
   overridden. `AlertOptions.onDismiss` is a different callback, and Linux fires it when a cancelable alert is
   dismissed with no button.
