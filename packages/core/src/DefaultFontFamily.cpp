#include "DefaultFontFamily.h"

namespace react_native_linux {

FontFamilyRequestKind classifyFontFamilyRequest(const std::string& fontFamily) {
    if (fontFamily.empty() || fontFamily == "sans-serif" || fontFamily == "system-ui") {
        return FontFamilyRequestKind::VendoredDefault;
    }

    if (fontFamily == "serif" || fontFamily == "monospace" || fontFamily == "cursive" || fontFamily == "fantasy") {
        return FontFamilyRequestKind::FontconfigGeneric;
    }

    return FontFamilyRequestKind::Named;
}

}  // namespace react_native_linux
