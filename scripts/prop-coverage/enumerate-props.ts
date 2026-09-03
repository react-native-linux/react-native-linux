import type { ComponentProps, ComponentSources, DeclaredProp, PropSource } from "./prop-coverage-types.ts";

const RENDERER_ROOT = "third_party/react-native/packages/react-native/ReactCommon/react/renderer";
const COMPONENTS_ROOT = `${RENDERER_ROOT}/components`;

/**
 * The name of one member declaration of a props class: an indented type, the name, an optional brace or `=`
 * initialiser, a semicolon and an optional trailing line comment. A method declaration cannot match because
 * neither the type nor the name may contain a parenthesis, and `operator==` cannot match because the `=`
 * initialiser is `=(?!=)`.
 */
const MEMBER_PATTERN =
  /(?<=^[ \t]+(?!(?:constexpr|explicit|friend|inline|return|static|template|typedef|using|virtual)\b)[A-Za-z_][\w:<>,&* \t]*?[ \t]+)[A-Za-z_]\w*(?=[ \t]*(?:\{[^;]*\}|=(?!=)[^;]*)?;[ \t]*(?:\/\/.*)?$)/u;

const BRACE_PATTERN = /[{}]/gu;
const NOT_FOUND = -1;
const BALANCED = 0;
const ONE_LEVEL = 1;
const FILE_START = 0;
const AFTER_BRACE = 1;
const WHOLE_MATCH = 0;

const COMPONENT_SOURCES: readonly ComponentSources[] = [
  {
    component: "View",
    sources: [
      { className: "BaseViewProps", path: `${COMPONENTS_ROOT}/view/BaseViewProps.h` },
      {
        className: "HostPlatformViewProps",
        path: `${COMPONENTS_ROOT}/view/platform/cxx/react/renderer/components/view/HostPlatformViewProps.h`,
      },
      { className: "ViewProps", path: `${COMPONENTS_ROOT}/view/ViewProps.h` },
    ],
  },
  {
    component: "Text",
    sources: [
      { className: "BaseParagraphProps", path: `${COMPONENTS_ROOT}/text/BaseParagraphProps.h` },
      {
        className: "HostPlatformParagraphProps",
        path: `${COMPONENTS_ROOT}/text/platform/cxx/react/renderer/components/text/HostPlatformParagraphProps.h`,
      },
      { className: "ParagraphProps", path: `${COMPONENTS_ROOT}/text/ParagraphProps.h` },
      { className: "BaseTextProps", path: `${COMPONENTS_ROOT}/text/BaseTextProps.h` },
      { className: "TextAttributes", path: `${RENDERER_ROOT}/attributedstring/TextAttributes.h` },
      { className: "ParagraphAttributes", path: `${RENDERER_ROOT}/attributedstring/ParagraphAttributes.h` },
    ],
  },
  {
    component: "Image",
    sources: [{ className: "ImageProps", path: `${COMPONENTS_ROOT}/image/ImageProps.h` }],
  },
  {
    component: "ScrollView",
    sources: [
      { className: "BaseScrollViewProps", path: `${COMPONENTS_ROOT}/scrollview/BaseScrollViewProps.h` },
      {
        className: "HostPlatformScrollViewProps",
        path: `${COMPONENTS_ROOT}/scrollview/platform/cxx/react/renderer/components/scrollview/HostPlatformScrollViewProps.h`,
      },
      { className: "ScrollViewProps", path: `${COMPONENTS_ROOT}/scrollview/ScrollViewProps.h` },
    ],
  },
  {
    component: "TextInput",
    sources: [
      { className: "BaseTextInputProps", path: `${COMPONENTS_ROOT}/textinput/BaseTextInputProps.h` },
      { className: "TextInputProps", path: "packages/core/src/TextInputComponent.h" },
    ],
  },
];

interface ClassBody {
  readonly firstLine: number;
  readonly source: string;
}

const buildClassPattern = (className: string): RegExp =>
  new RegExp(String.raw`(?:^|\n)[ \t]*(?:class|struct)[ \t]+${className}\b`, "gu");

const buildAliasPattern = (className: string): RegExp =>
  new RegExp(String.raw`(?:^|\n)[ \t]*using[ \t]+${className}[ \t]*=`, "u");

const findMatchingBrace = (contents: string, openIndex: number): number => {
  let depth = BALANCED;

  for (const brace of contents.slice(openIndex).matchAll(BRACE_PATTERN)) {
    depth += brace[WHOLE_MATCH] === "{" ? ONE_LEVEL : -ONE_LEVEL;

    if (depth === BALANCED) {
      return openIndex + brace.index;
    }
  }

  return NOT_FOUND;
};

const countLines = (contents: string): number => contents.split("\n").length;

/**
 * The body of `class`/`struct <className>`, skipping forward declarations — `class TextAttributes;` precedes the
 * definition in the header that declares it.
 */
const findClassBody = (contents: string, className: string): ClassBody | null => {
  for (const declaration of contents.matchAll(buildClassPattern(className))) {
    const openIndex = contents.indexOf("{", declaration.index);
    const declarationEndIndex = contents.indexOf(";", declaration.index);
    const isForwardDeclaration =
      openIndex === NOT_FOUND || (declarationEndIndex !== NOT_FOUND && declarationEndIndex < openIndex);
    const closeIndex = isForwardDeclaration ? NOT_FOUND : findMatchingBrace(contents, openIndex);

    if (closeIndex !== NOT_FOUND) {
      return {
        firstLine: countLines(contents.slice(FILE_START, openIndex)),
        source: contents.slice(openIndex + AFTER_BRACE, closeIndex),
      };
    }
  }

  return null;
};

const parsePropsHeader = (contents: string, source: PropSource): readonly DeclaredProp[] => {
  const body = findClassBody(contents, source.className);

  if (body === null) {
    if (buildAliasPattern(source.className).test(contents)) {
      return [];
    }

    throw new Error(`${source.path} declares neither a class nor a using alias named "${source.className}"`);
  }

  return body.source.split("\n").flatMap((line, offset) => {
    const name = MEMBER_PATTERN.exec(line)?.[WHOLE_MATCH] ?? null;

    return name === null ? [] : [{ line: body.firstLine + offset, name, source: source.path }];
  });
};

const enumerateComponentProps = (
  componentSources: readonly ComponentSources[],
  readFile: (relativePath: string) => string,
): readonly ComponentProps[] =>
  componentSources.map((entry) => ({
    component: entry.component,
    props: entry.sources.flatMap((source) => parsePropsHeader(readFile(source.path), source)),
  }));

export { COMPONENT_SOURCES, enumerateComponentProps, parsePropsHeader };
