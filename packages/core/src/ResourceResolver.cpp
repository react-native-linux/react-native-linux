#include "ResourceResolver.h"

#include <algorithm>
#include <cstdlib>
#include <stdexcept>
#include <utility>
#include <vector>

namespace react_native_linux {

namespace {

std::string formatMissingResourceMessage(const std::string& relativePath,
                                         const std::vector<std::filesystem::path>& searched) {
    std::string message = "the resource \"" + relativePath + "\" was not found in any of " +
                          std::to_string(searched.size()) + " searched directories:";

    for (const std::filesystem::path& directory : searched) {
        message += " " + directory.string() + ";";
    }

    return message;
}

} // namespace

ResourceResolver::ResourceResolver(Sources sources) : sources_(std::move(sources)) {}

std::filesystem::path ResourceResolver::resolve(const std::string& relativePath) const {
    searchedDirectories_.clear();

    std::vector<std::filesystem::path> candidateRoots;

    if (sources_.overrideRoot.has_value()) {
        // The override is the whole search: a harness that points it at a tree must never be answered from
        // another source, and a resource the override tree lacks is a failure, not a fall-through.
        candidateRoots.push_back(*sources_.overrideRoot);
    } else {
        if (sources_.portableRoot.has_value()) {
            candidateRoots.push_back(*sources_.portableRoot);
        }

        if (sources_.installedResourceRoot.has_value()) {
            candidateRoots.push_back(*sources_.installedResourceRoot);
        }

        if (sources_.buildTreeRoot.has_value()) {
            candidateRoots.push_back(*sources_.buildTreeRoot);
        }
    }

    for (const std::filesystem::path& root : candidateRoots) {
        const std::filesystem::path candidate = root / relativePath;

        searchedDirectories_.push_back(root);

        std::error_code existenceError;

        if (std::filesystem::exists(candidate, existenceError)) {
            return candidate;
        }
    }

    throw ResourceNotFoundError(relativePath, searchedDirectories_);
}

const std::vector<std::filesystem::path>& ResourceResolver::searchedDirectories() const noexcept {
    return searchedDirectories_;
}

std::filesystem::path ResourceResolver::resolveExecutablePath(const std::filesystem::path& executablePath) {
    return std::filesystem::weakly_canonical(executablePath);
}

std::optional<std::filesystem::path> ResourceResolver::detectPortableRoot(const std::filesystem::path& executablePath) {
    const std::filesystem::path executableDirectory = executablePath.parent_path();
    const std::filesystem::path marker = executableDirectory / "portable.marker";

    std::error_code existenceError;

    if (std::filesystem::exists(marker, existenceError)) {
        return executableDirectory;
    }

    return std::nullopt;
}

std::optional<std::filesystem::path>
ResourceResolver::developmentFontDirectory(const std::filesystem::path& bakedFontDirectory) {
    // The CMakeLists beside the vendored fonts exists only in the build tree, never under an install prefix.
    const std::filesystem::path developmentMarker = bakedFontDirectory / ".." / "CMakeLists.txt";

    std::error_code existenceError;

    if (std::filesystem::exists(developmentMarker, existenceError)) {
        // Normalise away the `..` segments a baked path can carry, so callers compare one canonical form.
        return resolveExecutablePath(bakedFontDirectory);
    }

    return std::nullopt;
}

ResourceResolver::Sources
ResourceResolver::runtimeSources(const std::optional<std::filesystem::path>& bakedFontDirectory) {
    const char* overrideRoot = std::getenv("RNL_RESOURCE_ROOT");
    const std::filesystem::path executable = resolveExecutablePath("/proc/self/exe");

    return {
        .overrideRoot = overrideRoot == nullptr ? std::nullopt : std::optional<std::filesystem::path>(overrideRoot),
        .portableRoot = detectPortableRoot(executable),
        .installedResourceRoot = executable.parent_path() / ".." / "share" / "react-native-linux",
        // The development font directory is the resource itself; the search resolves `fonts` under the root,
        // so the build-tree root is its parent (the package directory the vendored fonts sit beside).
        .buildTreeRoot = bakedFontDirectory.has_value()
            ? [&bakedFontDirectory] {
                  const std::optional<std::filesystem::path> development =
                      developmentFontDirectory(*bakedFontDirectory);

                  return development.has_value()
                             ? std::optional<std::filesystem::path>(development->parent_path())
                             : std::nullopt;
              }()
            : std::nullopt,
    };
}

std::filesystem::path
ResourceResolver::runtimeFontDirectory(const std::optional<std::filesystem::path>& bakedFontDirectory) {
    const ResourceResolver resolver(runtimeSources(bakedFontDirectory));

    return resolver.resolve("fonts");
}

std::filesystem::path ResourceResolver::writableDirectory(const std::string& envValue, bool envSet,
                                                          const std::filesystem::path& home,
                                                          const std::string& defaultSubdirectory) {
    if (!envSet || envValue.empty()) {
        return home / defaultSubdirectory;
    }

    const std::filesystem::path configured(envValue);

    // The XDG Base Directory specification: a relative path is ill-formed and must be ignored, falling back to
    // the default rather than resolving against the process's working directory.
    if (!configured.is_absolute()) {
        return home / defaultSubdirectory;
    }

    return configured;
}

ResourceNotFoundError::ResourceNotFoundError(const std::string& relativePath,
                                             const std::vector<std::filesystem::path>& searched)
    : std::runtime_error(formatMissingResourceMessage(relativePath, searched)) {}

} // namespace react_native_linux
