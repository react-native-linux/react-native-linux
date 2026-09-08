#include "Activation.h"

#include <utility>

namespace react_native_linux {

std::optional<std::string> ActivationModel::currentUrl() const {
    const std::lock_guard<std::mutex> lock(mutex_);

    return currentUrl_;
}

void ActivationModel::onActivationUrlReceived(const std::string& url) {
    std::function<void(const std::string&)> listenerToInvoke;

    {
        const std::lock_guard<std::mutex> lock(mutex_);
        currentUrl_ = url;
        listenerToInvoke = changeListener_;
    }

    if (listenerToInvoke) {
        listenerToInvoke(url);
    }
}

void ActivationModel::setChangeListener(std::function<void(const std::string&)> listener) {
    const std::lock_guard<std::mutex> lock(mutex_);

    changeListener_ = std::move(listener);
}

} // namespace react_native_linux
