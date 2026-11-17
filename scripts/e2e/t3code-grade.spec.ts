import { afterEach, beforeEach, describe, expect, it } from "vitest";
import { mkdirSync, mkdtempSync, rmSync, writeFileSync } from "node:fs";
import { PNG } from "pngjs";
import { gradeMainScreenRun } from "./t3code.ts";
import path from "node:path";
import { tmpdir } from "node:os";

const SURFACE_WIDTH = 1280;
const SURFACE_HEIGHT = 720;
const MAIN_SCREEN_HEIGHT = 640;
const BACKGROUND = 250;
const TOAST = 40;
const CHANNELS_PER_PIXEL = 4;

const cleanRun = {
  exitCode: 0,
  hasSocket: true,
  isTraceClosed: true,
  lastNames: ["Select a thread", "New thread"],
  mainScreenMs: 3100,
  signal: null,
  trace: "[rnl-present] first frame presented\n",
};

/** A surface of `BACKGROUND` `height` rows tall, with LogBox's toast, a colour of its own, in the rows below 640. */
const encodeSurface = (height: number): Buffer => {
  const image = new PNG({ height, width: SURFACE_WIDTH });

  image.data.fill(BACKGROUND);
  image.data.fill(TOAST, MAIN_SCREEN_HEIGHT * SURFACE_WIDTH * CHANNELS_PER_PIXEL);

  return PNG.sync.write(image);
};

let workspace = "";
let goldensDirectory = "";
let screenshotPath = "";

beforeEach(() => {
  workspace = mkdtempSync(path.join(tmpdir(), "rnl-t3code-grade-"));
  goldensDirectory = path.join(workspace, "goldens");
  screenshotPath = path.join(workspace, "screenshot.png");
  mkdirSync(goldensDirectory);
});

afterEach(() => {
  rmSync(workspace, { force: true, recursive: true });
});

describe("gradeMainScreenRun", () => {
  it("passes a clean run whose screenshot above LogBox's toast matches the golden", () => {
    writeFileSync(screenshotPath, encodeSurface(SURFACE_HEIGHT));
    writeFileSync(path.join(goldensDirectory, "t3code-main-screen.png"), encodeSurface(MAIN_SCREEN_HEIGHT));

    expect(gradeMainScreenRun(cleanRun, goldensDirectory, screenshotPath)).toStrictEqual({ failures: [], notes: [] });
  });

  it("names the cropped picture to bless while the golden does not exist yet", () => {
    writeFileSync(screenshotPath, encodeSurface(SURFACE_HEIGHT));

    expect(gradeMainScreenRun(cleanRun, goldensDirectory, screenshotPath).notes).toStrictEqual([
      `no screenshot golden at ${path.join(goldensDirectory, "t3code-main-screen.png")}; bless ${path.join(workspace, "screenshot-cropped.png")}`,
    ]);
  });

  it("fails a clean run whose window captured no screenshot", () => {
    expect(gradeMainScreenRun(cleanRun, goldensDirectory, screenshotPath).failures).toStrictEqual([
      `the window captured no screenshot at ${screenshotPath}`,
    ]);
  });

  it("grades no screenshot for a run that never showed the main screen", () => {
    expect(gradeMainScreenRun({ ...cleanRun, mainScreenMs: null }, goldensDirectory, screenshotPath)).toStrictEqual({
      failures: [
        'T3 Code\'s main screen never showed its "New thread" button; the last accessibility tree named "Select a thread", "New thread"',
      ],
      notes: [],
    });
  });
});
