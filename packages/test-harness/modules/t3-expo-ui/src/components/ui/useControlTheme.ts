import { createContext, useContext } from "react";

const themes = {
  light: {
    "--color-border": "#e4e4e7",
    "--color-card": "#ffffff",
    "--color-foreground": "#27272a",
    "--color-foreground-muted": "#6f6f79",
    "--color-icon": "#27272a",
    "--color-icon-muted": "#71717b",
    "--color-primary": "#1b4ed8",
    "--color-subtle": "#fafafa",
  },
  dark: {
    "--color-border": "#191919",
    "--color-card": "#111111",
    "--color-foreground": "#f5f5f5",
    "--color-foreground-muted": "#838383",
    "--color-icon": "#f5f5f5",
    "--color-icon-muted": "#818181",
    "--color-primary": "#346bf1",
    "--color-subtle": "#111111",
  },
} as const;

export const ControlColorScheme = createContext<keyof typeof themes>("light");

export function useControlTheme() {
  const colorScheme = useContext(ControlColorScheme);
  return { colorScheme, colors: themes[colorScheme] };
}
