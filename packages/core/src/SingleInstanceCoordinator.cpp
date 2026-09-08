#include "SingleInstanceCoordinator.h"

#include <cstdint>
#include <vector>

namespace react_native_linux {

namespace {

constexpr char kSingleInstanceObjectPath[] = "/org/reactnativelinux/SingleInstance";
constexpr char kSingleInstanceInterface[] = "org.reactnativelinux.SingleInstance1";
constexpr char kActivateMethod[] = "Activate";

int appendActivationRequest(sd_bus_message* message, const ActivationRequest& activation) {
    int result = sd_bus_message_open_container(message, 'a', "ay");

    if (result < 0) {
        return result;
    }

    for (const ActivationByteArgument& argument : activation.argv) {
        result = sd_bus_message_append_array(message, 'y', argument.data(), argument.size());

        if (result < 0) {
            return result;
        }
    }

    result = sd_bus_message_close_container(message);

    if (result < 0) {
        return result;
    }

    return sd_bus_message_append_array(message, 'y', activation.cwd.data(), activation.cwd.size());
}

std::optional<ActivationRequest> readActivationRequest(sd_bus_message* message) {
    ActivationRequest activation;
    int result = sd_bus_message_enter_container(message, 'a', "ay");

    if (result < 0) {
        return std::nullopt;
    }

    for (;;) {
        const void* argumentData = nullptr;
        size_t argumentSize = 0;
        result = sd_bus_message_read_array(message, 'y', &argumentData, &argumentSize);

        if (result < 0) {
            return std::nullopt;
        }

        if (result == 0) {
            break;
        }

        activation.argv.emplace_back(static_cast<const uint8_t*>(argumentData),
                                     static_cast<const uint8_t*>(argumentData) + argumentSize);
    }

    if (sd_bus_message_exit_container(message) < 0) {
        return std::nullopt;
    }

    const void* cwdData = nullptr;
    size_t cwdSize = 0;

    if (sd_bus_message_read_array(message, 'y', &cwdData, &cwdSize) < 0) {
        return std::nullopt;
    }

    activation.cwd.assign(static_cast<const uint8_t*>(cwdData), static_cast<const uint8_t*>(cwdData) + cwdSize);

    return activation;
}

} // namespace

void SingleInstanceCoordinator::BusDeleter::operator()(sd_bus* bus) const noexcept { sd_bus_unref(bus); }

void SingleInstanceCoordinator::SlotDeleter::operator()(sd_bus_slot* slot) const noexcept { sd_bus_slot_unref(slot); }

int SingleInstanceCoordinator::onActivate(sd_bus_message* message, void* userData, sd_bus_error* /*error*/) {
    auto* coordinator = static_cast<SingleInstanceCoordinator*>(userData);
    const std::optional<ActivationRequest> activation = readActivationRequest(message);

    if (activation.has_value()) {
        coordinator->pendingActivationUrl_ = extractActivationUrl(activation->argv);
    }

    sd_bus_reply_method_return(message, "");

    return 0;
}

SingleInstanceCoordinator::SingleInstanceCoordinator(std::string_view applicationId,
                                                     const ActivationRequest& ownActivation) {
    sd_bus* openedBus = nullptr;

    if (sd_bus_open_user(&openedBus) < 0) {
        return;
    }

    bus_.reset(openedBus);

    const std::string busName = singleInstanceBusName(applicationId);
    const int requestResult = sd_bus_request_name(bus_.get(), busName.c_str(), 0);

    if (requestResult >= 0) {
        exportActivationObject();

        return;
    }

    isPrimaryInstance_ = false;
    forwardToPrimaryInstance(busName, ownActivation);
}

void SingleInstanceCoordinator::exportActivationObject() {
    static const sd_bus_vtable activationVTable[] = {
        SD_BUS_VTABLE_START(0),
        SD_BUS_METHOD(kActivateMethod, "aayay", "", &SingleInstanceCoordinator::onActivate, SD_BUS_VTABLE_UNPRIVILEGED),
        SD_BUS_VTABLE_END};

    sd_bus_slot* slot = nullptr;

    if (sd_bus_add_object_vtable(bus_.get(), &slot, kSingleInstanceObjectPath, kSingleInstanceInterface,
                                 activationVTable, this) >= 0) {
        activationSlot_.reset(slot);
    }
}

void SingleInstanceCoordinator::forwardToPrimaryInstance(const std::string& busName,
                                                         const ActivationRequest& ownActivation) {
    sd_bus_message* callMessage = nullptr;

    if (sd_bus_message_new_method_call(bus_.get(), &callMessage, busName.c_str(), kSingleInstanceObjectPath,
                                       kSingleInstanceInterface, kActivateMethod) < 0) {
        return;
    }

    if (appendActivationRequest(callMessage, ownActivation) >= 0) {
        sd_bus_error callError = SD_BUS_ERROR_NULL;
        sd_bus_message* reply = nullptr;

        sd_bus_call(bus_.get(), callMessage, 0, &callError, &reply);
        sd_bus_error_free(&callError);
        sd_bus_message_unref(reply);
    }

    sd_bus_message_unref(callMessage);
}

bool SingleInstanceCoordinator::isPrimaryInstance() const noexcept { return isPrimaryInstance_; }

std::optional<std::string> SingleInstanceCoordinator::takePendingActivationUrl() {
    if (!bus_) {
        return std::nullopt;
    }

    while (sd_bus_process(bus_.get(), nullptr) > 0) {
    }

    std::optional<std::string> url = std::move(pendingActivationUrl_);
    pendingActivationUrl_.reset();

    return url;
}

} // namespace react_native_linux
