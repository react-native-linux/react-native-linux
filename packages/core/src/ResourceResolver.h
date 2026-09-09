#pragma once

#include <filesystem>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace react_native_linux {

/**
 * The resource resolver of #361: one stated, ordered search path for everything the running process finds on
 * disk — the JavaScript bundle, the vendored fonts, image assets — and the XDG base-directory rule for
 * everything it writes. Every consumer uses this and nothing else, because a second way to find a file is the
 * tauri#10078 shape: working from the build tree, broken from the installed artifact.
 *
 * The sources, in order:
 *
 * 1. the explicit override (`RNL_RESOURCE_ROOT`), for the harness and for a developer pointing at a tree —
 *    when set it is the *only* source searched, and a resource it does not contain fails rather than falling
 *    through, because a silently-ignored override is a harness testing the wrong tree;
 * 2. the portable-bundle root, when the process runs inside one (`portable.marker` beside the executable);
 * 3. the installed prefix's resource directory, derived from the executable path with every symlink followed
 *    (the tauri `restart` shape — an application is launched through symlinks, not only directly);
 * 4. the build tree, only when the development marker is present.
 *
 * A resource missing from every source fails naming the resource and every directory that was searched — the
 * #314 rule, generalised from a missing pinned font to any resource.
 *
 * Threading contract: every function here is pure over its inputs and the files they name — no environment
 * reads, no process state. The runtime assembly that reads the environment and `/proc/self/exe` is
 * `runtimeFontSources`/`runtimeBundledFontDirectory`, and callers that want the answer for the process's whole
 * lifetime cache it in their own static, which C++ initialises exactly once per process.
 */
class ResourceResolver final {
public:
    struct Sources {
        std::optional<std::filesystem::path> overrideRoot;
        std::optional<std::filesystem::path> portableRoot;
        std::optional<std::filesystem::path> installedResourceRoot;
        std::optional<std::filesystem::path> buildTreeRoot;
    };

    explicit ResourceResolver(Sources sources);

    /**
     * The first `<root>/<relativePath>` that exists wins. The override, when set, is the only source searched;
     * a miss everywhere throws `ResourceNotFoundError` naming the relative path and every directory searched.
     */
    [[nodiscard]] std::filesystem::path resolve(const std::string& relativePath) const;

    /** The directories the last `resolve` searched, in order — what a failure message needs. */
    [[nodiscard]] const std::vector<std::filesystem::path>& searchedDirectories() const noexcept;

    /** The executable path with every symlink followed — one symlink, or a nested chain. */
    [[nodiscard]] static std::filesystem::path resolveExecutablePath(const std::filesystem::path& executablePath);

    /** The portable-bundle root: the executable's directory, when it contains the bundle marker file. */
    [[nodiscard]] static std::optional<std::filesystem::path>
    detectPortableRoot(const std::filesystem::path& executablePath);

    /**
     * The build-tree font directory the build bakes in, only when the development marker sits beside it — the
     * CMakeLists of the vendored-font directory, which exists in a build tree and cannot exist under an
     * install prefix.
     */
    [[nodiscard]] static std::optional<std::filesystem::path>
    developmentFontDirectory(const std::filesystem::path& bakedFontDirectory);

    /** The sources for the font directory, from the process's environment and executable. */
    [[nodiscard]] static Sources runtimeSources(const std::optional<std::filesystem::path>& bakedFontDirectory);

    /**
     * The font directory the three `SkFontMgr_New_Custom_Directory` call sites use, resolved through the whole
     * ordered search: `RNL_RESOURCE_ROOT`, the portable root, the installed prefix, the build tree. Reads the
     * environment on every call — callers that want the answer for the process's lifetime cache it themselves.
     */
    [[nodiscard]] static std::filesystem::path
    runtimeFontDirectory(const std::optional<std::filesystem::path>& bakedFontDirectory);

    /**
     * XDG Base Directory: `$XDG_<NAME>_HOME` when set to an absolute path, else `<home>/<defaultSubdirectory>`.
     * A relative value is ignored, which the specification requires; a directory that does not exist yet is
     * still returned, because creating it is the consumer's job at first write.
     */
    [[nodiscard]] static std::filesystem::path writableDirectory(const std::string& envValue, bool envSet,
                                                                 const std::filesystem::path& home,
                                                                 const std::string& defaultSubdirectory);

private:
    Sources sources_;
    mutable std::vector<std::filesystem::path> searchedDirectories_;
};

/** Thrown when a resource is missing from every source; the message names the resource and every directory. */
class ResourceNotFoundError : public std::runtime_error {
public:
    ResourceNotFoundError(const std::string& relativePath, const std::vector<std::filesystem::path>& searched);
};

} // namespace react_native_linux
