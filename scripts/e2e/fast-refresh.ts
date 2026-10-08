import { readObject, readPositiveInteger, readString } from "./fields.ts";
import path from "node:path";

const PARENT_DIRECTORY = "..";

/**
 * #81: a scenario that runs its bundle from a watching Metro and edits one file once the window is ready. `file` is
 * a path from the repository root, restored after the run; `expect` is the trace line the edited module prints when
 * Fast Refresh runs it again, and `maxEditToVisibleMs` is the budget from the write to that line.
 */
interface FastRefreshEdit {
  readonly expect: string;
  readonly file: string;
  readonly find: string;
  readonly maxEditToVisibleMs: number;
  readonly replace: string;
}

const readEditedFile = (block: Record<string, unknown>, sourceName: string): string => {
  const file = readString(block["file"], "fastRefresh.file", sourceName);

  if (path.isAbsolute(file) || file.split(path.sep).includes(PARENT_DIRECTORY)) {
    throw new Error(`${sourceName}: "fastRefresh.file" must be a relative path inside the repository`);
  }

  return file;
};

const readFastRefresh = (record: Record<string, unknown>, sourceName: string): FastRefreshEdit | null => {
  if (!("fastRefresh" in record)) {
    return null;
  }

  const block = readObject(record["fastRefresh"], "fastRefresh", sourceName);

  return {
    expect: readString(block["expect"], "fastRefresh.expect", sourceName),
    file: readEditedFile(block, sourceName),
    find: readString(block["find"], "fastRefresh.find", sourceName),
    maxEditToVisibleMs: readPositiveInteger(block["maxEditToVisibleMs"], "fastRefresh.maxEditToVisibleMs", sourceName),
    replace: readString(block["replace"], "fastRefresh.replace", sourceName),
  };
};

/**
 * The edited text. Text that does not contain `find` is an error naming the file rather than an unchanged file,
 * because an edit that changes nothing would read as a refresh that never arrived.
 */
const applyFastRefreshEdit = (text: string, edit: FastRefreshEdit): string => {
  if (!text.includes(edit.find)) {
    throw new Error(`${edit.file} does not contain ${JSON.stringify(edit.find)}`);
  }

  return text.replace(edit.find, edit.replace);
};

interface EditToVisibleGrade {
  readonly failures: readonly string[];
  readonly note: string;
}

/** `elapsedMs` is null when the expected line never appeared. */
const gradeEditToVisible = (elapsedMs: number | null, edit: FastRefreshEdit): EditToVisibleGrade => {
  if (elapsedMs === null) {
    return {
      failures: [`"${edit.expect}" never appeared after editing ${edit.file}`],
      note: "fast refresh: the edit never became visible",
    };
  }

  return {
    failures:
      elapsedMs > edit.maxEditToVisibleMs
        ? [
            `fast refresh took ${String(elapsedMs)} ms from edit to visible, the budget is ${String(edit.maxEditToVisibleMs)} ms`,
          ]
        : [],
    note: `fast refresh: edit-to-visible ${String(elapsedMs)} ms`,
  };
};

export { applyFastRefreshEdit, gradeEditToVisible, readFastRefresh };
export type { FastRefreshEdit };
