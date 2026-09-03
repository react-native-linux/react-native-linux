#include "ImageContent.h"
#include "SceneTestSupport.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <string>

namespace {

using react_native_linux::ImageCache;
using react_native_linux::imagePlacement;
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
std::shared_ptr<void> makeDecodedPixels() {
    return std::make_shared<int>(0);
}

void fill(ImageCache& cache, const std::string& uri) {
    cache.insert(uri, makeDecodedPixels(), kEntryByteCount);
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

std::shared_ptr<void> cachedImage(ImageCache& cache, const std::string& uri) {
    const std::shared_ptr<void> image = makeDecodedPixels();

    cache.insert(uri, image, kEntryByteCount);

    return image;
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
    const std::shared_ptr<void> image = cachedImage(cache, "tile.png");

    addChild(scene, kSurfaceTag, imageNode(2, "tile.png"));

    EXPECT_EQ(mountedPixels(scene), image);
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

    const std::shared_ptr<void> image = cachedImage(cache, "tile.png");

    scene.damageImageSource("tile.png");

    EXPECT_EQ(mountedPixels(scene), image);
}

TEST(ImageLifetimeTest, AnEvictionCannotBlankANodeThatIsAlreadyDrawingTheImage) {
    ImageCache cache{kEntryByteCount};
    RetainedScene scene = sceneReadingCache(cache);
    const std::weak_ptr<void> evicted = cachedImage(cache, "tile.png");

    addChild(scene, kSurfaceTag, imageNode(2, "tile.png"));

    // The cache is one entry wide, so the next source takes the first one's place.
    cachedImage(cache, "other.png");

    ASSERT_EQ(cache.find("tile.png"), nullptr);
    EXPECT_FALSE(evicted.expired());
    EXPECT_EQ(mountedPixels(scene), evicted.lock());
}

TEST(ImageLifetimeTest, TheLastNodeToDropASourceIsWhatMakesItsBytesReclaimable) {
    ImageCache cache{2 * kEntryByteCount};
    RetainedScene scene = sceneReadingCache(cache);
    const std::weak_ptr<void> pixels = cachedImage(cache, "tile.png");
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
    std::vector<std::weak_ptr<void>> decoded;

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

    for (const std::weak_ptr<void>& image : decoded) {
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
    const std::shared_ptr<void> image = cachedImage(cache, "tile.png");

    mountingManager.setDecodedImageProvider([&cache](const std::string& uri) { return cache.find(uri); });
    mountChildAndTakeFrame(mountingManager, imageNode(2, "tile.png"));

    const SceneSnapshot snapshot = mountingManager.snapshotScene();

    ASSERT_EQ(snapshot.size(), 1U);
    ASSERT_TRUE(snapshot[0].image.has_value());
    EXPECT_EQ(snapshot[0].image.value().pixels, image);
}

TEST(ImageCacheTest, AMissReturnsNothing) {
    ImageCache cache{kEntryByteCount};

    EXPECT_EQ(cache.find("tile.png"), nullptr);
    EXPECT_EQ(cache.entryCount(), 0U);
    EXPECT_EQ(cache.byteCount(), 0U);
}

TEST(ImageCacheTest, AnInsertedImageIsFoundAndAccountedFor) {
    ImageCache cache{kEntryByteCount};
    const std::shared_ptr<void> image = makeDecodedPixels();

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

    cache.insert("huge.png", makeDecodedPixels(), kEntryByteCount + 1);

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
