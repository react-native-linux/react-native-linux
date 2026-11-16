import { Text, type TextProps } from "react-native";

const textRoles = {
  footnote: { fontSize: 14, lineHeight: 20 },
  body: { fontSize: 16, lineHeight: 22 },
  headline: { fontSize: 18, lineHeight: 24 },
  title: { fontSize: 20, lineHeight: 26 },
} as const;

export function AppText({
  textRole = "body",
  style,
  ...props
}: TextProps & { readonly textRole?: keyof typeof textRoles }) {
  return <Text style={[{ fontFamily: "Noto Sans" }, textRoles[textRole], style]} {...props} />;
}
