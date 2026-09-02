#include "ImageContent.h"
#include "RetainedScene.h"

#include <gtest/gtest.h>

#include <react/renderer/graphics/Point.h>
#include <react/renderer/graphics/Rect.h>
#include <react/renderer/graphics/Size.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace {

using facebook::react::Point;
using facebook::react::Rect;
using facebook::react::Size;
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

std::shared_ptr<void> makeImage() {
    return std::make_shared<int>(0);
}

void fill(ImageCache& cache, const std::string& uri) {
    cache.insert(uri, makeImage(), kEntryByteCount);
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

TEST(ImageCacheTest, AMissReturnsNothing) {
    ImageCache cache{kEntryByteCount};

    EXPECT_EQ(cache.find("tile.png"), nullptr);
    EXPECT_EQ(cache.entryCount(), 0U);
    EXPECT_EQ(cache.byteCount(), 0U);
}

TEST(ImageCacheTest, AnInsertedImageIsFoundAndAccountedFor) {
    ImageCache cache{kEntryByteCount};
    const std::shared_ptr<void> image = makeImage();

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

    cache.insert("huge.png", makeImage(), kEntryByteCount + 1);

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
