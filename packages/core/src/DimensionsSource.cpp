#include "DimensionsSource.h"

#include <mutex>
#include <optional>

namespace react_native_linux {

void DimensionsSource::configure(double width, double height, double scale) {
    if (width <= 0.0 || height <= 0.0 || scale <= 0.0) {
        return;
    }

    const std::lock_guard<std::mutex> lock(mutex_);

    if (metrics_.width == width && metrics_.height == height && metrics_.scale == scale) {
        return;
    }

    metrics_.width = width;
    metrics_.height = height;
    metrics_.scale = scale;
    hasPendingChange_ = true;
}

DisplayMetrics DimensionsSource::metrics() const {
    const std::lock_guard<std::mutex> lock(mutex_);

    return metrics_;
}

std::optional<DisplayMetrics> DimensionsSource::takeChangeIfAny() {
    const std::lock_guard<std::mutex> lock(mutex_);

    if (!hasPendingChange_) {
        return std::nullopt;
    }

    hasPendingChange_ = false;

    return metrics_;
}

} // namespace react_native_linux
