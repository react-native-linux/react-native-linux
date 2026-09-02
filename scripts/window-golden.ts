import { argv, env, pid, stderr, stdout } from "node:process";
import { existsSync, mkdirSync, mkdtempSync, readdirSync, rmSync, writeFileSync } from "node:fs";
import { spawn, spawnSync } from "node:child_process";
import { setTimeout as delay } from "node:timers/promises";
import path from "node:path";
import { pathToFileURL } from "node:url";
import { tmpdir } from "node:os";

const FAILURE_EXIT_STATUS = 1;
const UNAVAILABLE_EXIT_STATUS = 2;
const SUCCESSFUL_EXIT_STATUS = 0;
const NOT_FOUND_INDEX = -1;
const NEXT_ARGUMENT = 1;
const FIRST_MATCH = 0;
const SOCKET_TIMEOUT_MS = 15_000;
const SOCKET_POLL_INTERVAL_MS = 50;
const COMPOSITOR_STOP_GRACE_MS = 250;
const RENDER_TIMEOUT_MS = 120_000;
const SCREENSHOT_FRAME_COUNT = "60";
const WINDOW_WIDTH = "800";
const WINDOW_HEIGHT = "600";

const COMPOSITOR_NAME = "weston";
const OUTPUT_FLAG = "--output";

const lavapipeIcdPattern = /^lvp_icd\..+\.json$/u;
const icdDirectories = ["/usr/share/vulkan/icd.d", "/etc/vulkan/icd.d", "/usr/local/share/vulkan/icd.d"];
const lavapipeLibraryDirectories = ["/usr/lib/x86_64-linux-gnu", "/usr/lib64", "/usr/lib"];
const lavapipeLibraryName = "libvulkan_lvp.so";
const lavapipeOverrideName = "RNL_LAVAPIPE_ICD";
const generatedManifestName = "rnl-lvp_icd.generated.json";

/**
 * Every variable that could point the client at the developer's own session or driver. The rig owns all four, so
 * they are dropped from the inherited environment rather than overwritten, and a stale one cannot survive.
 */
const overriddenEnvironmentNames = new Set(["DISPLAY", "WAYLAND_DISPLAY", "VK_DRIVER_FILES", "VK_ICD_FILENAMES"]);

interface WindowFixture {
  readonly bundleFileName: string;
  readonly goldenFileName: string;
}

const repositoryRoot = path.resolve(import.meta.dirname, "..");
const packageDirectory = path.join(repositoryRoot, "packages", "core");
const bundlesDirectory = path.join(packageDirectory, "test-bundles");
const binaryPath = path.join(repositoryRoot, "build", "dev", "bin", "rnl_window");
const defaultOutputDirectory = path.join(repositoryRoot, "build", "window-goldens");

const fixtures: readonly WindowFixture[] = [
  { bundleFileName: "fabric-view.js", goldenFileName: "window-fabric-view.png" },
  { bundleFileName: "view-props.js", goldenFileName: "window-view-props.png" },
];

const findExecutable = (executableName: string): string | null => {
  for (const searchDirectory of (env["PATH"] ?? "").split(path.delimiter)) {
    const candidate = path.join(searchDirectory, executableName);

    if (searchDirectory !== "" && existsSync(candidate)) {
      return candidate;
    }
  }

  return null;
};

const writeGeneratedManifest = (libraryPath: string): string => {
  const manifestPath = path.join(tmpdir(), generatedManifestName);

  writeFileSync(
    manifestPath,
    JSON.stringify({ ICD: { api_version: "1.3.0", library_path: libraryPath }, file_format_version: "1.0.0" }),
  );

  return manifestPath;
};

const findLavapipeIcdManifestPath = (): string | null => {
  const override = env[lavapipeOverrideName];

  if (typeof override === "string" && override !== "" && existsSync(override)) {
    return override;
  }

  for (const icdDirectory of icdDirectories) {
    const manifests = existsSync(icdDirectory)
      ? readdirSync(icdDirectory).filter((entry) => lavapipeIcdPattern.test(entry))
      : [];
    const manifest = manifests[FIRST_MATCH] ?? null;

    if (manifest !== null) {
      return path.join(icdDirectory, manifest);
    }
  }

  return null;
};

const findLavapipeLibraryPath = (): string | null => {
  for (const libraryDirectory of lavapipeLibraryDirectories) {
    const libraryPath = path.join(libraryDirectory, lavapipeLibraryName);

    if (existsSync(libraryPath)) {
      return libraryPath;
    }
  }

  return null;
};

const findLavapipeIcd = (): string | null => {
  const manifestPath = findLavapipeIcdManifestPath();

  if (manifestPath !== null) {
    return manifestPath;
  }

  const libraryPath = findLavapipeLibraryPath();

  return libraryPath === null ? null : writeGeneratedManifest(libraryPath);
};

const buildEnvironment = (overrides: Record<string, string>): Record<string, string | undefined> => {
  const inherited = Object.fromEntries(
    Object.entries(env).filter(([variableName]) => !overriddenEnvironmentNames.has(variableName)),
  );

  return { ...inherited, ...overrides };
};

const readOutputDirectory = (): string => {
  const flagIndex = argv.indexOf(OUTPUT_FLAG);

  if (flagIndex === NOT_FOUND_INDEX) {
    return defaultOutputDirectory;
  }

  return argv[flagIndex + NEXT_ARGUMENT] ?? defaultOutputDirectory;
};

let compositorLog = "";

/**
 * The headless backend needs no DRM device, no seat and no display, which is what lets it run unattended on a CI
 * runner. Which renderer it uses never matters here: the golden's pixels come out of the swapchain image, so the
 * compositor only has to accept and release buffers. `--no-config` keeps a developer's `weston.ini` out of the rig.
 */
const startCompositor = (
  compositorPath: string,
  runtimeDirectory: string,
  socketName: string,
): ReturnType<typeof spawn> => {
  const compositor = spawn(
    compositorPath,
    [
      "--backend=headless",
      "--no-config",
      `--socket=${socketName}`,
      `--width=${WINDOW_WIDTH}`,
      `--height=${WINDOW_HEIGHT}`,
      "--idle-time=0",
    ],
    { env: buildEnvironment({ XDG_RUNTIME_DIR: runtimeDirectory }), stdio: ["ignore", "pipe", "pipe"] },
  );

  const recordCompositorOutput = (chunk: Buffer): void => {
    compositorLog += chunk.toString();
  };

  compositor.stdout?.on("data", recordCompositorOutput);
  compositor.stderr?.on("data", recordCompositorOutput);
  compositor.on("error", (error: Error) => {
    compositorLog += error.message;
  });

  return compositor;
};

const waitForCompositorSocket = async (compositor: ReturnType<typeof spawn>, socketPath: string): Promise<void> => {
  const deadline = Date.now() + SOCKET_TIMEOUT_MS;

  while (!existsSync(socketPath)) {
    if (compositor.exitCode !== null || Date.now() > deadline) {
      throw new Error(`${COMPOSITOR_NAME} never created ${socketPath}:\n${compositorLog}`);
    }

    await delay(SOCKET_POLL_INTERVAL_MS);
  }
};

const stopCompositor = async (compositor: ReturnType<typeof spawn>, runtimeDirectory: string): Promise<void> => {
  compositor.kill("SIGTERM");
  await delay(COMPOSITOR_STOP_GRACE_MS);
  compositor.kill("SIGKILL");
  rmSync(runtimeDirectory, { force: true, recursive: true });
};

const renderFixture = (
  fixture: WindowFixture,
  outputPath: string,
  clientEnvironment: Record<string, string | undefined>,
): void => {
  const render = spawnSync(
    binaryPath,
    [
      "--fabric",
      path.join(bundlesDirectory, fixture.bundleFileName),
      "--screenshot",
      outputPath,
      "--frames",
      SCREENSHOT_FRAME_COUNT,
    ],
    { encoding: "utf8", env: clientEnvironment, timeout: RENDER_TIMEOUT_MS },
  );

  if (render.status !== SUCCESSFUL_EXIT_STATUS || !existsSync(outputPath)) {
    throw new Error(
      `rnl_window --screenshot exited with status ${String(render.status)} for ${fixture.bundleFileName}:\n` +
        `${render.stdout}${render.stderr}\n${COMPOSITOR_NAME}:\n${compositorLog}`,
    );
  }

  stdout.write(`rendered ${outputPath}\n`);
};

const renderFixtures = async (
  compositorPath: string,
  lavapipeIcdPath: string,
  outputDirectory: string,
): Promise<void> => {
  const runtimeDirectory = mkdtempSync(path.join(tmpdir(), "rnl-window-golden-"));
  const socketName = `rnl-window-golden-${String(pid)}`;
  const compositor = startCompositor(compositorPath, runtimeDirectory, socketName);
  const clientEnvironment = buildEnvironment({
    VK_ICD_FILENAMES: lavapipeIcdPath,
    WAYLAND_DISPLAY: socketName,
    XDG_RUNTIME_DIR: runtimeDirectory,
  });

  try {
    await waitForCompositorSocket(compositor, path.join(runtimeDirectory, socketName));

    for (const fixture of fixtures) {
      renderFixture(fixture, path.join(outputDirectory, fixture.goldenFileName), clientEnvironment);
    }
  } finally {
    await stopCompositor(compositor, runtimeDirectory);
  }
};

const isMainModule = (): boolean => {
  const [, entryPath] = argv;

  return typeof entryPath === "string" && import.meta.url === pathToFileURL(entryPath).href;
};

if (isMainModule()) {
  const compositorPath = findExecutable(COMPOSITOR_NAME);
  const lavapipeIcdPath = findLavapipeIcd();
  const hasBinary = existsSync(binaryPath);

  const unavailableReasons = [
    ...(hasBinary ? [] : [`${binaryPath} is missing; build it with "cmake --build build/dev --target rnl_window"`]),
    ...(compositorPath === null ? [`${COMPOSITOR_NAME} is not on PATH; install the "weston" package`] : []),
    ...(lavapipeIcdPath === null
      ? [
          `no lavapipe ICD manifest under ${icdDirectories.join(" or ")} and no ${lavapipeLibraryName} under ${lavapipeLibraryDirectories.join(" or ")} (set ${lavapipeOverrideName} to a manifest path); ` +
            'install "vulkan-swrast" on Arch or "mesa-vulkan-drivers" on Ubuntu',
        ]
      : []),
  ];

  if (hasBinary && compositorPath !== null && lavapipeIcdPath !== null) {
    const outputDirectory = readOutputDirectory();

    mkdirSync(outputDirectory, { recursive: true });

    try {
      await renderFixtures(compositorPath, lavapipeIcdPath, outputDirectory);
    } catch (error) {
      stderr.write(`${error instanceof Error ? error.message : String(error)}\n`);
      process.exitCode = FAILURE_EXIT_STATUS;
    }
  } else {
    for (const reason of unavailableReasons) {
      stderr.write(`${reason}\n`);
    }

    process.exitCode = UNAVAILABLE_EXIT_STATUS;
  }
}

export { findLavapipeIcdManifestPath, findLavapipeLibraryPath };
