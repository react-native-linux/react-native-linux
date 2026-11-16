#include "OpenUriPortal.h"

#include <cerrno>
#include <cstdint>
#include <fcntl.h>
#include <stdexcept>
#include <string>
#include <sys/stat.h>
#include <system_error>
#include <unistd.h>
#include <utility>

namespace react_native_linux {

namespace {

constexpr char kDestination[] = "org.freedesktop.portal.Desktop";
constexpr char kPath[] = "/org/freedesktop/portal/desktop";
constexpr char kInterface[] = "org.freedesktop.portal.OpenURI";

struct DescriptorClose {
    explicit DescriptorClose(int descriptor) : descriptor(descriptor) {}

    DescriptorClose(const DescriptorClose&) = delete;
    DescriptorClose(DescriptorClose&&) = delete;
    DescriptorClose& operator=(const DescriptorClose&) = delete;
    DescriptorClose& operator=(DescriptorClose&&) = delete;

    ~DescriptorClose() { ::close(descriptor); }

    int descriptor;
};

} // namespace

void OpenUriPortal::BusClose::operator()(sd_bus* bus) const noexcept { sd_bus_close_unref(bus); }

OpenUriPortal::OpenUriPortal(SdBusOpener openBus, std::chrono::microseconds timeout)
    : openBus_(std::move(openBus)), timeout_(timeout) {}

sd_bus* OpenUriPortal::connection(int& result) {
    result = 0;

    if (bus_ != nullptr && sd_bus_is_open(bus_.get()) > 0) {
        return bus_.get();
    }

    sd_bus* bus = nullptr;

    result = openBus_(&bus);
    bus_.reset(bus);

    if (result < 0) {
        bus_.reset();
        return nullptr;
    }

    sd_bus_set_method_call_timeout(bus, timeout_.count());

    return bus;
}

bool OpenUriPortal::isAvailable() {
    int result = 0;
    sd_bus* const bus = connection(result);
    std::uint32_t version = 0;

    return bus != nullptr &&
           sd_bus_get_property_trivial(bus, kDestination, kPath, kInterface, "version", nullptr, 'u', &version) >= 0;
}

void OpenUriPortal::openFile(const std::filesystem::path& file) {
    const int descriptor = ::open(file.c_str(), O_RDONLY | O_CLOEXEC | O_NONBLOCK);

    if (descriptor < 0) {
        throw std::system_error(errno, std::generic_category(), "cannot open " + file.string());
    }

    const DescriptorClose closing(descriptor);

    struct stat status {};

    // A failed fstat leaves the mode zero, which is not a regular file either.
    ::fstat(descriptor, &status);

    if (!S_ISREG(status.st_mode)) {
        throw std::runtime_error("cannot share " + file.string() + ": not a regular file");
    }

    int result = 0;
    sd_bus* const bus = connection(result);

    if (bus == nullptr) {
        throw std::system_error(-result, std::generic_category(), "cannot open the session bus");
    }

    SdBusError error;

    result = sd_bus_call_method(bus, kDestination, kPath, kInterface, "OpenFile", &error.value, nullptr, "sha{sv}", "",
                                descriptor, 1, "ask", "b", 1);

    if (result < 0) {
        sd_bus_error_set_errno(&error.value, result);
        throw std::runtime_error(std::string("the OpenURI portal refused it: ") + error.value.name);
    }
}

} // namespace react_native_linux
