import { useImperativeHandle, useRef } from "react";
import { TextInput, View, type KeyDownEvent, type TextInputInstance } from "react-native";

import { FONT_FAMILIES } from "../lib/fontFamilies";
import { useScaledTextRole } from "../features/settings/appearance/useScaledTextRole";
import type { ComposerEditorProps, ComposerEditorSelection } from "./T3ComposerEditor.types";

// react-native-linux: the platform's TextInput, so chips stay source text (react-native-linux#694)
// and expo-paste-input, which has no Linux view, is left out. keyDown reaches JS with modifiers
// before the field edits, so Return follows the Android view's returnSubmission rules.
export function ComposerEditor({
  ref,
  skills: _skills,
  selection,
  onPasteImages: _onPasteImages,
  onPasteText: _onPasteText,
  style,
  textStyle,
  contentInsetVertical = 0,
  singleLineCentered: _singleLineCentered,
  enterBehavior = "send",
  readResetGeneration: _readResetGeneration,
  readOnly = false,
  onContentSizeChange,
  onSelectionChange,
  onSubmit,
  ...props
}: ComposerEditorProps) {
  const inputRef = useRef<TextInputInstance>(null);
  const selectionRef = useRef<ComposerEditorSelection>({ start: 0, end: 0 });
  const bodyText = useScaledTextRole("body");
  const fontFamily = FONT_FAMILIES.regular;
  const editable = (props.editable ?? true) && !readOnly;
  const returnSends = enterBehavior === "send";

  useImperativeHandle(
    ref,
    () => ({
      focus: () => inputRef.current?.focus(),
      blur: () => inputRef.current?.blur(),
      setSelection: (nextSelection) =>
        inputRef.current?.setSelection(nextSelection.start, nextSelection.end),
      flushPendingInput: async () => {},
    }),
    [],
  );

  const handleKeyDown = ({ nativeEvent: key }: KeyDownEvent) => {
    if (onSubmit === undefined || !editable || key.key !== "Enter") return;
    if (key.isComposing === true || key.altKey || key.metaKey) return;
    if (!returnSends) {
      if (key.ctrlKey) onSubmit(key.shiftKey);
      return;
    }
    if (!key.shiftKey) {
      onSubmit(key.ctrlKey);
      return;
    }
    if (key.ctrlKey) return;
    const { start, end } = selectionRef.current;
    props.onChangeText(`${props.value.slice(0, start)}\n${props.value.slice(end)}`);
    onSelectionChange?.({ start: start + 1, end: start + 1 });
  };

  return (
    <View style={[{ minHeight: 0 }, style]}>
      <TextInput
        ref={inputRef}
        {...props}
        readOnly={readOnly}
        selection={selection}
        submitBehavior={returnSends && onSubmit !== undefined ? "submit" : "newline"}
        onKeyDown={handleKeyDown}
        onSelectionChange={(event) => {
          selectionRef.current = event.nativeEvent.selection;
          onSelectionChange?.(event.nativeEvent.selection);
        }}
        onContentSizeChange={(event) => onContentSizeChange?.(event.nativeEvent.contentSize)}
        multiline={props.multiline ?? true}
        placeholderTextColorClassName={"accent-placeholder"}
        className="text-foreground"
        style={[
          {
            flex: 1,
            minHeight: 0,
            fontFamily,
            ...bodyText,
            paddingVertical: contentInsetVertical,
          },
          textStyle,
        ]}
      />
    </View>
  );
}

export type {
  ComposerEditorHandle,
  ComposerEditorProps,
  ComposerEditorSelection,
  ComposerTextPaste,
} from "./T3ComposerEditor.types";
