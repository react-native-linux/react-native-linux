# Prop coverage

Every prop the five shipped components declare is in exactly one of three states — `implemented`, `deviating`
or `not-implemented` — and a prop in none of them fails `pnpm prop-coverage:check`, which the `validate` job
runs. This file is generated: edit `docs/prop-coverage.json` and run `pnpm prop-coverage:report`.

## How the list is derived

`scripts/prop-coverage.ts` parses the props classes React Native declares in the vendored headers under
`third_party/react-native`, plus the one props class this platform declares itself
(`packages/core/src/TextInputComponent.h`). Nothing below is hand-maintained: a prop is listed because a header
declares it, so an upstream bump that adds a prop fails the check rather than reaching a user.

A prop is the name matched by

```regexp
(?<=^[ \t]+(?!(?:constexpr|explicit|friend|inline|return|static|template|typedef|using|virtual)\b)[A-Za-z_][\w:<>,&* \t]*?[ \t]+)[A-Za-z_]\w*(?=[ \t]*(?:\{[^;]*\}|=(?!=)[^;]*)?;[ \t]*(?://.*)?$)
```

on one line inside the brace-matched body of the named `class` or `struct`: a lookbehind for an indented type,
the name, and a lookahead for an optional brace or `=` initialiser, a semicolon and an optional trailing
comment. A method declaration cannot match, because neither the type nor the name may contain a parenthesis;
`operator==` cannot match, because the initialiser is `=(?!=)`; and a forward declaration is skipped, because a
`;` before the `{` is not a body. A source that declares its name as a `using` alias contributes no props and is
listed as a source anyway, so a platform header that grows real members on an upstream bump is enumerated the
day it does.

Each component's props are its **own** declarations. Everything `<Paragraph>`, `<Image>`, `<ScrollView>` and
`<TextInput>` inherit from `ViewProps` is enumerated once, under **View**. Layout (`YogaStylableProps`) and
accessibility (`AccessibilityProps`) are not paint props and are out of scope here; they belong to the layout
and accessibility issues. The generated tree under `packages/core/generated` declares no props class for any of
the five — upstream hand-writes all five rather than generating them — so the vendored headers are the whole
source of truth.

## The three states

| State | What it means | What it carries |
| --- | --- | --- |
| `implemented` | the platform reads the prop and an assertion proves it | `test`: a GoogleTest name from `packages/core/tests/**/*.cpp`, or a golden file name from `packages/core/goldens/*.ts` |
| `deviating` | the platform deliberately does something else, as the *Fidelity limits* prose of `docs/cpp-toolchain.md` records | `reason` |
| `not-implemented` | nothing reads the prop, or nothing asserts it | `issue`: the open issue that owns it, `#69` when no narrower one does |

`implemented` is the strong claim and it is checked: the `test` string has to appear verbatim in a test source
or a golden registration, so deleting or renaming a test turns its props back into a build failure. A prop the
platform reads but nothing asserts is `not-implemented`, because issue #69's acceptance criterion is an
assertion and not an implementation.

## What the check enforces

- a declared prop with no ledger entry fails, which is what an upstream bump or a new component looks like;
- a ledger entry for a prop no longer declared fails, which is what an upstream removal looks like;
- an `implemented` entry whose `test` no longer exists fails, which is what a deleted test looks like;
- a rendered report that differs from this file fails, so the tables below are always the ledger.

## Summary

| Component | Props | Implemented | Deviating | Not implemented |
| --- | --- | --- | --- | --- |
| View | 35 | 8 | 6 | 21 |
| Text | 45 | 12 | 12 | 21 |
| Image | 14 | 3 | 7 | 4 |
| ScrollView | 39 | 2 | 2 | 35 |
| TextInput | 22 | 11 | 0 | 11 |
| **Total** | 155 | 36 | 27 | 92 |

## View

| Prop | Declared at | State | Proof, reason or owner |
| --- | --- | --- | --- |
| `opacity` | `third_party/react-native/packages/react-native/ReactCommon/react/renderer/components/view/BaseViewProps.h:46` | implemented | `RetainedSceneTest, OpacityMultipliesDownTheTree` |
| `backgroundColor` | `third_party/react-native/packages/react-native/ReactCommon/react/renderer/components/view/BaseViewProps.h:47` | implemented | `RetainedSceneTest, UpdateReplacesFrameAndBackgroundColorInPlace` |
| `borderRadii` | `third_party/react-native/packages/react-native/ReactCommon/react/renderer/components/view/BaseViewProps.h:50` | implemented | `RetainedSceneTest, BorderRadiiAreClampedSoAdjacentCornersDoNotOverlap` |
| `borderColors` | `third_party/react-native/packages/react-native/ReactCommon/react/renderer/components/view/BaseViewProps.h:51` | implemented | `RetainedSceneTest, BorderWidthsAndColorsAreReadPerSide` |
| `borderCurves` | `third_party/react-native/packages/react-native/ReactCommon/react/renderer/components/view/BaseViewProps.h:52` | deviating | Corners are always circular; iOS' `continuous` squircle is never drawn. See *View props fidelity*. |
| `borderStyles` | `third_party/react-native/packages/react-native/ReactCommon/react/renderer/components/view/BaseViewProps.h:53` | deviating | Every border is solid; `dotted` and `dashed` draw the same ring as `solid`. Issue #101 owns implementing them or refusing them in writing. |
| `outlineColor` | `third_party/react-native/packages/react-native/ReactCommon/react/renderer/components/view/BaseViewProps.h:56` | not-implemented | #68 |
| `outlineOffset` | `third_party/react-native/packages/react-native/ReactCommon/react/renderer/components/view/BaseViewProps.h:57` | not-implemented | #68 |
| `outlineStyle` | `third_party/react-native/packages/react-native/ReactCommon/react/renderer/components/view/BaseViewProps.h:58` | not-implemented | #68 |
| `outlineWidth` | `third_party/react-native/packages/react-native/ReactCommon/react/renderer/components/view/BaseViewProps.h:59` | not-implemented | #68 |
| `shadowColor` | `third_party/react-native/packages/react-native/ReactCommon/react/renderer/components/view/BaseViewProps.h:62` | not-implemented | #67 |
| `shadowOffset` | `third_party/react-native/packages/react-native/ReactCommon/react/renderer/components/view/BaseViewProps.h:63` | not-implemented | #67 |
| `shadowOpacity` | `third_party/react-native/packages/react-native/ReactCommon/react/renderer/components/view/BaseViewProps.h:64` | not-implemented | #67 |
| `shadowRadius` | `third_party/react-native/packages/react-native/ReactCommon/react/renderer/components/view/BaseViewProps.h:65` | not-implemented | #67 |
| `cursor` | `third_party/react-native/packages/react-native/ReactCommon/react/renderer/components/view/BaseViewProps.h:67` | not-implemented | #40 |
| `boxShadow` | `third_party/react-native/packages/react-native/ReactCommon/react/renderer/components/view/BaseViewProps.h:70` | not-implemented | #67 |
| `filter` | `third_party/react-native/packages/react-native/ReactCommon/react/renderer/components/view/BaseViewProps.h:73` | not-implemented | #68 |
| `backgroundImage` | `third_party/react-native/packages/react-native/ReactCommon/react/renderer/components/view/BaseViewProps.h:76` | implemented | `RetainedSceneTest, ABackgroundImageGradientTravelsToThePrimitiveWithTheInheritedOpacity` |
| `backgroundSize` | `third_party/react-native/packages/react-native/ReactCommon/react/renderer/components/view/BaseViewProps.h:79` | deviating | Every gradient layer fills the whole border box; CSS `background-size` is parsed and ignored. See *Gradients*, Fidelity limits. |
| `backgroundPosition` | `third_party/react-native/packages/react-native/ReactCommon/react/renderer/components/view/BaseViewProps.h:82` | deviating | Every gradient layer fills the whole border box; CSS `background-position` is parsed and ignored. See *Gradients*, Fidelity limits. |
| `backgroundRepeat` | `third_party/react-native/packages/react-native/ReactCommon/react/renderer/components/view/BaseViewProps.h:85` | deviating | Every gradient layer fills the whole border box; CSS `background-repeat` is parsed and ignored, and every gradient is `SkTileMode::kClamp`. |
| `mixBlendMode` | `third_party/react-native/packages/react-native/ReactCommon/react/renderer/components/view/BaseViewProps.h:88` | not-implemented | #68 |
| `isolation` | `third_party/react-native/packages/react-native/ReactCommon/react/renderer/components/view/BaseViewProps.h:91` | not-implemented | #68 |
| `transform` | `third_party/react-native/packages/react-native/ReactCommon/react/renderer/components/view/BaseViewProps.h:94` | implemented | `RetainedSceneTest, ATransformIsAppliedAboutTheCenterOfTheAbsoluteFrame` |
| `transformOrigin` | `third_party/react-native/packages/react-native/ReactCommon/react/renderer/components/view/BaseViewProps.h:95` | implemented | `AnimatedPropsTest, ATransformResolvesAboutTheOriginTheNodeMountedWith` |
| `backfaceVisibility` | `third_party/react-native/packages/react-native/ReactCommon/react/renderer/components/view/BaseViewProps.h:96` | deviating | The 4x4 transform is reduced to its 2D affine part, so depth is lost and a back face is never culled. See *View props fidelity*. |
| `shouldRasterize` | `third_party/react-native/packages/react-native/ReactCommon/react/renderer/components/view/BaseViewProps.h:97` | not-implemented | #69 |
| `zIndex` | `third_party/react-native/packages/react-native/ReactCommon/react/renderer/components/view/BaseViewProps.h:98` | not-implemented | #35 |
| `pointerEvents` | `third_party/react-native/packages/react-native/ReactCommon/react/renderer/components/view/BaseViewProps.h:101` | implemented | `AnimatedHitTestTest, PointerEventsNoneRemovesTheNodeAndItsChildren` |
| `hitSlop` | `third_party/react-native/packages/react-native/ReactCommon/react/renderer/components/view/BaseViewProps.h:102` | not-implemented | #64 |
| `onLayout` | `third_party/react-native/packages/react-native/ReactCommon/react/renderer/components/view/BaseViewProps.h:103` | not-implemented | #115 |
| `events` | `third_party/react-native/packages/react-native/ReactCommon/react/renderer/components/view/BaseViewProps.h:105` | not-implemented | #36 |
| `collapsable` | `third_party/react-native/packages/react-native/ReactCommon/react/renderer/components/view/BaseViewProps.h:107` | not-implemented | #69 |
| `collapsableChildren` | `third_party/react-native/packages/react-native/ReactCommon/react/renderer/components/view/BaseViewProps.h:108` | not-implemented | #69 |
| `removeClippedSubviews` | `third_party/react-native/packages/react-native/ReactCommon/react/renderer/components/view/BaseViewProps.h:110` | not-implemented | #69 |

## Text

| Prop | Declared at | State | Proof, reason or owner |
| --- | --- | --- | --- |
| `paragraphAttributes` | `third_party/react-native/packages/react-native/ReactCommon/react/renderer/components/text/BaseParagraphProps.h:43` | implemented | `RetainedSceneTextTest, ParagraphStateBecomesTheTextOnTheNode` |
| `isSelectable` | `third_party/react-native/packages/react-native/ReactCommon/react/renderer/components/text/BaseParagraphProps.h:48` | not-implemented | #43 |
| `onTextLayout` | `third_party/react-native/packages/react-native/ReactCommon/react/renderer/components/text/BaseParagraphProps.h:50` | not-implemented | #111 |
| `textAttributes` | `third_party/react-native/packages/react-native/ReactCommon/react/renderer/components/text/BaseTextProps.h:31` | implemented | `RetainedSceneTextTest, OpacityMultipliesIntoTheFragmentColors` |
| `foregroundColor` | `third_party/react-native/packages/react-native/ReactCommon/react/renderer/attributedstring/TextAttributes.h:50` | implemented | `RetainedSceneTextTest, OpacityMultipliesIntoTheFragmentColors` |
| `backgroundColor` | `third_party/react-native/packages/react-native/ReactCommon/react/renderer/attributedstring/TextAttributes.h:51` | implemented | `text-metrics.png` |
| `opacity` | `third_party/react-native/packages/react-native/ReactCommon/react/renderer/attributedstring/TextAttributes.h:52` | not-implemented | #69 |
| `fontFamily` | `third_party/react-native/packages/react-native/ReactCommon/react/renderer/attributedstring/TextAttributes.h:55` | not-implemented | #70 |
| `fontSize` | `third_party/react-native/packages/react-native/ReactCommon/react/renderer/attributedstring/TextAttributes.h:56` | implemented | `text.png` |
| `fontSizeMultiplier` | `third_party/react-native/packages/react-native/ReactCommon/react/renderer/attributedstring/TextAttributes.h:57` | not-implemented | #113 |
| `fontWeight` | `third_party/react-native/packages/react-native/ReactCommon/react/renderer/attributedstring/TextAttributes.h:58` | implemented | `text.png` |
| `fontStyle` | `third_party/react-native/packages/react-native/ReactCommon/react/renderer/attributedstring/TextAttributes.h:59` | implemented | `text.png` |
| `fontVariant` | `third_party/react-native/packages/react-native/ReactCommon/react/renderer/attributedstring/TextAttributes.h:60` | deviating | Ignored; SkParagraph is given no OpenType feature list. See *Text*, Fidelity limits. |
| `allowFontScaling` | `third_party/react-native/packages/react-native/ReactCommon/react/renderer/attributedstring/TextAttributes.h:61` | not-implemented | #113 |
| `maxFontSizeMultiplier` | `third_party/react-native/packages/react-native/ReactCommon/react/renderer/attributedstring/TextAttributes.h:62` | not-implemented | #113 |
| `dynamicTypeRamp` | `third_party/react-native/packages/react-native/ReactCommon/react/renderer/attributedstring/TextAttributes.h:63` | not-implemented | #113 |
| `letterSpacing` | `third_party/react-native/packages/react-native/ReactCommon/react/renderer/attributedstring/TextAttributes.h:64` | implemented | `text.png` |
| `textTransform` | `third_party/react-native/packages/react-native/ReactCommon/react/renderer/attributedstring/TextAttributes.h:65` | deviating | Ignored; the string reaches SkParagraph exactly as React sent it. See *Text*, Fidelity limits. |
| `lineHeight` | `third_party/react-native/packages/react-native/ReactCommon/react/renderer/attributedstring/TextAttributes.h:68` | implemented | `LineBoxMetricsTest, ConvertsLineHeightPointsToAMultipleOfTheFontSize` |
| `alignment` | `third_party/react-native/packages/react-native/ReactCommon/react/renderer/attributedstring/TextAttributes.h:69` | implemented | `text.png` |
| `baseWritingDirection` | `third_party/react-native/packages/react-native/ReactCommon/react/renderer/attributedstring/TextAttributes.h:70` | not-implemented | #72 |
| `lineBreakStrategy` | `third_party/react-native/packages/react-native/ReactCommon/react/renderer/attributedstring/TextAttributes.h:71` | not-implemented | #69 |
| `lineBreakMode` | `third_party/react-native/packages/react-native/ReactCommon/react/renderer/attributedstring/TextAttributes.h:72` | not-implemented | #69 |
| `textDecorationColor` | `third_party/react-native/packages/react-native/ReactCommon/react/renderer/attributedstring/TextAttributes.h:75` | not-implemented | #69 |
| `textDecorationLineType` | `third_party/react-native/packages/react-native/ReactCommon/react/renderer/attributedstring/TextAttributes.h:76` | implemented | `text.png` |
| `textDecorationStyle` | `third_party/react-native/packages/react-native/ReactCommon/react/renderer/attributedstring/TextAttributes.h:77` | not-implemented | #69 |
| `textShadowOffset` | `third_party/react-native/packages/react-native/ReactCommon/react/renderer/attributedstring/TextAttributes.h:81` | deviating | Text shadows are not drawn at all. See *Text*, Fidelity limits. |
| `textShadowRadius` | `third_party/react-native/packages/react-native/ReactCommon/react/renderer/attributedstring/TextAttributes.h:82` | deviating | Text shadows are not drawn at all. See *Text*, Fidelity limits. |
| `textShadowColor` | `third_party/react-native/packages/react-native/ReactCommon/react/renderer/attributedstring/TextAttributes.h:83` | deviating | Text shadows are not drawn at all. See *Text*, Fidelity limits. |
| `isHighlighted` | `third_party/react-native/packages/react-native/ReactCommon/react/renderer/attributedstring/TextAttributes.h:86` | not-implemented | #43 |
| `isPressable` | `third_party/react-native/packages/react-native/ReactCommon/react/renderer/attributedstring/TextAttributes.h:87` | not-implemented | #43 |
| `layoutDirection` | `third_party/react-native/packages/react-native/ReactCommon/react/renderer/attributedstring/TextAttributes.h:93` | not-implemented | #119 |
| `accessibilityRole` | `third_party/react-native/packages/react-native/ReactCommon/react/renderer/attributedstring/TextAttributes.h:94` | not-implemented | #61 |
| `role` | `third_party/react-native/packages/react-native/ReactCommon/react/renderer/attributedstring/TextAttributes.h:95` | not-implemented | #61 |
| `textEffects` | `third_party/react-native/packages/react-native/ReactCommon/react/renderer/attributedstring/TextAttributes.h:98` | not-implemented | #69 |
| `maximumNumberOfLines` | `third_party/react-native/packages/react-native/ReactCommon/react/renderer/attributedstring/ParagraphAttributes.h:36` | implemented | `text.png` |
| `ellipsizeMode` | `third_party/react-native/packages/react-native/ReactCommon/react/renderer/attributedstring/ParagraphAttributes.h:42` | deviating | Only `clip` versus not-`clip` is honoured: SkParagraph truncates at the tail only, so `head` and `middle` are drawn as `tail`. |
| `textBreakStrategy` | `third_party/react-native/packages/react-native/ReactCommon/react/renderer/attributedstring/ParagraphAttributes.h:47` | deviating | Ignored; SkParagraph's own line breaking is used. See *Text*, Fidelity limits. |
| `adjustsFontSizeToFit` | `third_party/react-native/packages/react-native/ReactCommon/react/renderer/attributedstring/ParagraphAttributes.h:52` | deviating | Ignored; a paragraph is never shrunk to fit its constraints. See *Text*, Fidelity limits. |
| `includeFontPadding` | `third_party/react-native/packages/react-native/ReactCommon/react/renderer/attributedstring/ParagraphAttributes.h:58` | not-implemented | #69 |
| `android_hyphenationFrequency` | `third_party/react-native/packages/react-native/ReactCommon/react/renderer/attributedstring/ParagraphAttributes.h:64` | not-implemented | #69 |
| `minimumFontSize` | `third_party/react-native/packages/react-native/ReactCommon/react/renderer/attributedstring/ParagraphAttributes.h:70` | deviating | Only meaningful with `adjustsFontSizeToFit`, which is ignored. |
| `maximumFontSize` | `third_party/react-native/packages/react-native/ReactCommon/react/renderer/attributedstring/ParagraphAttributes.h:71` | deviating | Only meaningful with `adjustsFontSizeToFit`, which is ignored. |
| `minimumFontScale` | `third_party/react-native/packages/react-native/ReactCommon/react/renderer/attributedstring/ParagraphAttributes.h:77` | deviating | Only meaningful with `adjustsFontSizeToFit`, which is ignored. |
| `textAlignVertical` | `third_party/react-native/packages/react-native/ReactCommon/react/renderer/attributedstring/ParagraphAttributes.h:83` | deviating | Ignored; a line box is placed by the vertical-metrics policy and not by an alignment. See *Text*, Fidelity limits. |

## Image

| Prop | Declared at | State | Proof, reason or owner |
| --- | --- | --- | --- |
| `sources` | `third_party/react-native/packages/react-native/ReactCommon/react/renderer/components/image/ImageProps.h:28` | implemented | `RetainedSceneImageTest, ImageStateBecomesTheImageOnTheNode` |
| `defaultSource` | `third_party/react-native/packages/react-native/ReactCommon/react/renderer/components/image/ImageProps.h:29` | deviating | Ignored; a source that has not decoded yet paints nothing rather than a placeholder. See *Image*, Fidelity limits. |
| `loadingIndicatorSource` | `third_party/react-native/packages/react-native/ReactCommon/react/renderer/components/image/ImageProps.h:30` | deviating | Ignored; there is no loading affordance. See *Image*, Fidelity limits. |
| `resizeMode` | `third_party/react-native/packages/react-native/ReactCommon/react/renderer/components/image/ImageProps.h:31` | implemented | `RetainedSceneImageTest, EveryResizeModeMapsOntoASceneResizeMode` |
| `blurRadius` | `third_party/react-native/packages/react-native/ReactCommon/react/renderer/components/image/ImageProps.h:32` | deviating | Ignored; no image filter is applied. See *Image*, Fidelity limits. |
| `capInsets` | `third_party/react-native/packages/react-native/ReactCommon/react/renderer/components/image/ImageProps.h:33` | deviating | Ignored; there is no nine-patch stretching. See *Image*, Fidelity limits. |
| `tintColor` | `third_party/react-native/packages/react-native/ReactCommon/react/renderer/components/image/ImageProps.h:34` | implemented | `RetainedSceneImageTest, OpacityMultipliesIntoTheTintAlphaAndTheImageAlpha` |
| `internal_analyticTag` | `third_party/react-native/packages/react-native/ReactCommon/react/renderer/components/image/ImageProps.h:35` | not-implemented | #69 |
| `resizeMethod` | `third_party/react-native/packages/react-native/ReactCommon/react/renderer/components/image/ImageProps.h:36` | not-implemented | #44 |
| `resizeMultiplier` | `third_party/react-native/packages/react-native/ReactCommon/react/renderer/components/image/ImageProps.h:37` | not-implemented | #44 |
| `shouldNotifyLoadEvents` | `third_party/react-native/packages/react-native/ReactCommon/react/renderer/components/image/ImageProps.h:38` | not-implemented | #44 |
| `overlayColor` | `third_party/react-native/packages/react-native/ReactCommon/react/renderer/components/image/ImageProps.h:39` | deviating | Ignored; nothing is composited under a rounded image. See *Image*, Fidelity limits. |
| `fadeDuration` | `third_party/react-native/packages/react-native/ReactCommon/react/renderer/components/image/ImageProps.h:40` | deviating | Ignored; a decoded image appears on the next damaged frame with no fade. See *Image*, Fidelity limits. |
| `progressiveRenderingEnabled` | `third_party/react-native/packages/react-native/ReactCommon/react/renderer/components/image/ImageProps.h:41` | deviating | Ignored; `SkCodec` is asked for one complete frame. See *Image*, Fidelity limits. |

## ScrollView

| Prop | Declared at | State | Proof, reason or owner |
| --- | --- | --- | --- |
| `alwaysBounceHorizontal` | `third_party/react-native/packages/react-native/ReactCommon/react/renderer/components/scrollview/BaseScrollViewProps.h:32` | not-implemented | #69 |
| `alwaysBounceVertical` | `third_party/react-native/packages/react-native/ReactCommon/react/renderer/components/scrollview/BaseScrollViewProps.h:33` | not-implemented | #69 |
| `bounces` | `third_party/react-native/packages/react-native/ReactCommon/react/renderer/components/scrollview/BaseScrollViewProps.h:34` | not-implemented | #69 |
| `bouncesZoom` | `third_party/react-native/packages/react-native/ReactCommon/react/renderer/components/scrollview/BaseScrollViewProps.h:35` | not-implemented | #69 |
| `canCancelContentTouches` | `third_party/react-native/packages/react-native/ReactCommon/react/renderer/components/scrollview/BaseScrollViewProps.h:36` | not-implemented | #69 |
| `centerContent` | `third_party/react-native/packages/react-native/ReactCommon/react/renderer/components/scrollview/BaseScrollViewProps.h:37` | not-implemented | #69 |
| `automaticallyAdjustContentInsets` | `third_party/react-native/packages/react-native/ReactCommon/react/renderer/components/scrollview/BaseScrollViewProps.h:38` | not-implemented | #69 |
| `automaticallyAdjustsScrollIndicatorInsets` | `third_party/react-native/packages/react-native/ReactCommon/react/renderer/components/scrollview/BaseScrollViewProps.h:39` | not-implemented | #69 |
| `automaticallyAdjustKeyboardInsets` | `third_party/react-native/packages/react-native/ReactCommon/react/renderer/components/scrollview/BaseScrollViewProps.h:40` | not-implemented | #69 |
| `decelerationRate` | `third_party/react-native/packages/react-native/ReactCommon/react/renderer/components/scrollview/BaseScrollViewProps.h:41` | implemented | `ScrollPhysicsTest, DecaysVelocityByTheRateRaisedToTheFrameTime` |
| `endDraggingSensitivityMultiplier` | `third_party/react-native/packages/react-native/ReactCommon/react/renderer/components/scrollview/BaseScrollViewProps.h:42` | not-implemented | #69 |
| `directionalLockEnabled` | `third_party/react-native/packages/react-native/ReactCommon/react/renderer/components/scrollview/BaseScrollViewProps.h:43` | not-implemented | #69 |
| `indicatorStyle` | `third_party/react-native/packages/react-native/ReactCommon/react/renderer/components/scrollview/BaseScrollViewProps.h:44` | not-implemented | #49 |
| `keyboardDismissMode` | `third_party/react-native/packages/react-native/ReactCommon/react/renderer/components/scrollview/BaseScrollViewProps.h:45` | not-implemented | #69 |
| `maintainVisibleContentPosition` | `third_party/react-native/packages/react-native/ReactCommon/react/renderer/components/scrollview/BaseScrollViewProps.h:46` | not-implemented | #69 |
| `maximumZoomScale` | `third_party/react-native/packages/react-native/ReactCommon/react/renderer/components/scrollview/BaseScrollViewProps.h:47` | not-implemented | #69 |
| `minimumZoomScale` | `third_party/react-native/packages/react-native/ReactCommon/react/renderer/components/scrollview/BaseScrollViewProps.h:48` | not-implemented | #69 |
| `scrollEnabled` | `third_party/react-native/packages/react-native/ReactCommon/react/renderer/components/scrollview/BaseScrollViewProps.h:49` | not-implemented | #69 |
| `pagingEnabled` | `third_party/react-native/packages/react-native/ReactCommon/react/renderer/components/scrollview/BaseScrollViewProps.h:50` | not-implemented | #69 |
| `pinchGestureEnabled` | `third_party/react-native/packages/react-native/ReactCommon/react/renderer/components/scrollview/BaseScrollViewProps.h:51` | not-implemented | #69 |
| `scrollsToTop` | `third_party/react-native/packages/react-native/ReactCommon/react/renderer/components/scrollview/BaseScrollViewProps.h:52` | not-implemented | #69 |
| `showsHorizontalScrollIndicator` | `third_party/react-native/packages/react-native/ReactCommon/react/renderer/components/scrollview/BaseScrollViewProps.h:53` | not-implemented | #49 |
| `showsVerticalScrollIndicator` | `third_party/react-native/packages/react-native/ReactCommon/react/renderer/components/scrollview/BaseScrollViewProps.h:54` | not-implemented | #49 |
| `persistentScrollbar` | `third_party/react-native/packages/react-native/ReactCommon/react/renderer/components/scrollview/BaseScrollViewProps.h:55` | not-implemented | #49 |
| `horizontal` | `third_party/react-native/packages/react-native/ReactCommon/react/renderer/components/scrollview/BaseScrollViewProps.h:56` | deviating | Both axes are always live and clamp independently, so a horizontal ScrollView works because its vertical axis has nothing to scroll rather than because the prop was read. |
| `scrollEventThrottle` | `third_party/react-native/packages/react-native/ReactCommon/react/renderer/components/scrollview/BaseScrollViewProps.h:57` | deviating | The cadence is one `onScroll` per frame, the fastest React Native ever asks for; honouring a throttle means dropping events the frame already coalesced. Issue #45 owns the cadence contract. |
| `zoomScale` | `third_party/react-native/packages/react-native/ReactCommon/react/renderer/components/scrollview/BaseScrollViewProps.h:58` | not-implemented | #69 |
| `contentInset` | `third_party/react-native/packages/react-native/ReactCommon/react/renderer/components/scrollview/BaseScrollViewProps.h:59` | not-implemented | #69 |
| `contentOffset` | `third_party/react-native/packages/react-native/ReactCommon/react/renderer/components/scrollview/BaseScrollViewProps.h:60` | implemented | `RetainedSceneScrollTest, ContentOffsetTranslatesTheChildrenOnBothAxes` |
| `scrollIndicatorInsets` | `third_party/react-native/packages/react-native/ReactCommon/react/renderer/components/scrollview/BaseScrollViewProps.h:61` | not-implemented | #49 |
| `snapToInterval` | `third_party/react-native/packages/react-native/ReactCommon/react/renderer/components/scrollview/BaseScrollViewProps.h:62` | not-implemented | #69 |
| `snapToAlignment` | `third_party/react-native/packages/react-native/ReactCommon/react/renderer/components/scrollview/BaseScrollViewProps.h:63` | not-implemented | #69 |
| `disableIntervalMomentum` | `third_party/react-native/packages/react-native/ReactCommon/react/renderer/components/scrollview/BaseScrollViewProps.h:64` | not-implemented | #69 |
| `snapToOffsets` | `third_party/react-native/packages/react-native/ReactCommon/react/renderer/components/scrollview/BaseScrollViewProps.h:65` | not-implemented | #69 |
| `snapToStart` | `third_party/react-native/packages/react-native/ReactCommon/react/renderer/components/scrollview/BaseScrollViewProps.h:66` | not-implemented | #69 |
| `snapToEnd` | `third_party/react-native/packages/react-native/ReactCommon/react/renderer/components/scrollview/BaseScrollViewProps.h:67` | not-implemented | #69 |
| `contentInsetAdjustmentBehavior` | `third_party/react-native/packages/react-native/ReactCommon/react/renderer/components/scrollview/BaseScrollViewProps.h:68` | not-implemented | #69 |
| `scrollToOverflowEnabled` | `third_party/react-native/packages/react-native/ReactCommon/react/renderer/components/scrollview/BaseScrollViewProps.h:69` | not-implemented | #69 |
| `isInvertedVirtualizedList` | `third_party/react-native/packages/react-native/ReactCommon/react/renderer/components/scrollview/BaseScrollViewProps.h:70` | not-implemented | #69 |

## TextInput

| Prop | Declared at | State | Proof, reason or owner |
| --- | --- | --- | --- |
| `paragraphAttributes` | `third_party/react-native/packages/react-native/ReactCommon/react/renderer/components/textinput/BaseTextInputProps.h:45` | implemented | `RetainedSceneTextInputTest, TheStateBecomesTheTextAndTheNodeBecomesAnEditor` |
| `defaultValue` | `third_party/react-native/packages/react-native/ReactCommon/react/renderer/components/textinput/BaseTextInputProps.h:47` | not-implemented | #53 |
| `placeholder` | `third_party/react-native/packages/react-native/ReactCommon/react/renderer/components/textinput/BaseTextInputProps.h:49` | implemented | `RetainedSceneTextInputTest, AnEmptyValueDrawsThePlaceholderInItsOwnColour` |
| `placeholderTextColor` | `third_party/react-native/packages/react-native/ReactCommon/react/renderer/components/textinput/BaseTextInputProps.h:50` | implemented | `RetainedSceneTextInputTest, AnEmptyValueDrawsThePlaceholderInItsOwnColour` |
| `cursorColor` | `third_party/react-native/packages/react-native/ReactCommon/react/renderer/components/textinput/BaseTextInputProps.h:55` | implemented | `RetainedSceneTextInputTest, CursorAndSelectionColoursOverrideTheAccent` |
| `selectionColor` | `third_party/react-native/packages/react-native/ReactCommon/react/renderer/components/textinput/BaseTextInputProps.h:56` | implemented | `RetainedSceneTextInputTest, CursorAndSelectionColoursOverrideTheAccent` |
| `selectionHandleColor` | `third_party/react-native/packages/react-native/ReactCommon/react/renderer/components/textinput/BaseTextInputProps.h:57` | not-implemented | #69 |
| `underlineColorAndroid` | `third_party/react-native/packages/react-native/ReactCommon/react/renderer/components/textinput/BaseTextInputProps.h:59` | not-implemented | #69 |
| `maxLength` | `third_party/react-native/packages/react-native/ReactCommon/react/renderer/components/textinput/BaseTextInputProps.h:61` | implemented | `EditorModelTest, MaxLengthCountsUtf16CodeUnitsSoAnEmojiCostsTwo` |
| `text` | `third_party/react-native/packages/react-native/ReactCommon/react/renderer/components/textinput/BaseTextInputProps.h:66` | implemented | `EditorModelTest, AControlledValueIsAdoptedWhenTheEventCountMatches` |
| `mostRecentEventCount` | `third_party/react-native/packages/react-native/ReactCommon/react/renderer/components/textinput/BaseTextInputProps.h:67` | implemented | `EditorModelTest, AControlledValueIsIgnoredWhileAnEditIsInFlight` |
| `autoFocus` | `third_party/react-native/packages/react-native/ReactCommon/react/renderer/components/textinput/BaseTextInputProps.h:69` | not-implemented | #54 |
| `autoCapitalize` | `third_party/react-native/packages/react-native/ReactCommon/react/renderer/components/textinput/BaseTextInputProps.h:71` | not-implemented | #69 |
| `editable` | `third_party/react-native/packages/react-native/ReactCommon/react/renderer/components/textinput/BaseTextInputProps.h:73` | not-implemented | #53 |
| `readOnly` | `third_party/react-native/packages/react-native/ReactCommon/react/renderer/components/textinput/BaseTextInputProps.h:74` | not-implemented | #53 |
| `submitBehavior` | `third_party/react-native/packages/react-native/ReactCommon/react/renderer/components/textinput/BaseTextInputProps.h:76` | not-implemented | #54 |
| `multiline` | `third_party/react-native/packages/react-native/ReactCommon/react/renderer/components/textinput/BaseTextInputProps.h:78` | implemented | `EditorModelTest, AMultilineFieldKeepsPastedNewlines` |
| `disableKeyboardShortcuts` | `third_party/react-native/packages/react-native/ReactCommon/react/renderer/components/textinput/BaseTextInputProps.h:80` | not-implemented | #54 |
| `acceptDragAndDropTypes` | `third_party/react-native/packages/react-native/ReactCommon/react/renderer/components/textinput/BaseTextInputProps.h:82` | not-implemented | #60 |
| `secureTextEntry` | `packages/core/src/TextInputComponent.h:40` | implemented | `EditorModelTest, SecureTextEntryMasksEveryGraphemeAndNeverTheBuffer` |
| `caretHidden` | `packages/core/src/TextInputComponent.h:41` | implemented | `RetainedSceneTextInputTest, CaretHiddenRemovesTheCaretColourAndTheCaretWithIt` |
| `selectTextOnFocus` | `packages/core/src/TextInputComponent.h:42` | not-implemented | #54 |
