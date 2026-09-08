#include "SingleInstanceActivation.h"

#include <cstdint>
#include <gtest/gtest.h>
#include <optional>
#include <string>
#include <vector>

namespace {

using react_native_linux::ActivationByteArgument;
using react_native_linux::ActivationRequest;
using react_native_linux::buildActivationRequest;
using react_native_linux::extractActivationUrl;
using react_native_linux::fromByteArgument;
using react_native_linux::singleInstanceBusName;
using react_native_linux::toByteArgument;

TEST(SingleInstanceBusNameTest, PassesThroughAnAlreadyValidReverseDnsIdentifier) {
    EXPECT_EQ(singleInstanceBusName("org.example.App"), "org.reactnativelinux.instance.org.example.App");
}

TEST(SingleInstanceBusNameTest, FoldsCharactersOutsideTheBusNameAlphabetToUnderscore) {
    EXPECT_EQ(singleInstanceBusName("com.example.my-app"), "org.reactnativelinux.instance.com.example.my_app");
    EXPECT_EQ(singleInstanceBusName("com.example.app!"), "org.reactnativelinux.instance.com.example.app_");
}

TEST(SingleInstanceBusNameTest, IsEmptyBodyForAnEmptyApplicationId) {
    EXPECT_EQ(singleInstanceBusName(""), "org.reactnativelinux.instance.");
}

TEST(SingleInstanceBusNameTest, ExercisesEveryAllowedCharacterClassAndTheInvalidFallback) {
    EXPECT_EQ(singleInstanceBusName("aA1_.!"), "org.reactnativelinux.instance.aA1_._");
}

TEST(SingleInstanceBusNameTest, FoldsACharacterPastEveryAllowedRangesUpperBound) {
    EXPECT_EQ(singleInstanceBusName("a{"), "org.reactnativelinux.instance.a_");
}

TEST(ByteArgumentTest, RoundTripsOrdinaryText) {
    const ActivationByteArgument encoded = toByteArgument("hello");

    EXPECT_EQ(fromByteArgument(encoded), "hello");
}

TEST(ByteArgumentTest, RoundTripsNonUtf8Bytes) {
    const std::string nonUtf8Path = std::string("/tmp/\xff\xfe-broken");
    const ActivationByteArgument encoded = toByteArgument(nonUtf8Path);

    EXPECT_EQ(encoded.size(), nonUtf8Path.size());
    EXPECT_EQ(fromByteArgument(encoded), nonUtf8Path);
}

TEST(ByteArgumentTest, EmptyTextRoundTripsToAnEmptyArgument) {
    EXPECT_TRUE(toByteArgument("").empty());
    EXPECT_EQ(fromByteArgument({}), "");
}

TEST(BuildActivationRequestTest, EncodesEveryArgvElementAndTheWorkingDirectory) {
    const ActivationRequest request = buildActivationRequest({"/usr/bin/app", "--flag", "myapp://open"}, "/home/user");

    ASSERT_EQ(request.argv.size(), 3U);
    EXPECT_EQ(fromByteArgument(request.argv[0]), "/usr/bin/app");
    EXPECT_EQ(fromByteArgument(request.argv[1]), "--flag");
    EXPECT_EQ(fromByteArgument(request.argv[2]), "myapp://open");
    EXPECT_EQ(fromByteArgument(request.cwd), "/home/user");
}

TEST(BuildActivationRequestTest, CarriesANonUtf8WorkingDirectoryAsRawBytes) {
    const std::string nonUtf8Cwd = std::string("/srv/\xc3(broken");
    const ActivationRequest request = buildActivationRequest({"/usr/bin/app"}, nonUtf8Cwd);

    EXPECT_EQ(fromByteArgument(request.cwd), nonUtf8Cwd);
}

TEST(ExtractActivationUrlTest, FindsAUrlAfterTheExecutablePath) {
    const ActivationRequest request = buildActivationRequest({"/usr/bin/app", "myapp://callback?token=1"}, "/");

    EXPECT_EQ(extractActivationUrl(request.argv), "myapp://callback?token=1");
}

TEST(ExtractActivationUrlTest, IgnoresArgv0EvenWhenItLooksLikeAUrl) {
    const ActivationRequest request = buildActivationRequest({"myapp://not-a-real-executable"}, "/");

    EXPECT_EQ(extractActivationUrl(request.argv), std::nullopt);
}

TEST(ExtractActivationUrlTest, ReturnsNulloptWhenNoArgumentLooksLikeAUrl) {
    const ActivationRequest request = buildActivationRequest({"/usr/bin/app", "--flag", "/some/path"}, "/");

    EXPECT_EQ(extractActivationUrl(request.argv), std::nullopt);
}

TEST(ExtractActivationUrlTest, ReturnsNulloptForAnEmptyArgv) { EXPECT_EQ(extractActivationUrl({}), std::nullopt); }

TEST(ExtractActivationUrlTest, RejectsAnEmptyArgument) {
    const ActivationRequest request = buildActivationRequest({"/usr/bin/app", ""}, "/");

    EXPECT_EQ(extractActivationUrl(request.argv), std::nullopt);
}

TEST(ExtractActivationUrlTest, RejectsAnArgumentStartingWithADigit) {
    const ActivationRequest request = buildActivationRequest({"/usr/bin/app", "1http://example.com"}, "/");

    EXPECT_EQ(extractActivationUrl(request.argv), std::nullopt);
}

TEST(ExtractActivationUrlTest, RejectsAnArgumentWithAnInvalidSchemeCharacter) {
    const ActivationRequest request = buildActivationRequest({"/usr/bin/app", "ht tp://example.com"}, "/");

    EXPECT_EQ(extractActivationUrl(request.argv), std::nullopt);
}

TEST(ExtractActivationUrlTest, AcceptsASchemeWithDigitsPlusDotAndHyphen) {
    const ActivationRequest request = buildActivationRequest({"/usr/bin/app", "a1+b.c-d://x"}, "/");

    EXPECT_EQ(extractActivationUrl(request.argv), "a1+b.c-d://x");
}

TEST(ExtractActivationUrlTest, RejectsASchemeLikeArgumentWithNoColonAtAll) {
    const ActivationRequest request = buildActivationRequest({"/usr/bin/app", "http"}, "/");

    EXPECT_EQ(extractActivationUrl(request.argv), std::nullopt);
}

TEST(ExtractActivationUrlTest, RejectsASingleCharacterArgument) {
    const ActivationRequest request = buildActivationRequest({"/usr/bin/app", "h"}, "/");

    EXPECT_EQ(extractActivationUrl(request.argv), std::nullopt);
}

TEST(ExtractActivationUrlTest, RejectsALeadCharacterPastEveryAllowedRangesUpperBound) {
    const ActivationRequest request = buildActivationRequest({"/usr/bin/app", "{nope://example.com"}, "/");

    EXPECT_EQ(extractActivationUrl(request.argv), std::nullopt);
}

TEST(ExtractActivationUrlTest, RejectsATailCharacterPastEveryAllowedRangesUpperBound) {
    const ActivationRequest request = buildActivationRequest({"/usr/bin/app", "http{oops://example.com"}, "/");

    EXPECT_EQ(extractActivationUrl(request.argv), std::nullopt);
}

TEST(ExtractActivationUrlTest, AcceptsAnUppercaseScheme) {
    const ActivationRequest request = buildActivationRequest({"/usr/bin/app", "HTTP://EXAMPLE.COM"}, "/");

    EXPECT_EQ(extractActivationUrl(request.argv), "HTTP://EXAMPLE.COM");
}

TEST(ExtractActivationUrlTest, SkipsANonMatchingArgumentBeforeAMatchingOne) {
    const ActivationRequest request =
        buildActivationRequest({"/usr/bin/app", "/some/path", "https://example.com"}, "/");

    EXPECT_EQ(extractActivationUrl(request.argv), "https://example.com");
}

} // namespace
