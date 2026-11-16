import type { ReactNode } from "react";

export function OverlayPortal(props: { readonly children: ReactNode }) {
  return props.children;
}
