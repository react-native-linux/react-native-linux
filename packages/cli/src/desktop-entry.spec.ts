import { InvalidIconValueError, generateDesktopEntry } from "./desktop-entry.ts";
import { describe, expect, it } from "vitest";
import { mkdtempSync, rmSync, writeFileSync } from "node:fs";
import { execFileSync } from "node:child_process";
import path from "node:path";
import { tmpdir } from "node:os";

const baseManifest = {
  applicationIdentifier: "org.reactnativelinux.Flagship",
  categories: ["Game", "LogicGame"],
  displayName: "Flagship",
  exec: "/usr/bin/flagship",
  icon: "org.reactnativelinux.Flagship",
};

describe("generateDesktopEntry, no schemes and no associations", () => {
  it("names the entry after the application identifier", () => {
    const entry = generateDesktopEntry(baseManifest);

    expect(entry.basename).toBe("org.reactnativelinux.Flagship.desktop");
  });

  it("renders every required key and no MimeType line", () => {
    const entry = generateDesktopEntry(baseManifest);

    expect(entry.contents).toContain("[Desktop Entry]\n");
    expect(entry.contents).toContain("Type=Application\n");
    expect(entry.contents).toContain("Name=Flagship\n");
    expect(entry.contents).toContain("Exec=/usr/bin/flagship\n");
    expect(entry.contents).toContain("Icon=org.reactnativelinux.Flagship\n");
    expect(entry.contents).toContain("Categories=Game;LogicGame;\n");
    expect(entry.contents).toContain("Terminal=false\n");
    expect(entry.contents).toContain("StartupWMClass=org.reactnativelinux.Flagship\n");
    expect(entry.contents).not.toContain("MimeType=");
  });
});

describe("generateDesktopEntry, url schemes and file associations", () => {
  it("appends %u and a MimeType line when url schemes are declared", () => {
    const entry = generateDesktopEntry({ ...baseManifest, urlSchemes: ["flagship"] });

    expect(entry.contents).toContain("Exec=/usr/bin/flagship %u");
    expect(entry.contents).toContain("MimeType=x-scheme-handler/flagship;");
  });

  it("appends %f and a MimeType line when only file associations are declared", () => {
    const entry = generateDesktopEntry({ ...baseManifest, fileAssociationMimeTypes: ["application/x-flagship"] });

    expect(entry.contents).toContain("Exec=/usr/bin/flagship %f");
    expect(entry.contents).toContain("MimeType=application/x-flagship;");
  });

  it("prefers %u and merges both mime lists when schemes and associations are both declared", () => {
    const entry = generateDesktopEntry({
      ...baseManifest,
      fileAssociationMimeTypes: ["application/x-flagship"],
      urlSchemes: ["flagship"],
    });

    expect(entry.contents).toContain("Exec=/usr/bin/flagship %u");
    expect(entry.contents).toContain("MimeType=x-scheme-handler/flagship;application/x-flagship;");
  });
});

describe("generateDesktopEntry, Name and Comment", () => {
  it("passes a display name containing a space through unescaped", () => {
    const entry = generateDesktopEntry({ ...baseManifest, displayName: "React Native Flagship" });

    expect(entry.contents).toContain("Name=React Native Flagship");
  });

  it("passes a non-ASCII display name through unescaped", () => {
    const entry = generateDesktopEntry({ ...baseManifest, displayName: "Flagship Café" });

    expect(entry.contents).toContain("Name=Flagship Café");
  });

  it("omits the Comment line when no comment is given", () => {
    const entry = generateDesktopEntry(baseManifest);

    expect(entry.contents).not.toContain("Comment=");
  });

  it("emits an escaped Comment line when a comment is given", () => {
    const entry = generateDesktopEntry({ ...baseManifest, comment: "A path\\separator and a line\nbreak" });

    expect(entry.contents).toContain(String.raw`Comment=A path\\separator and a line\nbreak`);
  });
});

describe("generateDesktopEntry, list and value escaping", () => {
  it("escapes a literal semicolon inside a category", () => {
    const entry = generateDesktopEntry({ ...baseManifest, categories: ["Weird;Category"] });

    expect(entry.contents).toContain(String.raw`Categories=Weird\;Category;`);
  });

  it("quotes the executable when it contains a space", () => {
    const entry = generateDesktopEntry({ ...baseManifest, exec: "/opt/react native/flagship" });

    expect(entry.contents).toContain('Exec="/opt/react native/flagship"');
  });

  it("escapes backslashes and double quotes inside a quoted executable, doubling each backslash again for the general value-escaping rule", () => {
    const entry = generateDesktopEntry({ ...baseManifest, exec: '/opt/weird "path"\\bin flagship' });

    expect(entry.contents).toContain(String.raw`Exec="/opt/weird \\"path\\"\\\\bin flagship"`);
  });
});

describe("generateDesktopEntry, spec-conformant Icon", () => {
  it("accepts an absolute path as the Icon value", () => {
    const icon = "/usr/share/icons/hicolor/256x256/apps/flagship.png";
    const entry = generateDesktopEntry({ ...baseManifest, icon });

    expect(entry.contents).toContain(`Icon=${icon}`);
  });

  it("accepts a themed icon name with no path separator as the Icon value", () => {
    const entry = generateDesktopEntry({ ...baseManifest, icon: "org.reactnativelinux.Flagship" });

    expect(entry.contents).toContain("Icon=org.reactnativelinux.Flagship");
  });

  it("rejects an Icon value that is a relative path", () => {
    expect(() => generateDesktopEntry({ ...baseManifest, icon: "icons/flagship" })).toThrow(InvalidIconValueError);
  });

  it("rejects an empty Icon value", () => {
    expect(() => generateDesktopEntry({ ...baseManifest, icon: "" })).toThrow(InvalidIconValueError);
  });
});

describe("generateDesktopEntry, the identity invariant", () => {
  it("keeps the basename and StartupWMClass equal to applicationIdentifier, the same value passed as --app-id", () => {
    const entry = generateDesktopEntry(baseManifest);
    const startupWmClassLine = entry.contents.split("\n").find((line) => line.startsWith("StartupWMClass="));

    expect(entry.basename).toBe(`${baseManifest.applicationIdentifier}.desktop`);
    expect(startupWmClassLine).toBe(`StartupWMClass=${baseManifest.applicationIdentifier}`);
  });

  it("validates as a spec-conformant desktop entry under desktop-file-validate", () => {
    const entry = generateDesktopEntry({ ...baseManifest, urlSchemes: ["flagship"] });
    const directory = mkdtempSync(path.join(tmpdir(), "desktop-entry-"));
    const entryPath = path.join(directory, entry.basename);
    writeFileSync(entryPath, entry.contents);

    try {
      expect(() => execFileSync("desktop-file-validate", [entryPath], { stdio: "pipe" })).not.toThrow();
    } finally {
      rmSync(directory, { force: true, recursive: true });
    }
  });
});
