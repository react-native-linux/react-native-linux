import { useControlTheme } from "../../../components/ui/useControlTheme";

export function useAppearancePreferences() {
  return { themeVariables: useControlTheme().colors };
}
