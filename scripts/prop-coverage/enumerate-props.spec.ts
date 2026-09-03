import { COMPONENT_SOURCES, enumerateComponentProps, parsePropsHeader } from "./enumerate-props.ts";
import { describe, expect, it } from "vitest";

const NAMED = 0;

const SAMPLE_SOURCE = { className: "SampleProps", path: "vendor/SampleProps.h" };

const SAMPLE_HEADER = [
  "#pragma once",
  "",
  "namespace facebook::react {",
  "",
  "class SampleProps : public Props {",
  " public:",
  "  SampleProps() = default;",
  "  SampleProps(const PropsParserContext &context, const SampleProps &sourceProps);",
  "",
  "  void",
  "  setProp(const PropsParserContext &context, const char *propName, const RawValue &value);",
  "",
  "#pragma mark - Props",
  "",
  "  // Colour",
  "  Float opacity{1.0};",
  "  SharedColor backgroundColor{};",
  "  CascadedBorderCurves borderCurves{}; // iOS only?",
  "  std::optional<std::vector<std::string>> acceptDragAndDropTypes{};",
  "  int maxLength = std::numeric_limits<int>::max();",
  "  Size shadowOffset{0, -3};",
  "",
  "  bool operator==(const SampleProps &rhs) const;",
  "  BorderMetrics resolveBorderMetrics(const LayoutMetrics &layoutMetrics) const;",
  "  static Transform defaultTransform();",
  "  using Base = Props;",
  "};",
  "",
  "} // namespace facebook::react",
].join("\n");

const FORWARD_DECLARED_HEADER = [
  "class Deferred;",
  "",
  "using SharedDeferred = std::shared_ptr<const Deferred>;",
  "",
  "class Deferred : public DebugStringConvertible {",
  " public:",
  "  bool ready{false};",
  "};",
].join("\n");

const ALIAS_HEADER = [
  "#include <react/renderer/components/view/BaseViewProps.h>",
  "",
  "namespace facebook::react {",
  "using HostPlatformViewProps = BaseViewProps;",
  "} // namespace facebook::react",
].join("\n");

describe("parsePropsHeader", () => {
  it("reads every member declaration with the line it is declared on", () => {
    expect(parsePropsHeader(SAMPLE_HEADER, SAMPLE_SOURCE)).toStrictEqual([
      { line: 16, name: "opacity", source: SAMPLE_SOURCE.path },
      { line: 17, name: "backgroundColor", source: SAMPLE_SOURCE.path },
      { line: 18, name: "borderCurves", source: SAMPLE_SOURCE.path },
      { line: 19, name: "acceptDragAndDropTypes", source: SAMPLE_SOURCE.path },
      { line: 20, name: "maxLength", source: SAMPLE_SOURCE.path },
      { line: 21, name: "shadowOffset", source: SAMPLE_SOURCE.path },
    ]);
  });

  it("skips a forward declaration and parses the definition that follows it", () => {
    expect(
      parsePropsHeader(FORWARD_DECLARED_HEADER, { className: "Deferred", path: "vendor/Deferred.h" }),
    ).toStrictEqual([{ line: 7, name: "ready", source: "vendor/Deferred.h" }]);
  });

  it("contributes no props when the name is a using alias for another class", () => {
    expect(
      parsePropsHeader(ALIAS_HEADER, { className: "HostPlatformViewProps", path: "vendor/HostPlatformViewProps.h" }),
    ).toStrictEqual([]);
  });

  it("reads an empty body out of a header that contains no semicolon at all", () => {
    expect(parsePropsHeader("class SampleProps {\n}", SAMPLE_SOURCE)).toStrictEqual([]);
  });

  it("refuses a header that declares neither the class nor an alias", () => {
    expect(() => parsePropsHeader("namespace facebook::react {}\n", SAMPLE_SOURCE)).toThrow(
      'vendor/SampleProps.h declares neither a class nor a using alias named "SampleProps"',
    );
  });

  it("refuses a class declaration with no body at all", () => {
    expect(() => parsePropsHeader("#pragma once\nclass SampleProps", SAMPLE_SOURCE)).toThrow(
      "declares neither a class nor a using alias",
    );
  });

  it("refuses a class body that is never closed", () => {
    expect(() => parsePropsHeader("class SampleProps {\n  bool ready{false};\n", SAMPLE_SOURCE)).toThrow(
      "declares neither a class nor a using alias",
    );
  });
});

describe("enumerateComponentProps", () => {
  it("concatenates the props of every source of every component", () => {
    const sources = [
      { component: "Sample", sources: [SAMPLE_SOURCE, { className: "Deferred", path: "vendor/Deferred.h" }] },
      { component: "Aliased", sources: [{ className: "HostPlatformViewProps", path: "vendor/Alias.h" }] },
    ];
    const headers: Record<string, string> = {
      "vendor/Alias.h": ALIAS_HEADER,
      "vendor/Deferred.h": FORWARD_DECLARED_HEADER,
      "vendor/SampleProps.h": SAMPLE_HEADER,
    };
    const enumerated = enumerateComponentProps(sources, (relativePath) => headers[relativePath] ?? "");

    expect(enumerated.map((entry) => entry.props.map((prop) => prop.name))).toStrictEqual([
      ["opacity", "backgroundColor", "borderCurves", "acceptDragAndDropTypes", "maxLength", "shadowOffset", "ready"],
      [],
    ]);
  });
});

describe("COMPONENT_SOURCES", () => {
  it("names the five shipped components and a class for every source", () => {
    expect(COMPONENT_SOURCES.map((entry) => entry.component)).toStrictEqual([
      "View",
      "Text",
      "Image",
      "ScrollView",
      "TextInput",
    ]);
    expect(COMPONENT_SOURCES.flatMap((entry) => entry.sources).every((source) => source.className.length > NAMED)).toBe(
      true,
    );
  });
});
