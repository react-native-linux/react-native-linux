const harnessAutolinkingEntryPattern =
  /^RNL_AUTOLINKING_CMAKE:FILEPATH=.*\/autolinking-harness\/rnl_autolinking\.cmake$/mu;

/**
 * Why a `harnessAutolinking` scenario cannot run on the window `cmakeCache` (a `CMakeCache.txt`, or null when the
 * build directory has none) was configured from, or null when it can: the window must have been configured with the
 * test harness's autolinking, which only `scripts/autolink-harness.ts` writes (#806).
 */
const describeMissingHarnessAutolinking = (cmakeCache: string | null): string | null =>
  harnessAutolinkingEntryPattern.test(cmakeCache ?? "")
    ? null
    : 'a harnessAutolinking scenario needs rnl_window configured with the test harness\'s libraries: run "node scripts/autolink-harness.ts build/autolinking-harness", then "cmake --preset dev -DRNL_AUTOLINKING_CMAKE=$PWD/build/autolinking-harness/rnl_autolinking.cmake" and rebuild rnl_window';

export { describeMissingHarnessAutolinking };
