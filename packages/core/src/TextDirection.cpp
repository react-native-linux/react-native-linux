#include "TextDirection.h"

namespace react_native_linux {

bool isRightToLeft(const facebook::react::TextAttributes& attributes) noexcept {
    const facebook::react::WritingDirection writing =
        attributes.baseWritingDirection.value_or(facebook::react::WritingDirection::Natural);

    if (writing != facebook::react::WritingDirection::Natural) {
        return writing == facebook::react::WritingDirection::RightToLeft;
    }

    return attributes.layoutDirection == facebook::react::LayoutDirection::RightToLeft;
}

} // namespace react_native_linux
