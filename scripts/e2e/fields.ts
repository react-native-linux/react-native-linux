const EMPTY_LENGTH = 0;
const MINIMUM_COORDINATE = 0;
const MINIMUM_INTEGER = 1;
const MINIMUM_POSITIVE_NUMBER = 0;

/**
 * The typed readers every `packages/*\/e2e/*.json` field is validated through. They live apart from the schema
 * that uses them so a schema file stays a list of fields rather than a list of fields and the arithmetic of
 * checking them; each one either returns the narrowed value or throws naming the scenario file and the field.
 */
const isRecord = (value: unknown): value is Record<string, unknown> =>
  typeof value === "object" && value !== null && !Array.isArray(value);

const isStringArray = (value: unknown): value is readonly string[] =>
  Array.isArray(value) && value.every((entry) => typeof entry === "string");

const readString = (value: unknown, label: string, sourceName: string): string => {
  if (typeof value !== "string" || value === "") {
    throw new Error(`${sourceName}: "${label}" must be a non-empty string`);
  }

  return value;
};

const readStringArray = (value: unknown, label: string, sourceName: string): readonly string[] => {
  if (!isStringArray(value) || value.length === EMPTY_LENGTH) {
    throw new Error(`${sourceName}: "${label}" must be a non-empty array of strings`);
  }

  return value;
};

const readPositiveInteger = (value: unknown, label: string, sourceName: string): number => {
  if (typeof value !== "number" || !Number.isInteger(value) || value < MINIMUM_INTEGER) {
    throw new Error(`${sourceName}: "${label}" must be a positive integer`);
  }

  return value;
};

const readPositiveNumber = (value: unknown, label: string, sourceName: string): number => {
  if (typeof value !== "number" || !Number.isFinite(value) || value <= MINIMUM_POSITIVE_NUMBER) {
    throw new Error(`${sourceName}: "${label}" must be a positive number`);
  }

  return value;
};

const readCoordinate = (value: unknown, label: string, sourceName: string): number => {
  if (typeof value !== "number" || !Number.isInteger(value) || value < MINIMUM_COORDINATE) {
    throw new Error(`${sourceName}: "${label}" must be a non-negative integer`);
  }

  return value;
};

const readObject = (value: unknown, label: string, sourceName: string): Record<string, unknown> => {
  if (!isRecord(value)) {
    throw new Error(`${sourceName}: "${label}" must be a JSON object`);
  }

  return value;
};

/** Every optional boolean field this schema has defaults to `false` when the scenario omits it. */
const readOptionalBoolean = (record: Record<string, unknown>, label: string, sourceName: string): boolean => {
  if (!(label in record)) {
    return false;
  }

  const value = record[label];

  if (typeof value !== "boolean") {
    throw new TypeError(`${sourceName}: "${label}" must be a boolean`);
  }

  return value;
};

export {
  isRecord,
  readCoordinate,
  readObject,
  readOptionalBoolean,
  readPositiveInteger,
  readPositiveNumber,
  readString,
  readStringArray,
};
