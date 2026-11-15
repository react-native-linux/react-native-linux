# T3 Code's Electron build, measured: the baseline rnl_window has to beat

- Part of #642 (epic #634). This is the Electron half of the comparison and the shared method. The rnl_window half
  waits on #641: T3 Code does not render on this platform yet. When it does, it runs on the same workload, in the
  same compositor, at the same output size and refresh rate, and the two tables sit side by side here.
- Measured with `pnpm perf:electron --t3code <checkout>` (`scripts/perf-electron.ts`). Re-run it the same way.

## Method

**The build.** T3 Code at commit `238678951a` (version 0.1.9), built the way its own CI builds the desktop app before
the launch check: `vp i`, `vp run --filter @t3tools/desktop ensure:electron`, `vp run build:desktop`. That gives
the production web bundle, the bundled server and `apps/desktop/dist-electron`, run by the Electron 44.10.1
binary it pins. The release AppImage packages the same three. Build it in a scratch clone, never in a working
checkout.

**The rig.** The headless cage from scripts/e2e.ts: wlroots' headless backend with the pixman renderer and no
input devices, in a private `XDG_RUNTIME_DIR`. Nothing touches a real display or session. Cage's child is not the
app: it sets a 1280x720 custom mode at the requested rate with `wlr-randr`, as #654's 120 Hz scenarios do, and
then sleeps. Electron then connects to cage's socket as an ordinary client, with
`--ozone-platform=wayland --no-sandbox --remote-debugging-port=0`, and cage maximises it to the output. The
window rnl_window gets is the same size.

**The state.** A fresh T3 home seeded with the fixture T3 Code's own launch check opens
(`scripts/fixtures/desktop-launch/statev2.sqlite`: one project, two threads, an older schema the backend migrates
on start), plus a provider instance for T3 Code's replay agent (`apps/server/scripts/dev-replay-agent.ts`). The
replay agent plays recorded ACP turns with simulated streaming through the real adapter, orchestration and UI, so
**no step needs an account or a secret, and none is skipped**. Telemetry and the update check are off.

**The workload**, in order:

1. **Cold start**: spawn to the sidebar listing the fixture's threads (polled over the DevTools protocol).
2. **Idle**: two seconds with the window open and nothing happening.
3. **Streamed reply**: a thread is started on the replay provider's `showcase` scenario (thinking, a plan, tool
   calls and markdown) with T3 Code's own CLI (`t3 thread start`, against the app's backend), opened by its
   route, and given three more turns so the feed is long. The measured phase is one more turn, from `t3 thread
   send --wait` to its return, while the window shows the thread.
4. **Feed scroll**: `Input.synthesizeScrollGesture` over the feed's scroll container, the full height up and back
   down at 1500 px/s, as mouse-wheel input Chromium paces itself.

**What is recorded.**

- *Presentation intervals*: `WAYLAND_DEBUG=client` on the Electron process, and every
  `wp_presentation_feedback.presented` event in it. The compositor stamps those on `CLOCK_MONOTONIC`, the clock
  Node's `process.hrtime` reads, so each frame falls in a phase by its own timestamp. This is the signal
  rnl_window's frame log records, summarised the same way: nearest-rank p50 and p95 and the maximum over
  consecutive intervals inside the phase.
- *Late frames*: intervals longer than 1.5 refresh periods, so at least one deadline passed with no new frame.
  That is a missed deadline only while something is moving every frame, as in the scroll. In the streamed reply
  it also counts the waits between chunks, and in cold start and idle it means nothing.
- *Startup*: spawn to the first presented frame, and to the first frame presented after the sidebar was in the
  DOM ("first usable frame").
- *CPU time and memory*, from /proc, for the app's process tree split three ways. **Client** is the main process,
  the renderer, the zygotes and the utility processes. **GPU process** is reported apart: here it rasterises on the
  CPU, so its CPU time stands in for GPU time. **Backend** is the T3 server Electron runs in Node mode, and the
  replay agent under it. The backend is not part of the window, and T3 Code on rnl_window talks to the same one.
  CPU is utime plus stime spent inside the phase. Memory is RSS and PSS summed at the phase's end; PSS divides
  shared pages between processes and is the fairer number for a multi-process app.

**What is not recorded, and why.**

- *Input-to-photon latency*. The DevTools protocol injects input inside the renderer, above Wayland, so a number
  taken here would not compare with rnl_window's, whose input arrives through `rnl_inject`'s virtual pointer.
  Driving Electron through the same virtual-pointer path is the follow-up that makes it comparable.
- *GPU time on a GPU*. Under this rig there is no GPU (see below).

## The rig's rendering path, and what that does to the numbers

In the container there is no DRM device, so Mesa offers only llvmpipe. Chromium blocklists llvmpipe: its GPU
feature status reads `gpu_compositing: disabled_software`, `rasterization: disabled_software`, and the window
attaches `wl_shm` buffers. **Electron here rasterises and composites in software.** rnl_window under lavapipe also
renders on the CPU (Skia Graphite over Vulkan emulated by lavapipe) and also hands the compositor shm buffers, so
the two sides pay for pixels in the same currency. Neither is what either app does on real hardware.

The shortest presentation interval in every run was 7.9 to 8.0 ms, so the output did run at 120 Hz. Electron mostly
presents every second refresh (16.7 ms) while it scrolls: under this rig it is CPU-bound, not compositor-bound.

**Real-hardware numbers need a real 120 Hz output.** The script only runs the headless rig, and the numbers
checked in here are headless ones. A run on omarchy's own panel would need the window on a real 120 Hz output,
which this script deliberately never opens.

## Results

Measured 2026-11-15 on **omarchy** (AMD Ryzen AI 9 HX 470, 24 threads, 27 GiB), in the CI-matching Ubuntu 24.04
container (Mesa 25.2.8 llvmpipe), **under lavapipe/llvmpipe in a headless cage at 1280x720@120 Hz**. The machine
was shared with other build workers; the one-minute load average at each run's start is given, and the spread
between runs is most likely that contention. Three runs, nothing averaged away.

| Run | Load at start | First frame (ms) | First usable frame (ms) | Discarded |
| ---: | ---: | ---: | ---: | ---: |
| 1 | 6.69 | 943 | 2090 | 0 |
| 2 | 31.22 | 1204 | 4684 | 0 |
| 3 | 34.04 | 642 | 2439 | 0 |

| Phase | Run | Duration (ms) | Frames | p50 (ms) | p95 (ms) | Max (ms) | Late | Client CPU (ms) | GPU process CPU (ms) | Backend CPU (ms) | Client RSS (MiB) | Client PSS (MiB) | Backend RSS (MiB) |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| Cold start | 1 | 2079 | 6 | 86.12 | 674.45 | 674.45 | 4 | 2350 | 130 | 1620 | 848 | 531 | 426 |
| Cold start | 2 | 4640 | 5 | 129.68 | 2388.21 | 2388.21 | 4 | 3800 | 140 | 3680 | 836 | 510 | 352 |
| Cold start | 3 | 2404 | 5 | 27.41 | 1275.71 | 1275.71 | 3 | 2800 | 80 | 2630 | 835 | 517 | 426 |
| Idle | 1 | 2000 | 5 | 364.74 | 499.51 | 499.51 | 4 | 10 | 10 | 10 | 875 | 548 | 357 |
| Idle | 2 | 2001 | 4 | 502.64 | 502.78 | 502.78 | 3 | 20 | 20 | 20 | 863 | 527 | 357 |
| Idle | 3 | 2001 | 5 | 499.59 | 499.96 | 499.96 | 4 | 10 | 30 | 10 | 862 | 532 | 356 |
| Streamed reply | 1 | 6894 | 163 | 24.09 | 87.70 | 525.83 | 140 | 2710 | 1520 | 1530 | 928 | 590 | 447 |
| Streamed reply | 2 | 5713 | 183 | 17.97 | 80.67 | 315.93 | 144 | 1510 | 930 | 680 | 952 | 606 | 451 |
| Streamed reply | 3 | 5600 | 182 | 17.48 | 79.03 | 253.73 | 145 | 1590 | 720 | 870 | 870 | 530 | 436 |
| Feed scroll | 1 | 3699 | 148 | 22.34 | 43.26 | 138.74 | 131 | 2600 | 2700 | 20 | 952 | 607 | 447 |
| Feed scroll | 2 | 3601 | 229 | 16.10 | 21.36 | 57.11 | 182 | 2440 | 2490 | 300 | 998 | 643 | 374 |
| Feed scroll | 3 | 3601 | 231 | 15.17 | 22.61 | 94.65 | 162 | 2490 | 2160 | 20 | 878 | 530 | 436 |

**Reading it.**

- **Scrolling does not reach 120 Hz.** Across the three runs the feed scroll presents 148 to 231 frames in 3.6 s,
  p50 15.2 to 22.3 ms, with 131 to 182 late intervals: Electron makes roughly every second 120 Hz deadline. The
  most common interval in every trace is two refresh periods, and the shortest is 7.9 to 8.0 ms, so the output
  itself was at 120 Hz. The GPU process burns 2.2 to 2.7 s of CPU in those 3.6 s, as much as the rest of the
  client: in this rig that is software raster and compositing.
- **A streamed reply costs 1.5 to 2.7 s of client CPU and 0.7 to 1.5 s of GPU-process CPU** for a 5.6 to 6.9 s turn,
  with a p95 interval of 79 to 88 ms. The late count here includes the waits between chunks, so it is not a
  missed-deadline count.
- **Idle is not free**: four or five frames every two seconds, one every ~500 ms, from something in the window
  that repaints on a timer. The composer's blinking caret is the likely source; it was not isolated.
- **Cold start**: the first frame lands 0.6 to 1.2 s after spawn, the first usable one 2.1 to 4.7 s after. Client
  memory is 870 to 1000 MiB RSS (530 to 640 MiB PSS) once the feed has content, beside 370 to 450 MiB for the
  backend.

These are the numbers T3 Code on rnl_window is measured against, on this rig, by this script, once #641 lands.

