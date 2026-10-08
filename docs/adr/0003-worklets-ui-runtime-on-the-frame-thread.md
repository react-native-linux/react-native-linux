# ADR-0003: Worklets' UI runtime runs on the frame thread

- Status: Accepted
- Date: 2026-10-07
- Deciders: Vitalii Yehorov
- Amends: ADR-0001 decision 6 ("Threading and animation, in React Native's actual terms")
- Issue: [#135](https://github.com/react-native-linux/react-native-linux/issues/135)

## Context

`react-native-worklets` runs worklets on a second Hermes runtime owned by whichever thread its `UIScheduler` calls
the UI thread. On iOS and Android that is the main thread, which does not draw. ADR-0001 decision 6 added a
platform-owned frame thread, "our concept and not a React Native one". That thread acquires the swapchain image,
paints through Skia and presents, and it is also the thread that applies mounting, which makes it this platform's
UI thread. `IOSUIScheduler::scheduleOnUI` runs a job inline when it is already on the UI thread, so a worklet
scheduled there runs inside the frame, against the compositor's deadline.

The options were:

1. **The UI runtime on the frame thread**, with iOS's semantics: a slow worklet makes that frame late.
2. **A dedicated worklet thread with its own queue.** Drawing is isolated, but `runOnUI` stops being synchronous when
   already on the UI thread. Reanimated's assumption that a shared value written in a worklet is visible to the
   same frame's prop application has to be re-proved, and the divergence from iOS and Android is permanent.
3. **Option 1 plus a per-frame worklet budget** that defers overflow to the next frame.

## Decision

Option 1. `LinuxUIScheduler` names the frame thread as the UI thread:

- A job scheduled on the frame thread runs inline, as on iOS.
- A job scheduled from any other thread is queued, and the frame thread drains the queue with upstream's own
  `triggerUI` at one point per frame.

There is no budget. The cost of a slow worklet is measured under the frame-timing harness of #20. Option 3's budget
is added only if those numbers show the frame deadline cannot be held without it.

## Consequences

- Worklet semantics match iOS, so Reanimated's same-frame visibility of shared values holds by construction rather
  than by proof.
- A worklet that runs long costs frames, exactly as it costs frames on a phone. That is visible in the frame journal
  rather than hidden behind a queue.
- The frame thread now owns a second JavaScript runtime. Its lifetime and teardown order belong to the window
  session (#136), and leaking it is #76's to catch.
