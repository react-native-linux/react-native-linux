#include "ImageContent.h"
#include "SceneTestSupport.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace {

using react_native_linux::animatedImageFrameIndex;
using react_native_linux::DecodedImageFrames;
using react_native_linux::ImageCache;
using react_native_linux::imagePlacement;
using react_native_linux::isAnimatedImage;
using react_native_linux::kAnimatedImageRepeatsForever;
using react_native_linux::ImageSourceKind;
using react_native_linux::ResolvedImageSource;
using react_native_linux::resolveImageSource;
using react_native_linux::SceneImageResizeMode;

constexpr char kAssetDirectory[] = "/assets";
constexpr size_t kEntryByteCount = 100;

// A 2:1 tile for a 4:3 image, so cover, contain and center all place it differently.
const Rect kTileFrame{.origin = Point{.x = 100, .y = 200}, .size = Size{.width = 400, .height = 200}};
const Size kImageSize{.width = 64, .height = 48};

ResolvedImageSource resolve(const std::string& uri) {
    return resolveImageSource(uri, kAssetDirectory);
}

std::vector<uint8_t> resolvedBytes(const std::string& payload) {
    return resolve("data:image/png;base64," + payload).bytes;
}

void expectPlacement(SceneImageResizeMode resizeMode, Size imageSize, float x, float y, float width, float height) {
    const Rect placement = imagePlacement(resizeMode, kTileFrame, imageSize);

    EXPECT_FLOAT_EQ(placement.origin.x, x);
    EXPECT_FLOAT_EQ(placement.origin.y, y);
    EXPECT_FLOAT_EQ(placement.size.width, width);
    EXPECT_FLOAT_EQ(placement.size.height, height);
}

// Whatever the decoder produced, seen as the cache and the scene see it: a pointer with a lifetime.
std::shared_ptr<void> makeFramePixels() {
    return std::make_shared<int>(0);
}

std::shared_ptr<const DecodedImageFrames> makeDecodedImage() {
    return std::make_shared<const DecodedImageFrames>(
        DecodedImageFrames{.frames = {makeFramePixels()}, .frameDurationsMilliseconds = {}, .repetitionCount = 0});
}

// The fixture animation of scripts/make-test-animation.ts, as the scene sees it: four frames, a hundred
// milliseconds each, looping forever.
constexpr int32_t kFixtureFrameMilliseconds = 100;
constexpr size_t kFixtureFrameCount = 4;
constexpr double kSixtyHertzMilliseconds = 1000.0 / 60.0;
constexpr double kOneHundredAndTwentyHertzMilliseconds = 1000.0 / 120.0;

std::shared_ptr<const DecodedImageFrames> makeAnimation(int32_t repetitionCount) {
    DecodedImageFrames decoded{.frames = {}, .frameDurationsMilliseconds = {},
                               .repetitionCount = repetitionCount};

    for (size_t frame = 0; frame < kFixtureFrameCount; frame++) {
        decoded.frames.push_back(makeFramePixels());
        decoded.frameDurationsMilliseconds.push_back(kFixtureFrameMilliseconds);
    }

    return std::make_shared<const DecodedImageFrames>(std::move(decoded));
}

void fill(ImageCache& cache, const std::string& uri) {
    cache.insert(uri, makeDecodedImage(), kEntryByteCount);
}

TEST(ImageSourceTest, AnEmptyUriIsUnsupported) {
    EXPECT_EQ(resolve("").kind, ImageSourceKind::Unsupported);
}

TEST(ImageSourceTest, AFileUriDropsItsScheme) {
    const ResolvedImageSource source = resolve("file:///tmp/tile.png");

    EXPECT_EQ(source.kind, ImageSourceKind::File);
    EXPECT_EQ(source.filePath, "/tmp/tile.png");
}

TEST(ImageSourceTest, AnAbsolutePathIsUsedAsItStands) {
    EXPECT_EQ(resolve("/tmp/tile.png").filePath, "/tmp/tile.png");
}

TEST(ImageSourceTest, ARelativePathResolvesAgainstTheAssetDirectory) {
    const ResolvedImageSource source = resolve("tile.png");

    EXPECT_EQ(source.kind, ImageSourceKind::File);
    EXPECT_EQ(source.filePath, "/assets/tile.png");
}

TEST(ImageSourceTest, RemoteSchemesAreUnsupportedBecauseThereIsNoNetworkingStack) {
    EXPECT_EQ(resolve("https://example.com/tile.png").kind, ImageSourceKind::Unsupported);
    EXPECT_EQ(resolve("asset://tile.png").kind, ImageSourceKind::Unsupported);
}

TEST(ImageSourceTest, ADataUriWithoutACommaIsUnsupported) {
    EXPECT_EQ(resolve("data:image/png;base64").kind, ImageSourceKind::Unsupported);
}

TEST(ImageSourceTest, ADataUriThatIsNotBase64IsUnsupported) {
    EXPECT_EQ(resolve("data:image/png,rawbytes").kind, ImageSourceKind::Unsupported);
}

TEST(ImageSourceTest, ADataUriWithAnEmptyPayloadIsUnsupported) {
    EXPECT_EQ(resolve("data:image/png;base64,").kind, ImageSourceKind::Unsupported);
}

TEST(ImageSourceTest, ADataUriCarriesItsDecodedBytes) {
    const ResolvedImageSource source = resolve("data:image/png;base64,AAAA");

    EXPECT_EQ(source.kind, ImageSourceKind::Data);
    EXPECT_EQ(source.bytes, (std::vector<uint8_t>{0x00, 0x00, 0x00}));
}

TEST(ImageSourceTest, TheWholeBase64AlphabetDecodes) {
    EXPECT_EQ(resolvedBytes("Az09+/"), (std::vector<uint8_t>{0x03, 0x3D, 0x3D, 0xFB}));
    EXPECT_EQ(resolvedBytes("////"), (std::vector<uint8_t>{0xFF, 0xFF, 0xFF}));
}

TEST(ImageSourceTest, Base64PaddingEndsThePayload) {
    EXPECT_EQ(resolvedBytes("QQ=="), (std::vector<uint8_t>{0x41}));
}

TEST(ImageSourceTest, ACharacterOutsideTheBase64AlphabetFailsTheWholePayload) {
    EXPECT_EQ(resolve("data:image/png;base64,AA~A").kind, ImageSourceKind::Unsupported);
}

TEST(ImagePlacementTest, StretchFillsTheFrame) {
    expectPlacement(SceneImageResizeMode::Stretch, kImageSize, 100, 200, 400, 200);
}

TEST(ImagePlacementTest, ContainScalesToTheSmallerRatioAndCentres) {
    expectPlacement(SceneImageResizeMode::Contain, kImageSize, 166.66667F, 200, 266.66667F, 200);
}

TEST(ImagePlacementTest, CoverScalesToTheLargerRatioAndOverflowsTheFrame) {
    expectPlacement(SceneImageResizeMode::Cover, kImageSize, 100, 150, 400, 300);
}

TEST(ImagePlacementTest, CentreKeepsTheNaturalSize) {
    expectPlacement(SceneImageResizeMode::Center, kImageSize, 268, 276, 64, 48);
}

TEST(ImagePlacementTest, RepeatAnchorsItsFirstTileAtTheFrameOrigin) {
    expectPlacement(SceneImageResizeMode::Repeat, kImageSize, 100, 200, 64, 48);
}

TEST(ImagePlacementTest, AnImageWithNoAreaPlacesNothing) {
    expectPlacement(SceneImageResizeMode::Cover, Size{.width = 0, .height = 48}, 100, 200, 0, 0);
    expectPlacement(SceneImageResizeMode::Cover, Size{.width = 64, .height = 0}, 100, 200, 0, 0);
}

// Issue #108. The decoded bitmap is owned by the nodes drawing it and by the cache, and by nothing else, so its
// lifetime is stated rather than hoped for: an eviction cannot take it away from a mounted node
// (react-native-macos#921), and the last node to drop it is what makes its bytes reclaimable.
//
// The cache here stands in for the process-wide one the pipeline owns; `ImageCache` is the same type, and the
// provider is the same function the host installs. See *Image* in docs/cpp-toolchain.md.

RetainedScene sceneReadingCache(ImageCache& cache) {
    RetainedScene scene;

    scene.setDecodedImageProvider([&cache](const std::string& uri) { return cache.find(uri); });
    scene.createSurfaceRoot(kSurfaceTag, Size{.width = 800, .height = 600});

    return scene;
}

const Rect kImageFrame{.origin = Point{.x = 10, .y = 20}, .size = Size{.width = 120, .height = 90}};

ShadowView imageNode(Tag tag, const std::string& uri) {
    return makeImage(tag, kImageFrame, uri, facebook::react::SharedColor{});
}

std::shared_ptr<const DecodedImageFrames> cachedImage(ImageCache& cache, const std::string& uri) {
    const std::shared_ptr<const DecodedImageFrames> image = makeDecodedImage();

    cache.insert(uri, image, kEntryByteCount);

    return image;
}

std::shared_ptr<void> firstFrame(const std::shared_ptr<const DecodedImageFrames>& decoded) {
    return decoded->frames.front();
}

std::shared_ptr<void> mountedPixels(const RetainedScene& scene) {
    const SceneSnapshot snapshot = scene.snapshot();

    if (snapshot.size() != 1U || !snapshot[0].image.has_value()) {
        return nullptr;
    }

    return snapshot[0].image.value().pixels;
}

TEST(ImageLifetimeTest, ANodeMountsWithThePixelsItsSourceWasAlreadyDecodedInto) {
    ImageCache cache{kEntryByteCount};
    RetainedScene scene = sceneReadingCache(cache);
    const std::shared_ptr<const DecodedImageFrames> image = cachedImage(cache, "tile.png");

    addChild(scene, kSurfaceTag, imageNode(2, "tile.png"));

    EXPECT_EQ(mountedPixels(scene), firstFrame(image));
}

TEST(ImageLifetimeTest, ANodeMountedWithNoProviderDrawsNothingUntilADecodeReports) {
    RetainedScene scene;

    scene.createSurfaceRoot(kSurfaceTag, Size{.width = 800, .height = 600});
    addChild(scene, kSurfaceTag, imageNode(2, "tile.png"));

    EXPECT_EQ(mountedPixels(scene), nullptr);

    // The same call the decode listener makes, with nowhere to read pixels from: it damages and hands over null.
    scene.damageImageSource("tile.png");

    EXPECT_EQ(mountedPixels(scene), nullptr);
}

TEST(ImageLifetimeTest, ADecodeThatFinishesAfterTheMountHandsItsPixelsToTheNodesDrawingIt) {
    ImageCache cache{kEntryByteCount};
    RetainedScene scene = sceneReadingCache(cache);

    addChild(scene, kSurfaceTag, imageNode(2, "tile.png"));
    EXPECT_EQ(mountedPixels(scene), nullptr);

    const std::shared_ptr<const DecodedImageFrames> image = cachedImage(cache, "tile.png");

    scene.damageImageSource("tile.png");

    EXPECT_EQ(mountedPixels(scene), firstFrame(image));
}

TEST(ImageLifetimeTest, AnEvictionCannotBlankANodeThatIsAlreadyDrawingTheImage) {
    ImageCache cache{kEntryByteCount};
    RetainedScene scene = sceneReadingCache(cache);
    const std::weak_ptr<const DecodedImageFrames> evicted = cachedImage(cache, "tile.png");

    addChild(scene, kSurfaceTag, imageNode(2, "tile.png"));

    // The cache is one entry wide, so the next source takes the first one's place.
    cachedImage(cache, "other.png");

    ASSERT_EQ(cache.find("tile.png"), nullptr);
    EXPECT_FALSE(evicted.expired());
    EXPECT_EQ(mountedPixels(scene), firstFrame(std::static_pointer_cast<const DecodedImageFrames>(evicted.lock())));
}

TEST(ImageLifetimeTest, TheLastNodeToDropASourceIsWhatMakesItsBytesReclaimable) {
    ImageCache cache{2 * kEntryByteCount};
    RetainedScene scene = sceneReadingCache(cache);
    const std::weak_ptr<const DecodedImageFrames> pixels = cachedImage(cache, "tile.png");
    const ShadowView first = imageNode(2, "tile.png");
    const ShadowView second = imageNode(3, "tile.png");

    addChild(scene, kSurfaceTag, first);
    addChild(scene, kSurfaceTag, second);

    // Evicting the cache entry leaves the two nodes as the only owners, so the bytes the cache accounted for are
    // gone and the bitmap is not.
    cachedImage(cache, "second.png");
    cachedImage(cache, "third.png");
    ASSERT_EQ(cache.byteCount(), 2 * kEntryByteCount);
    ASSERT_EQ(cache.find("tile.png"), nullptr);
    EXPECT_FALSE(pixels.expired());

    scene.removeChild(kSurfaceTag, first);
    scene.deleteNode(first.tag);
    EXPECT_FALSE(pixels.expired());

    scene.removeChild(kSurfaceTag, second);
    scene.deleteNode(second.tag);
    EXPECT_TRUE(pixels.expired());
}

TEST(ImageLifetimeTest, MountingAndUnmountingFiftyScreensHoldsNoMoreThanTheCacheCeiling) {
    constexpr int kScreenCount = 50;
    ImageCache cache{2 * kEntryByteCount};
    RetainedScene scene = sceneReadingCache(cache);
    std::vector<std::weak_ptr<const DecodedImageFrames>> decoded;

    for (int screen = 0; screen < kScreenCount; screen++) {
        const std::string uri = "screen-" + std::to_string(screen) + ".png";
        const ShadowView node = imageNode(2, uri);

        decoded.push_back(cachedImage(cache, uri));
        addChild(scene, kSurfaceTag, node);
        scene.removeChild(kSurfaceTag, node);
        scene.deleteNode(node.tag);

        EXPECT_LE(cache.byteCount(), 2 * kEntryByteCount);
    }

    // Everything but what the cache still holds is gone: 50 navigations end with two bitmaps alive, not fifty.
    size_t aliveCount = 0;

    for (const std::weak_ptr<const DecodedImageFrames>& image : decoded) {
        aliveCount += image.expired() ? 0U : 1U;
    }

    EXPECT_EQ(aliveCount, cache.entryCount());
    EXPECT_EQ(cache.byteCount(), 2 * kEntryByteCount);
}

// The passthrough the host wires: the mounting manager owns the scene, so the provider reaches it through one
// call under the same mutex every other scene write takes.
TEST(ImageLifetimeTest, TheMountingManagerHandsTheProviderToItsScene) {
    ImageCache cache{kEntryByteCount};
    LinuxMountingManager mountingManager;
    const std::shared_ptr<const DecodedImageFrames> image = cachedImage(cache, "tile.png");

    mountingManager.setDecodedImageProvider([&cache](const std::string& uri) { return cache.find(uri); });
    mountChildAndTakeFrame(mountingManager, imageNode(2, "tile.png"));

    const SceneSnapshot snapshot = mountingManager.snapshotScene();

    ASSERT_EQ(snapshot.size(), 1U);
    ASSERT_TRUE(snapshot[0].image.has_value());
    EXPECT_EQ(snapshot[0].image.value().pixels, firstFrame(image));
}

// Issue #257. Which frame of an animated source is on screen is arithmetic on the durations the codec read out of
// the file against elapsed wall-clock time, and never a count of display refreshes: core#33039 is a GIF running
// at twice its speed on a 120 Hz device because the two were confused. See *Image* in docs/cpp-toolchain.md.

// The frames one refresh rate shows over `durationMilliseconds`, with consecutive repeats collapsed — the
// sequence of frames the eye sees, independent of how often the display asked.
std::vector<size_t> framesShownAt(const DecodedImageFrames& decoded, double refreshMilliseconds,
                                  double durationMilliseconds) {
    std::vector<size_t> shown;
    double elapsedMilliseconds = 0;

    while (elapsedMilliseconds < durationMilliseconds) {
        const size_t frameIndex = animatedImageFrameIndex(decoded, elapsedMilliseconds);

        if (shown.empty() || shown.back() != frameIndex) {
            shown.push_back(frameIndex);
        }

        elapsedMilliseconds += refreshMilliseconds;
    }

    return shown;
}

TEST(AnimatedImageScheduleTest, AStillSourceIsAlwaysItsOnlyFrame) {
    const DecodedImageFrames oneFrame{.frames = {makeFramePixels()},
                                      .frameDurationsMilliseconds = {},
                                      .repetitionCount = kAnimatedImageRepeatsForever};
    const DecodedImageFrames noDurations{.frames = {makeFramePixels(), makeFramePixels()},
                                         .frameDurationsMilliseconds = {},
                                         .repetitionCount = kAnimatedImageRepeatsForever};
    const DecodedImageFrames noTime{.frames = {makeFramePixels(), makeFramePixels()},
                                    .frameDurationsMilliseconds = {0, 0},
                                    .repetitionCount = kAnimatedImageRepeatsForever};

    EXPECT_FALSE(isAnimatedImage(oneFrame));
    EXPECT_FALSE(isAnimatedImage(noDurations));
    EXPECT_FALSE(isAnimatedImage(noTime));
    EXPECT_EQ(animatedImageFrameIndex(oneFrame, 5000), 0U);
    EXPECT_EQ(animatedImageFrameIndex(noDurations, 5000), 0U);
    EXPECT_EQ(animatedImageFrameIndex(noTime, 5000), 0U);
}

TEST(AnimatedImageScheduleTest, TheFrameIsTheDurationTheElapsedTimeFallsIn) {
    const std::shared_ptr<const DecodedImageFrames> decoded = makeAnimation(kAnimatedImageRepeatsForever);

    EXPECT_TRUE(isAnimatedImage(*decoded));
    EXPECT_EQ(animatedImageFrameIndex(*decoded, 0), 0U);
    EXPECT_EQ(animatedImageFrameIndex(*decoded, 99.9), 0U);
    EXPECT_EQ(animatedImageFrameIndex(*decoded, 100), 1U);
    EXPECT_EQ(animatedImageFrameIndex(*decoded, 250), 2U);
    EXPECT_EQ(animatedImageFrameIndex(*decoded, 399.9), 3U);
    // One whole loop later, and a thousand loops later, the same instant shows the same frame.
    EXPECT_EQ(animatedImageFrameIndex(*decoded, 400), 0U);
    EXPECT_EQ(animatedImageFrameIndex(*decoded, 400650), 2U);
}

TEST(AnimatedImageScheduleTest, SixtyAndOneHundredAndTwentyHertzShowTheSameFrames) {
    const std::shared_ptr<const DecodedImageFrames> decoded = makeAnimation(kAnimatedImageRepeatsForever);
    const std::vector<size_t> expected{0, 1, 2, 3, 0, 1, 2, 3, 0, 1};

    // core#33039: a second of wall clock is two and a half loops of a four-frame, 100 ms animation whether the
    // display refreshed sixty times in it or a hundred and twenty. The 120 Hz run asks twice as often and sees
    // exactly the same ten frames go by, rather than twenty.
    EXPECT_EQ(framesShownAt(*decoded, kSixtyHertzMilliseconds, 1000), expected);
    EXPECT_EQ(framesShownAt(*decoded, kOneHundredAndTwentyHertzMilliseconds, 1000), expected);
}

TEST(AnimatedImageScheduleTest, ALoopCountHoldsTheLastFrameOnceItHasPlayedOut) {
    const std::shared_ptr<const DecodedImageFrames> once = makeAnimation(0);
    const std::shared_ptr<const DecodedImageFrames> twice = makeAnimation(1);

    // A repetition count is the number of plays *after* the first, so zero is one play through and one is two.
    EXPECT_EQ(animatedImageFrameIndex(*once, 250), 2U);
    EXPECT_EQ(animatedImageFrameIndex(*once, 400), kFixtureFrameCount - 1);
    EXPECT_EQ(animatedImageFrameIndex(*once, 5000), kFixtureFrameCount - 1);
    EXPECT_EQ(animatedImageFrameIndex(*twice, 450), 0U);
    EXPECT_EQ(animatedImageFrameIndex(*twice, 800), kFixtureFrameCount - 1);
}

TEST(AnimatedImageScheduleTest, AFrameWithNoDurationOfItsOwnIsNeverOnScreen) {
    const DecodedImageFrames decoded{.frames = {makeFramePixels(), makeFramePixels(), makeFramePixels()},
                                     .frameDurationsMilliseconds = {100, 0, 100},
                                     .repetitionCount = kAnimatedImageRepeatsForever};

    EXPECT_EQ(animatedImageFrameIndex(decoded, 50), 0U);
    EXPECT_EQ(animatedImageFrameIndex(decoded, 100), 2U);
    EXPECT_EQ(animatedImageFrameIndex(decoded, 150), 2U);
}

// The scene half of the same rule: a mounted `<Image>` moves through those frames on the frame clock, damages its
// own box when it does, and pauses entirely while an `overflow: hidden` ancestor has clipped it away.

// Five 60 Hz frames is 83 ms, which is still inside the first fixture frame, and seven is 117 ms, which is
// inside the second. Neither lands on a boundary, so nothing here turns on the last bit of a double.
constexpr int kFramesInsideTheFirstFixtureFrame = 5;
constexpr int kFramesInsideTheSecondFixtureFrame = 7;
constexpr int kFramesInsideTheThirdFixtureFrame = 15;

// Whether any of them changed frame, which is what the frame clock is handed.
bool advanceFrames(RetainedScene& scene, int frameCount) {
    bool hasAdvanced = false;

    for (int frame = 0; frame < frameCount; frame++) {
        hasAdvanced = scene.advanceImageAnimations(kSixtyHertzMilliseconds) || hasAdvanced;
    }

    return hasAdvanced;
}

RetainedScene sceneShowingAnimation(ImageCache& cache, const std::shared_ptr<const DecodedImageFrames>& animation) {
    cache.insert("loop.gif", animation, kEntryByteCount);

    RetainedScene scene = sceneReadingCache(cache);

    // A decoded still `<Image>`, an `<Image>` with nothing decoded for it and a plain `<View>` share the scene,
    // because the advance has to walk past all three of them without touching any.
    cache.insert("tile.png", makeDecodedImage(), kEntryByteCount);
    addChild(scene, kSurfaceTag, imageNode(2, "loop.gif"));
    addChild(scene, kSurfaceTag, imageNode(3, "never-decoded.png"));
    addChild(scene, kSurfaceTag, imageNode(4, "tile.png"));
    addChild(scene, kSurfaceTag, makeStyledView(5, kImageFrame, std::make_shared<ViewProps>()));
    scene.takeDamage();

    return scene;
}

size_t shownFrameIndex(const RetainedScene& scene, const DecodedImageFrames& animation) {
    const SceneSnapshot snapshot = scene.snapshot();

    for (const ScenePrimitive& primitive : snapshot) {
        if (primitive.tag != 2) {
            continue;
        }

        for (size_t index = 0; index < animation.frames.size(); index++) {
            if (primitive.image.value().pixels == animation.frames[index]) {
                return index;
            }
        }
    }

    return animation.frames.size();
}

TEST(AnimatedImageSceneTest, AnAdvanceThatCrossesAFrameBoundaryDamagesOnlyTheImagesOwnBox) {
    ImageCache cache{4 * kEntryByteCount};
    const std::shared_ptr<const DecodedImageFrames> animation = makeAnimation(kAnimatedImageRepeatsForever);
    RetainedScene scene = sceneShowingAnimation(cache, animation);

    EXPECT_EQ(shownFrameIndex(scene, *animation), 0U);

    for (int frame = 0; frame < kFramesInsideTheFirstFixtureFrame; frame++) {
        EXPECT_FALSE(scene.advanceImageAnimations(kSixtyHertzMilliseconds));
        EXPECT_TRUE(scene.takeDamage().empty());
    }

    EXPECT_TRUE(advanceFrames(scene, kFramesInsideTheSecondFixtureFrame - kFramesInsideTheFirstFixtureFrame));
    EXPECT_EQ(shownFrameIndex(scene, *animation), 1U);

    // #12: a looping animation repaints its own rectangle and nothing else, however long it runs.
    const SceneDamage damage = scene.takeDamage();

    ASSERT_EQ(damage.size(), 1U);
    EXPECT_FLOAT_EQ(damage.front().origin.x, kImageFrame.origin.x);
    EXPECT_FLOAT_EQ(damage.front().origin.y, kImageFrame.origin.y);
    EXPECT_FLOAT_EQ(damage.front().size.width, kImageFrame.size.width);
    EXPECT_FLOAT_EQ(damage.front().size.height, kImageFrame.size.height);
}

TEST(AnimatedImageSceneTest, AClippedOutAnimationSchedulesNoFrameAndResumesFromWhereItPaused) {
    ImageCache cache{4 * kEntryByteCount};
    const std::shared_ptr<const DecodedImageFrames> animation = makeAnimation(kAnimatedImageRepeatsForever);
    RetainedScene scene = sceneReadingCache(cache);
    const std::shared_ptr<ViewProps> clippingProps = std::make_shared<ViewProps>();

    cache.insert("loop.gif", animation, kEntryByteCount);
    clippingProps->yogaStyle.setOverflow(facebook::yoga::Overflow::Hidden);

    const ShadowView clipper = makeStyledView(2, makeRect(100, 100, 50, 50), clippingProps);

    addChild(scene, kSurfaceTag, clipper);
    // Laid out entirely to the right of its clipping parent, so nothing of it survives the clip.
    addChild(scene, 2, makeImage(3, makeRect(200, 0, 120, 90), "loop.gif", facebook::react::SharedColor{}));
    scene.takeDamage();

    for (int frame = 0; frame < 4 * kFramesInsideTheThirdFixtureFrame; frame++) {
        EXPECT_FALSE(scene.advanceImageAnimations(kSixtyHertzMilliseconds));
    }

    EXPECT_TRUE(scene.takeDamage().empty());

    // Visible again, and the animation picks up at the frame it paused on rather than wherever the wall clock
    // would have carried it.
    const std::shared_ptr<ViewProps> visibleProps = std::make_shared<ViewProps>();

    scene.updateNode(makeStyledView(2, makeRect(100, 100, 50, 50), visibleProps));
    scene.takeDamage();

    advanceFrames(scene, kFramesInsideTheSecondFixtureFrame);

    const SceneSnapshot snapshot = scene.snapshot();
    size_t shown = animation->frames.size();

    for (const ScenePrimitive& primitive : snapshot) {
        if (primitive.tag == 3) {
            for (size_t index = 0; index < animation->frames.size(); index++) {
                if (primitive.image.value().pixels == animation->frames[index]) {
                    shown = index;
                }
            }
        }
    }

    EXPECT_EQ(shown, 1U);
}

TEST(AnimatedImageSceneTest, AnUpdateThatKeepsTheSourceKeepsThePlaceInTheAnimation) {
    ImageCache cache{4 * kEntryByteCount};
    const std::shared_ptr<const DecodedImageFrames> animation = makeAnimation(kAnimatedImageRepeatsForever);
    RetainedScene scene = sceneShowingAnimation(cache, animation);

    advanceFrames(scene, kFramesInsideTheThirdFixtureFrame);

    ASSERT_EQ(shownFrameIndex(scene, *animation), 2U);

    scene.updateNode(imageNode(2, "loop.gif"));
    EXPECT_EQ(shownFrameIndex(scene, *animation), 2U);

    // A different source is a different animation, and it starts at its own first frame.
    scene.updateNode(imageNode(2, "other.gif"));
    EXPECT_EQ(shownFrameIndex(scene, *animation), animation->frames.size());
}

TEST(AnimatedImageSceneTest, TheMountingManagerFlagsAnAdvancedFrameAsPendingDamage) {
    ImageCache cache{4 * kEntryByteCount};
    LinuxMountingManager mountingManager;

    cache.insert("loop.gif", makeAnimation(kAnimatedImageRepeatsForever), kEntryByteCount);
    mountingManager.setDecodedImageProvider([&cache](const std::string& uri) { return cache.find(uri); });
    mountChildAndTakeFrame(mountingManager, imageNode(2, "loop.gif"));

    ASSERT_FALSE(mountingManager.hasPendingDamage());

    for (int frame = 0; frame < kFramesInsideTheFirstFixtureFrame; frame++) {
        EXPECT_FALSE(mountingManager.advanceImageAnimations(kSixtyHertzMilliseconds));
        EXPECT_FALSE(mountingManager.hasPendingDamage());
    }

    for (int frame = kFramesInsideTheFirstFixtureFrame; frame < kFramesInsideTheSecondFixtureFrame; frame++) {
        mountingManager.advanceImageAnimations(kSixtyHertzMilliseconds);
    }

    EXPECT_TRUE(mountingManager.hasPendingDamage());

    // Damage already pending stays pending whatever the next advance does.
    mountingManager.advanceImageAnimations(kSixtyHertzMilliseconds);

    EXPECT_TRUE(mountingManager.hasPendingDamage());
}

TEST(AnimatedImageSceneTest, ASourceThatDecodedIntoNoFramesPaintsNothing) {
    ImageCache cache{kEntryByteCount};
    RetainedScene scene = sceneReadingCache(cache);

    cache.insert("empty.gif", std::make_shared<const DecodedImageFrames>(), kEntryByteCount);
    addChild(scene, kSurfaceTag, imageNode(2, "empty.gif"));

    EXPECT_EQ(mountedPixels(scene), nullptr);
}

TEST(ImageCacheTest, AMissReturnsNothing) {
    ImageCache cache{kEntryByteCount};

    EXPECT_EQ(cache.find("tile.png"), nullptr);
    EXPECT_EQ(cache.entryCount(), 0U);
    EXPECT_EQ(cache.byteCount(), 0U);
}

TEST(ImageCacheTest, AnInsertedImageIsFoundAndAccountedFor) {
    ImageCache cache{kEntryByteCount};
    const std::shared_ptr<const DecodedImageFrames> image = makeDecodedImage();

    cache.insert("tile.png", image, kEntryByteCount);

    EXPECT_EQ(cache.find("tile.png"), image);
    EXPECT_EQ(cache.entryCount(), 1U);
    EXPECT_EQ(cache.byteCount(), kEntryByteCount);
}

TEST(ImageCacheTest, ReinsertingAUriReplacesItRatherThanDoubleCountingIt) {
    ImageCache cache{kEntryByteCount};

    fill(cache, "tile.png");
    fill(cache, "tile.png");

    EXPECT_EQ(cache.entryCount(), 1U);
    EXPECT_EQ(cache.byteCount(), kEntryByteCount);
}

TEST(ImageCacheTest, AnEntryLargerThanTheCapacityIsNotCachedAtAll) {
    ImageCache cache{kEntryByteCount};

    cache.insert("huge.png", makeDecodedImage(), kEntryByteCount + 1);

    EXPECT_EQ(cache.find("huge.png"), nullptr);
    EXPECT_EQ(cache.byteCount(), 0U);
}

TEST(ImageCacheTest, InsertingPastTheCapacityEvictsTheLeastRecentlyUsedEntry) {
    ImageCache cache{2 * kEntryByteCount};

    fill(cache, "first.png");
    fill(cache, "second.png");
    fill(cache, "third.png");

    EXPECT_EQ(cache.entryCount(), 2U);
    EXPECT_EQ(cache.byteCount(), 2 * kEntryByteCount);
    EXPECT_EQ(cache.find("first.png"), nullptr);
    EXPECT_NE(cache.find("second.png"), nullptr);
    EXPECT_NE(cache.find("third.png"), nullptr);
}

TEST(ImageCacheTest, AHitMakesAnEntryTheMostRecentlyUsedOne) {
    ImageCache cache{2 * kEntryByteCount};

    fill(cache, "first.png");
    fill(cache, "second.png");
    EXPECT_NE(cache.find("first.png"), nullptr);
    fill(cache, "third.png");

    EXPECT_NE(cache.find("first.png"), nullptr);
    EXPECT_EQ(cache.find("second.png"), nullptr);
}

} // namespace
