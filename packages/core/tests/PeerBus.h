#pragma once

#include "SdBus.h"
#include "SocketPairBus.h"

#include <array>
#include <atomic>
#include <functional>
#include <gtest/gtest.h>
#include <memory>
#include <stdexcept>
#include <string>
#include <sys/socket.h>
#include <systemd/sd-bus.h>
#include <thread>
#include <unistd.h>
#include <utility>

namespace react_native_linux::test_support {

/**
 * A D-Bus service at the far end of a socket pair, standing in for one on the session bus: `SecretServiceTest`'s
 * keyring and `OpenUriPortalTest`'s portal. A peer connection needs no bus daemon, so a test runs wherever sd-bus does.
 *
 * Threading contract: the bus is processed on this object's own thread, and every method call reaches `answer` there;
 * `bus` is for sending from `answer`. Once `answer` returns false the thread stops reading, as a stalled service does.
 * The destructor joins the thread before anything else goes, so an owner that declares this last may answer from its
 * other members.
 */
class PeerBus final {
public:
    /** Answers `call`, and returns whether to go on reading. */
    using Answer = std::function<bool(sd_bus_message* call)>;

    explicit PeerBus(Answer answer) : answer_(std::move(answer)) {
        std::array<int, 2> descriptors{-1, -1};
        sd_id128_t identifier{};
        sd_bus* server = nullptr;

        EXPECT_EQ(::socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, descriptors.data()), 0);
        clientDescriptor_ = descriptors[1];
        EXPECT_GE(sd_bus_new(&server), 0);
        server_.reset(server);
        EXPECT_GE(sd_bus_set_fd(server, descriptors[0], descriptors[0]), 0);
        EXPECT_GE(sd_id128_randomize(&identifier), 0);
        EXPECT_GE(sd_bus_set_server(server, 1, identifier), 0);
        EXPECT_GE(sd_bus_add_filter(server, nullptr, &PeerBus::onMessage, this), 0);
        EXPECT_GE(sd_bus_start(server), 0);
        thread_ = std::thread([this] {
            while (!stopping_.load() && reading_) {
                const int processed = sd_bus_process(server_.get(), nullptr);

                if (processed < 0) {
                    return;
                }

                if (processed == 0) {
                    sd_bus_wait(server_.get(), 10'000);
                }
            }
        });
    }

    PeerBus(const PeerBus&) = delete;
    PeerBus(PeerBus&&) = delete;
    PeerBus& operator=(const PeerBus&) = delete;
    PeerBus& operator=(PeerBus&&) = delete;

    ~PeerBus() {
        stopping_.store(true);
        thread_.join();
        ::close(clientDescriptor_);
    }

    [[nodiscard]] sd_bus* bus() const noexcept { return server_.get(); }

    /** Connects a client to this service, as `sd_bus_open_user` connects one to the session bus. */
    [[nodiscard]] SdBusOpener opener() const { return socketPairOpener(clientDescriptor_); }

private:
    struct BusClose {
        void operator()(sd_bus* bus) const noexcept { sd_bus_flush_close_unref(bus); }
    };

    static int onMessage(sd_bus_message* message, void* userData, sd_bus_error* /*error*/) {
        if (sd_bus_message_is_method_call(message, nullptr, nullptr) <= 0) {
            return 0;
        }

        auto* peer = static_cast<PeerBus*>(userData);

        peer->reading_ = peer->answer_(message);

        return 1;
    }

    Answer answer_;
    std::unique_ptr<sd_bus, BusClose> server_;
    int clientDescriptor_{-1};
    std::atomic<bool> stopping_{false};
    bool reading_{true};
    std::thread thread_;
};

/** An opener that fails with `error`, as `sd_bus_open_user` does without a session bus. */
inline SdBusOpener failingWith(int error) {
    return [error](sd_bus**) { return error; };
}

/** The message `work` fails with, or `no error`. */
template <typename Work> std::string failureOf(Work work) {
    std::string failure = "no error";

    try {
        work();
    } catch (const std::runtime_error& error) {
        failure = error.what();
    }

    return failure;
}

} // namespace react_native_linux::test_support
