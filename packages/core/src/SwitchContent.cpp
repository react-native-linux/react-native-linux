#include "SwitchContent.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace react_native_linux {

namespace {

constexpr uint32_t kChannelMask = 0xFFU;
constexpr int kChannelBits = 8;
constexpr int kChannelCount = 4;

uint32_t mixChannel(uint32_t first, uint32_t second, float fraction, int shift) {
    const float firstChannel = static_cast<float>((first >> shift) & kChannelMask);
    const float secondChannel = static_cast<float>((second >> shift) & kChannelMask);
    const float mixed = std::round(firstChannel + ((secondChannel - firstChannel) * fraction));

    return static_cast<uint32_t>(mixed) << shift;
}

facebook::react::BorderRadii pillRadii(const facebook::react::Rect& frame) {
    const float radius = static_cast<float>(std::min(frame.size.width, frame.size.height) / 2);
    const facebook::react::CornerRadii corner{.vertical = radius, .horizontal = radius};

    return facebook::react::BorderRadii{
        .topLeft = corner, .topRight = corner, .bottomLeft = corner, .bottomRight = corner};
}

} // namespace

SwitchGeometry switchGeometry(const facebook::react::Rect& frame, float thumbProgress) {
    const float clampedProgress = std::clamp(thumbProgress, 0.0F, 1.0F);
    const float thumbRadius = std::max(static_cast<float>(frame.size.height / 2) - kSwitchThumbInset, 0.0F);
    const float travelStart = static_cast<float>(frame.origin.x) + kSwitchThumbInset + thumbRadius;
    const float travelEnd =
        static_cast<float>(frame.origin.x + frame.size.width) - kSwitchThumbInset - thumbRadius;
    const float travel = std::max(travelEnd, travelStart) - travelStart;

    return SwitchGeometry{.track = roundedBorderBox(frame, pillRadii(frame)),
                          .thumbCenter =
                              facebook::react::Point{.x = travelStart + (travel * clampedProgress),
                                                     .y = frame.origin.y + (frame.size.height / 2)},
                          .thumbRadius = thumbRadius};
}

float advanceSwitchThumbProgress(float thumbProgress, bool isOn, double frameMilliseconds) {
    const float step = static_cast<float>(frameMilliseconds / kSwitchToggleMilliseconds);

    if (isOn) {
        return std::min(thumbProgress + step, 1.0F);
    }

    return std::max(thumbProgress - step, 0.0F);
}

uint32_t switchTrackColorArgb(const SceneSwitchContent& content) {
    const float fraction = std::clamp(content.thumbProgress, 0.0F, 1.0F);
    uint32_t mixed = 0;

    for (int channel = 0; channel < kChannelCount; ++channel) {
        mixed |= mixChannel(content.trackOffColorArgb, content.trackOnColorArgb, fraction, channel * kChannelBits);
    }

    return mixed;
}

} // namespace react_native_linux
