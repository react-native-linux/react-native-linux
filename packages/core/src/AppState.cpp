#include "AppState.h"

#include <utility>

namespace react_native_linux {

AppStateSnapshot appStateOf(const ToplevelState& state) noexcept {
    return AppStateSnapshot{.status = state.suspended ? AppStatus::Background : AppStatus::Active,
                            .isFocused = state.activated};
}

std::string_view nameOfAppStatus(AppStatus status) noexcept {
    return status == AppStatus::Active ? "active" : "background";
}

AppStateSnapshot AppStateModel::current() const {
    const std::lock_guard<std::mutex> lock(mutex_);

    return current_;
}

void AppStateModel::seed(const ToplevelState& state) {
    const std::lock_guard<std::mutex> lock(mutex_);

    current_ = appStateOf(state);
}

void AppStateModel::onToplevelStateChanged(const ToplevelState& state) {
    const AppStateSnapshot next = appStateOf(state);
    AppStateSnapshot previous;
    ChangeListener listenerToInvoke;

    {
        const std::lock_guard<std::mutex> lock(mutex_);

        if (next == current_) {
            return;
        }

        previous = std::exchange(current_, next);
        listenerToInvoke = changeListener_;
    }

    if (listenerToInvoke) {
        listenerToInvoke(previous, next);
    }
}

void AppStateModel::setChangeListener(ChangeListener listener) {
    const std::lock_guard<std::mutex> lock(mutex_);

    changeListener_ = std::move(listener);
}

} // namespace react_native_linux
