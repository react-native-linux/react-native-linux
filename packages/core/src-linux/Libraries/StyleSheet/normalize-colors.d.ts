declare module "@react-native/normalize-colors" {
  /** A CSS colour string or a number as `0xrrggbbaa`, or `null` for anything it cannot read. */
  export default function normalizeColor(color: string | number): number | null;
}
