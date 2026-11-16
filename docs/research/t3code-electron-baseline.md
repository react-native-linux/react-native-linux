# T3 Code's Electron build in the software-rendering rig, with a GPU rung: exploratory runs and the method

- Part of #642 (epic #634). This is the Electron half of the comparison and the method both halves share. The
  rnl_window half waits on #641: T3 Code does not render on this platform yet.
- **The runs checked in here are exploratory.** They show that the method works and what it can see. They are not
  the bar rnl_window has to beat. The bar comes from the comparison protocol below, run once #641 lands, with both
  apps in one session.
- Measured with `pnpm perf:electron --t3code <checkout> --renderer <rung>` (`scripts/perf-electron.ts`), from the
  CI-matching container, which has Electron's `libnss3` and `libnspr4`. The script needs `rnl_inject` in
  `build/dev/bin`. The GPU rung also needs the DRM render node passed into the container
  (`--device /dev/dri/renderD128`) and nothing else: no display, no seat, no session. The llvmpipe rung needs the
  render node kept out. `--electron-arguments="<switches>"` adds Chromium switches, as the frame-clock probe does.

## The comparison protocol

The comparison counts only when it follows these rules:

- **Both apps run in one session**, interleaved E/R/E/R (Electron, rnl_window, Electron, rnl_window), at least three
  runs each, on a quiet machine.
- **Each run is pinned to the same CPUs** with `taskset`, and `LP_NUM_THREADS` is set to that CPU count and recorded,
  because llvmpipe and lavapipe size their thread pools from it.
- **Each run is pinned to one renderer rung** with `--renderer`, on both sides, and every table prints its rung.
  Matching rungs are compared, never across rungs.
- **Every run reports** the one-minute load average at its start, the CPUs it may use, and what Chromium reported
  about its own rendering path.

## Method

**The build.** T3 Code at commit `238678951a` (version 0.1.9), built the way its own CI builds the desktop app before
the launch check: `vp i`, `vp run --filter @t3tools/desktop ensure:electron`, `vp run build:desktop`. That gives
the production web bundle, the bundled server and `apps/desktop/dist-electron`, run by the Electron 44.10.1
binary it pins. The release AppImage packages the same three. Build it in a scratch clone, never in a working
checkout.

**The rig.** The headless cage from scripts/e2e.ts: wlroots' headless backend and no input devices, in a private
`XDG_RUNTIME_DIR`. Nothing touches a real display or session. Cage's child is not the app: it sets a 1280x720 custom
mode at the requested rate with `wlr-randr`, as #654's 120 Hz scenarios do, and then sleeps. Electron then connects
to cage's socket as an ordinary client, with `--ozone-platform=wayland --no-sandbox --remote-debugging-port=0`, and
cage maximises it to the output. The window rnl_window gets is the same size. Cage composites with pixman on the CPU,
except on the GPU rung: there it runs `WLR_RENDERER=gles2`, opens the render node itself, composites on the GPU and
still takes the 120 Hz custom mode. A render node grants no modesetting and no display, so that cage stays as
private as the others.

**The renderer rungs.** Each rung pins how Electron draws, and names the rnl_window `--renderer` rung it is compared
with. The last column is what Chromium reported through `SystemInfo.getInfo` and what the window attached, in every
run below.

| Rung | Electron | Cage | rnl_window pair | Chromium reported |
| --- | --- | --- | --- | --- |
| `software` | `--disable-gpu` | pixman | `--renderer raster` (`SharedMemoryRasterRenderer`, CPU Skia into `wl_shm`) | software compositing and raster, `wl_shm` buffers |
| `llvmpipe` | `--ignore-gpu-blocklist`, with no render node in reach | pixman | `--renderer software-vulkan` (Skia Graphite on lavapipe) | GL renderer llvmpipe through ANGLE, GPU raster, software compositing, `wl_shm` buffers |
| `gpu` | no switches | gles2 on the render node | `--renderer preferred-vulkan` | GL renderer AMD Radeon 890M (radeonsi) through ANGLE, GPU compositing and raster, `linux-dmabuf` buffers |

`software` is the path Chromium takes on its own wherever it blocklists the GPU, llvmpipe included: in the container
without the render node, Electron with no switches reports the same status. The switch only pins it, so the rung
cannot drift on a machine whose GPU Chromium accepts. `llvmpipe` cannot be pinned by a switch: with the render
node in reach, ANGLE took the Radeon even under `LIBGL_ALWAYS_SOFTWARE=1`, and the printed GL renderer showed it.
So that rung runs in the container without the render node, and its rows below say llvmpipe. The script checks
every rung against that column: a run whose Chromium drew another way (an llvmpipe run on the Radeon, a gpu or
llvmpipe run that fell back to software) exits non-zero instead of reporting.

One difference inside a pair is not the renderer: **rnl_window's raster rung repaints the whole surface every frame,
and Chromium damages only what changed** (`wl_surface.damage(256, 40, 1024, 680)` during the scroll, the feed and
not the sidebar). Until rnl_window sends partial damage, the raster pair charges it for more pixels per frame.

**The state.** A fresh T3 home seeded with the fixture T3 Code's own launch check opens
(`scripts/fixtures/desktop-launch/statev2.sqlite`: one project, two threads, an older schema the backend migrates
on start), plus a provider instance for T3 Code's replay agent (`apps/server/scripts/dev-replay-agent.ts`). The
replay agent plays recorded ACP turns with simulated streaming through the real adapter, orchestration and UI, so
**no step needs an account or a secret, and none is skipped**. Telemetry and the update check are off.

**The workload**, in order:

1. **Cold start**: spawn to observed readiness, the moment the harness saw the sidebar list the fixture's threads
   (watched over the DevTools protocol; see *Startup* below).
2. **Idle**: two seconds with the window open and nothing happening.
3. **Streamed reply**: a thread is started on the replay provider's `showcase` scenario (thinking, a plan, tool
   calls and markdown) with T3 Code's own CLI (`t3 thread start`, against the app's backend), opened by its
   route, and given three more turns so the feed is long. The measured phase is one more turn, from `t3 thread
   send --wait` to its return, while the window shows the thread.
4. **Scroll up** and 5. **Scroll down**: the wheel workload below, one phase per direction.

**The scroll workload, the contract both apps share.** Input enters the way a user's does: `rnl_inject` drives
cage's `zwlr_virtual_pointer_v1`, the path rnl_window's e2e scenarios already use, and Chromium receives ordinary
`wl_pointer.axis_value120` events.

- *Element*: T3 Code's timeline scroll container, `.messages-timeline-scroll` (the LegendList element in
  `MessagesTimeline`), chosen by selector. The pointer moves to its centre in output pixels.
- *Start endpoint*: the feed scrolled to its bottom, where a finished turn leaves it, and settled for two seconds.
- *Input*: 16 wheel notches up, one every 60 ms, then the pointer held for 400 ms; then the same 16 notches down.
  Each direction is one `rnl_inject` run and one phase, about 1.4 s long.
- *Checks*: the feed starts and ends at its bottom to within a pixel, and its highest point stops short of the top,
  so every notch moved it. A run that fails a check fails.
- *Distance*: measured, not assumed. Chromium moves T3 Code's feed 112.5 px a notch, **1800 px each way, in every
  run below**. rnl_window turns a notch into its own distance; its runs record theirs the same way.

The 400 ms hold is there because the seat loses its pointer when `rnl_inject` exits. In probe runs where the
pointer left while the feed was still animating, T3 Code's renderer crashed (exit 133) in two runs of six; with
the hold, none of the runs below crashed.

**What is recorded.**

- *Presentation intervals*: `WAYLAND_DEBUG=client` on the Electron process, and every
  `wp_presentation_feedback.presented` event in it. The compositor stamps those on `CLOCK_MONOTONIC`, the clock
  Node's `process.hrtime` reads, so each frame falls in a phase by its own timestamp. This is the signal
  rnl_window's frame log records, summarised the same way: nearest-rank p50 and p95 and the maximum over
  consecutive intervals inside the phase.
- *Late frames*: intervals longer than 1.5 refresh periods, so at least one deadline passed with no new frame.
  That is a missed deadline only while something moves every frame, as in the scroll. In the streamed reply it
  also counts the waits between chunks, and in cold start and idle it means nothing.
- *Startup*: spawn to the first presented frame, and to the first frame presented after **observed readiness**:
  the moment the harness first saw the sidebar's thread list in the DOM. In the page a MutationObserver watches
  for it, and after two animation frames, by which time the frame holding the list has been produced, the check
  replies with the time since it saw the list. When the list appears while the observer watches, that moment is
  React's commit, placed on `CLOCK_MONOTONIC` to within the reply's latency. When the list is already there as a
  check begins (DevTools attached after it rendered, or the commit fell between two checks, as around the window's
  one reload), the moment is the check's own, after the commit, and the report says so. Observed readiness is
  therefore never earlier than the commit, and the figure after it can only overstate Electron's startup. The runs
  below predate that note, so which of them were anchored on a check is not recorded. **rnl_window has no DOM, and
  its planned anchor is the first Fabric mount transaction that contains the sidebar's thread-item view**, stamped
  by the app at the commit itself rather than by a poll, so it is never late. Until Electron gets an anchor of the
  same kind, compare its startup only from runs whose readiness was observed at the commit.
- *CPU time and memory*, from /proc, for the app's process tree split three ways. **Client** is the main process,
  the renderer, the zygotes, the utility processes, and helpers the main process starts in Node mode for other
  jobs. **GPU process** is reported apart: on the two software rungs it rasterises or composites on the CPU.
  **Backend** is the process whose arguments hold the T3 server's entry script (`apps/server/dist/bin.mjs`) and
  everything under it, the replay agent included; the backend is not part of the window, and T3 Code on rnl_window
  talks to the same one. **Compositor** is cage's whole tree: on the pixman rungs it composites the window on the
  CPU, a cost rnl_window pays too because it runs in the same cage. CPU is utime plus stime spent inside the
  phase. Memory is RSS and PSS summed at the phase's end; PSS divides shared pages between processes and is the
  fairer number for a multi-process app.
- *Chromium's own report*: the GL renderer and the `gpu_compositing` and `rasterization` status from
  `SystemInfo.getInfo`, and how many `wl_shm` and `linux-dmabuf` buffers the window created.

**What is not recorded, and why.**

- *Input-to-photon latency*. The wheel now enters through the same virtual pointer rnl_window uses, so this is
  within reach: what is missing is matching each notch to the first frame that moved because of it. That is a
  follow-up.
- *A real panel*. Every number here is from the headless rig. A run on omarchy's own 120 Hz panel would need the
  window on the user's display, which this script never opens.

## Chromium's frame clock in this rig

wlroots' headless output reports a refresh of 0 in every `wp_presentation_feedback.presented` event, although
`wl_output.mode` advertises 120000 mHz. **Hypothesis: Chromium then keeps its default 60 Hz BeginFrame interval**,
and presents at most every second refresh whatever the rung. The probe rows below support it: with
`--disable-gpu-vsync --disable-frame-rate-limit`, which leave Wayland frame callbacks as the only pacing, the same
scroll runs at the output's rate on every rung, the software one included. On a real 120 Hz output the presented
events carry the refresh, so the default-clock rows most likely understate what Electron does there.

The probe switches are not a substitute: the streamed reply costs more than twice the CPU per presented frame with
them, most likely for frames Chromium draws and never submits. So the main tables keep Electron's default clock, and the protocol above holds both
apps to the same rig. Giving Chromium the output's real refresh in the rig is #700.

## Results

Measured 2026-11-15 on **omarchy** (AMD Ryzen AI 9 HX 470, 24 threads, Radeon 890M, 27 GiB) in the CI-matching
Ubuntu 24.04 container (Mesa 25.2.8), in a headless cage at 1280x720@120 Hz. Every run was pinned with
`taskset -c 12-23` and ran with `LP_NUM_THREADS=12`. The `software` and `gpu` runs were interleaved in one
session (software, llvmpipe, gpu, three rounds) at a load of 1.1 to 6.4. The llvmpipe runs from that session
reported the Radeon as their GL renderer and are discarded (see the rungs above). The llvmpipe rows below were
re-run without the render node half an hour later, **at a load of 11 to 13** from other work on the machine, so
they do not compare with the other two rungs. That load may have contributed to their slower startup, but these
runs cannot attribute the difference. Their run 2 failed the travel check (the feed came back 120 px short of its
bottom) and is left out. Nothing else is averaged away.

These are exploratory numbers. Every scroll and streamed-reply row here runs on Chromium's default frame clock,
which this rig caps at 60 Hz (see above).

### Rung `software` (pair: rnl_window `--renderer raster`)

| Run | Load at start | First frame (ms) | First frame after observed readiness (ms) | Discarded |
| ---: | ---: | ---: | ---: | ---: |
| 1 | 1.07 | 495 | 1415 | 0 |
| 2 | 1.37 | 389 | 1392 | 0 |
| 3 | 3.59 | 649 | 2191 | 0 |

| Phase | Run | Duration (ms) | Frames | p50 (ms) | p95 (ms) | Max (ms) | Late | Client CPU (ms) | GPU process CPU (ms) | Backend CPU (ms) | Compositor CPU (ms) | Client RSS (MiB) | Client PSS (MiB) | Backend RSS (MiB) |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| Cold start | 1 | 1423 | 6 | 112.59 | 622.18 | 622.18 | 5 | 1880 | 50 | 1420 | 0 | 876 | 496 | 425 |
| Cold start | 2 | 1416 | 9 | 8.59 | 657.69 | 657.69 | 4 | 1790 | 50 | 1530 | 0 | 861 | 484 | 427 |
| Cold start | 3 | 2234 | 9 | 46.73 | 1070.18 | 1070.18 | 7 | 2600 | 90 | 2310 | 0 | 873 | 511 | 427 |
| Idle | 1 | 2000 | 5 | 347.76 | 500.06 | 500.06 | 4 | 10 | 30 | 10 | 0 | 895 | 506 | 356 |
| Idle | 2 | 2000 | 5 | 308.62 | 499.18 | 499.18 | 4 | 20 | 20 | 0 | 0 | 890 | 504 | 357 |
| Idle | 3 | 2001 | 5 | 334.30 | 500.41 | 500.41 | 4 | 0 | 30 | 10 | 0 | 883 | 511 | 357 |
| Streamed reply | 1 | 5587 | 174 | 18.36 | 78.16 | 252.98 | 148 | 1290 | 620 | 1070 | 10 | 949 | 551 | 377 |
| Streamed reply | 2 | 5576 | 185 | 16.72 | 82.70 | 227.03 | 138 | 1370 | 660 | 930 | 20 | 945 | 551 | 379 |
| Streamed reply | 3 | 5511 | 174 | 17.53 | 81.20 | 168.73 | 147 | 1330 | 680 | 680 | 20 | 950 | 554 | 441 |
| Scroll up | 1 | 1415 | 77 | 16.13 | 25.29 | 53.40 | 57 | 950 | 500 | 40 | 40 | 977 | 564 | 376 |
| Scroll up | 2 | 1406 | 72 | 16.31 | 24.41 | 46.80 | 53 | 900 | 470 | 60 | 40 | 917 | 506 | 377 |
| Scroll up | 3 | 1418 | 80 | 15.82 | 25.18 | 50.68 | 53 | 880 | 660 | 50 | 50 | 981 | 570 | 369 |
| Scroll down | 1 | 1406 | 75 | 16.34 | 22.12 | 23.62 | 55 | 670 | 510 | 0 | 30 | 990 | 575 | 376 |
| Scroll down | 2 | 1407 | 88 | 13.79 | 25.85 | 29.95 | 54 | 820 | 530 | 0 | 50 | 930 | 535 | 377 |
| Scroll down | 3 | 1405 | 81 | 15.21 | 22.65 | 34.12 | 52 | 840 | 700 | 10 | 40 | 1005 | 590 | 369 |

### Rung `llvmpipe` (pair: rnl_window `--renderer software-vulkan`)

| Run | Load at start | First frame (ms) | First frame after observed readiness (ms) | Discarded |
| ---: | ---: | ---: | ---: | ---: |
| 1 | 13.07 | 1177 | 3742 | 0 |
| 3 | 12.91 | 738 | 2966 | 0 |
| 4 | 11.33 | 1072 | 3043 | 0 |

| Phase | Run | Duration (ms) | Frames | p50 (ms) | p95 (ms) | Max (ms) | Late | Client CPU (ms) | GPU process CPU (ms) | Backend CPU (ms) | Compositor CPU (ms) | Client RSS (MiB) | Client PSS (MiB) | Backend RSS (MiB) |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| Cold start | 1 | 3811 | 6 | 208.54 | 1734.46 | 1734.46 | 5 | 3440 | 170 | 2920 | 0 | 862 | 528 | 350 |
| Cold start | 3 | 3090 | 7 | 178.26 | 1453.26 | 1453.26 | 5 | 3190 | 120 | 2810 | 0 | 856 | 524 | 352 |
| Cold start | 4 | 3044 | 5 | 96.81 | 1471.99 | 1471.99 | 4 | 3060 | 200 | 2780 | 0 | 865 | 532 | 426 |
| Idle | 1 | 2001 | 4 | 501.31 | 509.98 | 509.98 | 3 | 20 | 30 | 10 | 0 | 872 | 506 | 355 |
| Idle | 3 | 2000 | 4 | 499.82 | 500.60 | 500.60 | 3 | 10 | 10 | 10 | 0 | 872 | 534 | 356 |
| Idle | 4 | 2001 | 4 | 501.01 | 501.34 | 501.34 | 3 | 10 | 20 | 10 | 0 | 876 | 538 | 357 |
| Streamed reply | 1 | 5966 | 167 | 20.26 | 82.48 | 106.70 | 141 | 1680 | 1130 | 920 | 20 | 981 | 606 | 447 |
| Streamed reply | 3 | 5879 | 183 | 16.85 | 79.94 | 314.86 | 143 | 1500 | 1020 | 810 | 20 | 992 | 644 | 449 |
| Streamed reply | 4 | 6787 | 173 | 22.07 | 83.05 | 503.20 | 134 | 2950 | 1470 | 1500 | 30 | 880 | 527 | 451 |
| Scroll up | 1 | 1442 | 71 | 16.66 | 29.03 | 73.41 | 57 | 1080 | 840 | 50 | 50 | 1040 | 670 | 447 |
| Scroll up | 3 | 1441 | 80 | 13.28 | 24.02 | 72.41 | 58 | 1250 | 840 | 50 | 50 | 1040 | 671 | 448 |
| Scroll up | 4 | 1438 | 70 | 16.54 | 23.82 | 51.39 | 59 | 930 | 790 | 60 | 50 | 913 | 550 | 450 |
| Scroll down | 1 | 1438 | 71 | 15.86 | 26.01 | 32.16 | 61 | 1150 | 780 | 0 | 50 | 1048 | 678 | 447 |
| Scroll down | 3 | 1411 | 73 | 15.82 | 28.26 | 36.54 | 57 | 940 | 850 | 0 | 50 | 1039 | 670 | 448 |
| Scroll down | 4 | 1429 | 84 | 11.92 | 23.95 | 34.89 | 40 | 900 | 720 | 250 | 50 | 940 | 569 | 373 |

### Rung `gpu` (pair: rnl_window `--renderer preferred-vulkan`)

| Run | Load at start | First frame (ms) | First frame after observed readiness (ms) | Discarded |
| ---: | ---: | ---: | ---: | ---: |
| 1 | 1.30 | 416 | 1378 | 0 |
| 2 | 3.53 | 491 | 1817 | 0 |
| 3 | 6.35 | 586 | 2284 | 0 |

| Phase | Run | Duration (ms) | Frames | p50 (ms) | p95 (ms) | Max (ms) | Late | Client CPU (ms) | GPU process CPU (ms) | Backend CPU (ms) | Compositor CPU (ms) | Client RSS (MiB) | Client PSS (MiB) | Backend RSS (MiB) |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| Cold start | 1 | 1410 | 8 | 83.61 | 595.37 | 595.37 | 6 | 1860 | 480 | 1480 | 0 | 960 | 574 | 427 |
| Cold start | 2 | 1750 | 7 | 47.45 | 823.91 | 823.91 | 5 | 2130 | 380 | 1830 | 10 | 948 | 551 | 425 |
| Cold start | 3 | 2170 | 6 | 126.69 | 1051.93 | 1051.93 | 5 | 2480 | 430 | 2230 | 0 | 949 | 545 | 427 |
| Idle | 1 | 2001 | 5 | 355.83 | 500.23 | 500.23 | 4 | 10 | 30 | 10 | 0 | 980 | 580 | 357 |
| Idle | 2 | 2000 | 5 | 483.98 | 501.00 | 501.00 | 4 | 20 | 20 | 10 | 0 | 976 | 586 | 356 |
| Idle | 3 | 2001 | 5 | 487.06 | 499.89 | 499.89 | 4 | 30 | 20 | 10 | 0 | 981 | 574 | 355 |
| Streamed reply | 1 | 5537 | 170 | 18.26 | 77.67 | 189.26 | 160 | 1370 | 500 | 660 | 10 | 1032 | 640 | 441 |
| Streamed reply | 2 | 5676 | 172 | 19.06 | 81.38 | 208.89 | 146 | 1540 | 620 | 840 | 30 | 1029 | 636 | 439 |
| Streamed reply | 3 | 5646 | 166 | 18.95 | 82.32 | 123.66 | 158 | 1320 | 530 | 770 | 30 | 1037 | 638 | 441 |
| Scroll up | 1 | 1417 | 72 | 16.84 | 26.27 | 49.61 | 48 | 630 | 280 | 230 | 30 | 1038 | 635 | 368 |
| Scroll up | 2 | 1444 | 73 | 15.95 | 28.00 | 59.89 | 45 | 890 | 450 | 80 | 40 | 1032 | 638 | 368 |
| Scroll up | 3 | 1447 | 73 | 16.21 | 29.00 | 72.65 | 51 | 590 | 350 | 70 | 30 | 1045 | 643 | 369 |
| Scroll down | 1 | 1403 | 73 | 16.46 | 25.01 | 32.07 | 50 | 600 | 270 | 10 | 30 | 1042 | 639 | 368 |
| Scroll down | 2 | 1422 | 77 | 14.31 | 24.94 | 31.37 | 48 | 880 | 400 | 10 | 50 | 1036 | 642 | 368 |
| Scroll down | 3 | 1410 | 73 | 16.05 | 25.53 | 27.53 | 52 | 700 | 430 | 0 | 50 | 1061 | 660 | 369 |

### The frame-clock probe

| Rung | Phase | Frames | p50 (ms) | p95 (ms) | Max (ms) | Late | Client CPU (ms) | GPU process CPU (ms) | Compositor CPU (ms) |
| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| software | Streamed reply | 243 | 10.68 | 73.31 | 214.38 | 95 | 3890 | 2550 | 30 |
| software | Scroll up | 130 | 8.53 | 10.49 | 50.18 | 4 | 1690 | 1130 | 80 |
| software | Scroll down | 142 | 8.48 | 9.99 | 18.76 | 3 | 1500 | 1200 | 90 |
| llvmpipe | Streamed reply | 259 | 8.08 | 72.78 | 243.19 | 89 | 3500 | 2640 | 30 |
| llvmpipe | Scroll up | 121 | 8.38 | 10.78 | 41.27 | 2 | 1600 | 1060 | 60 |
| llvmpipe | Scroll down | 138 | 8.39 | 8.49 | 8.56 | 0 | 1370 | 1140 | 60 |
| gpu | Streamed reply | 272 | 8.07 | 75.04 | 510.03 | 71 | 3830 | 2660 | 50 |
| gpu | Scroll up | 122 | 8.43 | 11.21 | 92.60 | 6 | 1520 | 1200 | 80 |
| gpu | Scroll down | 131 | 8.40 | 11.26 | 24.37 | 5 | 1260 | 1110 | 80 |

The probe rows are one run per rung, `--electron-arguments="--disable-gpu-vsync --disable-frame-rate-limit"`,
at a load of 7.8 to 12.4.

**Reading it.**

- **In the software-rendering rig, Electron's scroll does not reach 120 Hz on its default frame clock**, and
  neither does the GPU rung: on all three rungs the scroll presents 70 to 88 frames in 1.4 s, p50 11.9 to 16.8 ms,
  with 40 to 61 late intervals per direction, so Electron makes roughly every second deadline. The
  frame-clock probe puts that on Chromium's clock in this rig, not on rendering cost: with the clock uncapped, the
  same scroll runs at p50 8.4 to 8.5 ms with 0 to 6 late intervals on every rung, the software one included.
- **"CPU-bound, not compositor-bound" was a hypothesis, and the probe does not support it at this load.** The
  software rung's GPU process spends 6.0 to 8.6 ms of CPU per presented scroll frame (470 to 700 ms over 72 to 88
  frames), under one refresh period, and uncapped it keeps up with every refresh. Whether a heavier feed or a
  loaded machine makes it CPU-bound is open.
- **A streamed reply costs 1.3 to 1.5 s of client CPU and 0.5 to 0.7 s of GPU-process CPU** for a 5.5 to 5.7 s
  turn on the quiet software and gpu runs (1.5 to 3.0 s and 1.0 to 1.5 s on the loaded llvmpipe runs), with a p95
  interval of 78 to 83 ms. The late count includes the waits between chunks, so it is not a
  missed-deadline count. With the clock uncapped the same turn costs 3.5 to 3.9 s and 2.6 s, more than twice as
  much per presented frame. The likely reason is frames Chromium draws and never submits (the compositor discarded
  none), which is why the probe switches are not a baseline.
- **The compositor's share is small**: cage spends 30 to 60 ms of CPU per scroll direction compositing on pixman,
  and the same on gles2, against 0.9 to 2.1 s for Electron's client and GPU process together.
- **Idle is not free**: four or five frames every two seconds, one every ~500 ms, from something in the window that
  repaints on a timer. The composer's blinking caret is the likely source; it was not isolated.
- **Cold start**, on the quiet software and gpu runs: the first frame lands 0.4 to 0.65 s after spawn, and the
  first frame after observed readiness 1.4 to 2.3 s after it. Client memory is 880 to 1060 MiB RSS (510 to 660 MiB
  PSS) once the feed has content, beside 370 to 450 MiB for the backend. The GPU rung's client is about 60 MiB larger.

These runs show the method end to end. The numbers rnl_window is held to come from the protocol above, after #641
and #700.
