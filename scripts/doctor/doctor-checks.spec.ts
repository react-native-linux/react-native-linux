import type { Probe, Tier } from "./doctor-check.ts";
import { describe, expect, it } from "vitest";

import { doctorChecks } from "./doctor-checks.ts";

const TIERS: readonly Tier[] = ["core", "window", "goldens", "coverage"];
const PROBE_KINDS: readonly Probe["kind"][] = ["binary", "pkg-config", "file-glob", "env-var", "lavapipe-icd"];
const EMPTY_TEXT_LENGTH = 0;

describe("doctorChecks / structure", () => {
  it("gives every check a non-empty name, why and remedy", () => {
    for (const check of doctorChecks) {
      expect(check.name.length).toBeGreaterThan(EMPTY_TEXT_LENGTH);
      expect(check.why.length).toBeGreaterThan(EMPTY_TEXT_LENGTH);
      expect(check.remedy.arch.length).toBeGreaterThan(EMPTY_TEXT_LENGTH);
      expect(check.remedy.ubuntu.length).toBeGreaterThan(EMPTY_TEXT_LENGTH);
    }
  });

  it("gives every check a name unique across the registry", () => {
    const names = doctorChecks.map((check) => check.name);

    expect(new Set(names).size).toBe(names.length);
  });

  it("assigns every check one of the four documented tiers", () => {
    for (const check of doctorChecks) {
      expect(TIERS).toContain(check.tier);
    }
  });

  it("uses one of the documented probe kinds for every check", () => {
    for (const check of doctorChecks) {
      expect(PROBE_KINDS).toContain(check.probe.kind);
    }
  });
});

describe("doctorChecks / content", () => {
  it("requires CMake at least 3.28.0, matching cmake_minimum_required", () => {
    const cmakeCheck = doctorChecks.find((check) => check.name === "CMake");

    expect(cmakeCheck?.required).toBe(true);
    expect(cmakeCheck?.probe.kind === "binary" ? cmakeCheck.probe.minimumVersion : null).toBe("3.28.0");
  });

  it("exercises every probe kind at least once", () => {
    const usedKinds = new Set(doctorChecks.map((check) => check.probe.kind));

    for (const kind of PROBE_KINDS) {
      expect(usedKinds.has(kind)).toBe(true);
    }
  });

  it("marks Boost and glog as optional, since they have a source fallback", () => {
    const boostCheck = doctorChecks.find((check) => check.name === "Boost");
    const glogCheck = doctorChecks.find((check) => check.name === "glog");

    expect(boostCheck?.required).toBe(false);
    expect(glogCheck?.required).toBe(false);
  });

  it("marks the lavapipe rig as a required part of the goldens tier", () => {
    const lavapipeCheck = doctorChecks.find((check) => check.probe.kind === "lavapipe-icd");

    expect(lavapipeCheck?.tier).toBe("goldens");
    expect(lavapipeCheck?.required).toBe(true);
  });

  it("has at least one check per tier", () => {
    for (const tier of TIERS) {
      expect(doctorChecks.some((check) => check.tier === tier)).toBe(true);
    }
  });
});
