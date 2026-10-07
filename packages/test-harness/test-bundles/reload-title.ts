// #81: A module that exports only a value is not a Fast Refresh boundary, and neither is reload.tsx, which exports
// nothing, so an edit here makes HMRClient ask DevSettings for a full reload. The e2e scenario edits this string.
export const reloadTitle = "Before Reload";
