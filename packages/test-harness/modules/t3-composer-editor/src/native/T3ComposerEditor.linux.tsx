import { useImperativeHandle, useLayoutEffect, useRef } from "react";
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
  const insertedSelectionRef = useRef<ComposerEditorSelection | null>(null);
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

  // Runs after TextInput's own layout effect has sent the new value, so the caret lands in it.
  useLayoutEffect(() => {
    const inserted = insertedSelectionRef.current;
    insertedSelectionRef.current = null;
    if (inserted !== null) inputRef.current?.setSelection(inserted.start, inserted.end);
  }, [props.value]);

  const insertNewline = () => {
    const { start, end } = selectionRef.current;
    const caret = { start: start + 1, end: start + 1 };
    selectionRef.current = caret;
    insertedSelectionRef.current = caret;
    props.onChangeText(`${props.value.slice(0, start)}\n${props.value.slice(end)}`);
    onSelectionChange?.(caret);
  };

  const handleKeyDown = ({ nativeEvent: key }: KeyDownEvent) => {
    if (onSubmit === undefined || !editable || key.key !== "Enter" || key.isComposing === true) {
      return;
    }
    if (!returnSends) {
      if (key.ctrlKey && !key.altKey && !key.metaKey && !key.repeat) onSubmit(key.shiftKey);
      return;
    }
    if (key.shiftKey || key.altKey || key.metaKey) {
      insertNewline();
      return;
    }
    if (!key.repeat) onSubmit(key.ctrlKey);
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
