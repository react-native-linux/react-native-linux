#include "ParagraphAttributesSignature.h"

#include <string>

#include <react/renderer/attributedstring/TextAttributes.h>

namespace react_native_linux {

/**
 * Everything a measurement depends on except the text itself and the wrap width — the cache key's other half.
 *
 * Every attribute that can move a glyph is here, and so is each fragment's *length*: two strings that join to
 * the same text but split the style boundary differently — `["ab" bold, "c" plain]` and `["a" bold, "bc"
 * plain]` — measure differently, and without the length in the key they collide.
 */
std::string toAttributesSignature(const facebook::react::AttributedString& attributedString,
                                  const facebook::react::ParagraphAttributes& paragraphAttributes) {
    std::string signature = "maxLines=" + std::to_string(paragraphAttributes.maximumNumberOfLines) +
                            ";ellipsize=" + std::to_string(static_cast<int>(paragraphAttributes.ellipsizeMode));

    for (const facebook::react::AttributedString::Fragment& fragment : attributedString.getFragments()) {
        if (fragment.isAttachment()) {
            // An attachment's box is measured from the shadow view's frame, so its dimensions are part of the key.
            const facebook::react::Size& size = fragment.parentShadowView.layoutMetrics.frame.size;

            signature += ";attachment=" + std::to_string(size.width) + "x" + std::to_string(size.height);

            continue;
        }

        const facebook::react::TextAttributes& attributes = fragment.textAttributes;

        signature +=
            ";f" + std::to_string(fragment.string.size()) + "@" + std::to_string(attributes.fontSize) + ":" +
            (attributes.fontWeight.has_value() ? std::to_string(static_cast<int>(*attributes.fontWeight)) : "none") +
            ":" +
            (attributes.fontStyle.has_value() ? std::to_string(static_cast<int>(*attributes.fontStyle)) : "none") +
            ":" +
            (attributes.fontVariant.has_value() ? std::to_string(static_cast<int>(*attributes.fontVariant)) : "none") +
            ":" + std::to_string(attributes.letterSpacing) + ":" + std::to_string(attributes.lineHeight) + ":" +
            attributes.fontFamily + ":" + std::to_string(attributes.fontSizeMultiplier) + ":" +
            std::to_string(attributes.maxFontSizeMultiplier) + ":" +
            (attributes.allowFontScaling.has_value() ? std::to_string(*attributes.allowFontScaling) : "none") + ":" +
            (attributes.textTransform.has_value() ? std::to_string(static_cast<int>(*attributes.textTransform))
                                                  : "none") +
            ":" +
            (attributes.alignment.has_value() ? std::to_string(static_cast<int>(*attributes.alignment)) : "none") +
            ":" +
            (attributes.baseWritingDirection.has_value()
                 ? std::to_string(static_cast<int>(*attributes.baseWritingDirection))
                 : "none");
    }

    return signature;
}

} // namespace react_native_linux
