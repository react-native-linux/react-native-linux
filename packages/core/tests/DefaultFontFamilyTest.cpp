#include "DefaultFontFamily.h"

#include <gtest/gtest.h>

#include <string>

namespace {

using react_native_linux::classifyFontFamilyRequest;
using react_native_linux::FontFamilyRequestKind;

struct FontFamilyRequestCase {
    std::string fontFamily;
    FontFamilyRequestKind expected;
};

class DefaultFontFamilyTest : public testing::TestWithParam<FontFamilyRequestCase> {};

TEST_P(DefaultFontFamilyTest, ClassifiesAsExpected) {
    EXPECT_EQ(classifyFontFamilyRequest(GetParam().fontFamily), GetParam().expected);
}

INSTANTIATE_TEST_SUITE_P(
    Requests, DefaultFontFamilyTest,
    testing::Values(FontFamilyRequestCase{"", FontFamilyRequestKind::VendoredDefault},
                    FontFamilyRequestCase{"sans-serif", FontFamilyRequestKind::VendoredDefault},
                    FontFamilyRequestCase{"system-ui", FontFamilyRequestKind::VendoredDefault},
                    FontFamilyRequestCase{"serif", FontFamilyRequestKind::FontconfigGeneric},
                    FontFamilyRequestCase{"monospace", FontFamilyRequestKind::FontconfigGeneric},
                    FontFamilyRequestCase{"cursive", FontFamilyRequestKind::FontconfigGeneric},
                    FontFamilyRequestCase{"fantasy", FontFamilyRequestKind::FontconfigGeneric},
                    FontFamilyRequestCase{"Helvetica Neue", FontFamilyRequestKind::Named},
                    FontFamilyRequestCase{"Totally Missing Icons", FontFamilyRequestKind::Named},
                    FontFamilyRequestCase{"Sans-Serif", FontFamilyRequestKind::Named},
                    FontFamilyRequestCase{"Monospace", FontFamilyRequestKind::Named}));

}  // namespace
