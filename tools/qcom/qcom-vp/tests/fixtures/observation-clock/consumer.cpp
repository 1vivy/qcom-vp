// SPDX-License-Identifier: BSD-3-Clause-Clear
//
// Drives the observation-clock service over its private Unix socket.
// The service must pause and resume real SystemC time for an external
// debugger. This fixture uses a real kernel, real timers, and a real
// watchdog. Every client wait has a limit, so a fault fails the test
// instead of stalling the suite.
#include "diagnostics/observation_clock.h"
#include "qcom/sm88xx/tme_mailbox/watchdog.h"
#include <async_event.h>
#include <cci/utils/broker.h>
#include <runonsysc.h>
#include <tlm-extensions/mem-tx-attrs.h>
#include <tlm_utils/simple_initiator_socket.h>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <iostream>
#include <string>
#include <thread>

#include <sys/socket.h>
#include <sys/time.h>
#include <sys/un.h>
#include <unistd.h>

namespace {

using SteadyClock = std::chrono::steady_clock;

// The client session reports its shutdown request and its completion.
std::atomic<bool> shutdown_requested{false};
std::atomic<bool> session_done{false};

// A failed assertion must end the process, not a detached thread.
[[noreturn]] void fail(const std::string& what) {
    std::cerr << "OBSERVATION_CLOCK_CONSUMER_FAIL: " << what << std::endl;
    std::_Exit(1);
}

void require(bool condition, const std::string& what) {
    if (!condition) fail(what);
}

template <typename Predicate>
void await(Predicate done, std::chrono::milliseconds limit, const std::string& what) {
    const auto deadline = SteadyClock::now() + limit;
    while (!done()) {
        if (SteadyClock::now() >= deadline) fail(what);
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}

// The protocol answers one request byte with one status byte and a
// little-endian 64-bit time stamp.
class Client {
public:
    Client(const std::string& path, std::chrono::milliseconds limit);
    ~Client() { close_now(); }
    Client(const Client&) = delete;
    Client& operator=(const Client&) = delete;

    std::uint64_t query(char request);
    void send_only(char request);
    void set_limit(std::chrono::milliseconds limit);
    // The service must close the connection and must not acknowledge.
    void expect_rejected(const std::string& what);
    void expect_closed(const std::string& what);
    void close_now();

private:
    void connect_to(const std::string& path);
    void read_exact(unsigned char* data, std::size_t size, const std::string& what);

    std::chrono::milliseconds limit_;
    int socket_ = -1;
};

Client::Client(const std::string& path, std::chrono::milliseconds limit) : limit_(limit) {
    connect_to(path);
}

void Client::connect_to(const std::string& path) {
    sockaddr_un address{};
    address.sun_family = AF_UNIX;
    require(path.size() < sizeof address.sun_path, "socket path is too long");
    std::memcpy(address.sun_path, path.c_str(), path.size() + 1);
    // The service binds the path during start_of_simulation. Retry while
    // the simulation starts.
    const auto deadline = SteadyClock::now() + std::chrono::milliseconds(5000);
    for (;;) {
        socket_ = ::socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
        require(socket_ >= 0, "Cannot create the client socket");
        if (::connect(socket_, reinterpret_cast<const sockaddr*>(&address), sizeof address) == 0) {
            set_limit(limit_);
            return;
        }
        const auto error = errno;
        close_now();
        if (SteadyClock::now() >= deadline)
            fail(std::string{"Cannot connect to the service: "} + std::strerror(error));
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
}

void Client::set_limit(std::chrono::milliseconds limit) {
    require(socket_ >= 0, "Cannot set the limit of a closed socket");
    limit_ = limit;
    timeval value{};
    value.tv_sec = static_cast<long>(limit.count() / 1000);
    value.tv_usec = static_cast<long>((limit.count() % 1000) * 1000);
    require(::setsockopt(socket_, SOL_SOCKET, SO_RCVTIMEO, &value, sizeof value) == 0,
            "Cannot set the receive limit");
    require(::setsockopt(socket_, SOL_SOCKET, SO_SNDTIMEO, &value, sizeof value) == 0,
            "Cannot set the send limit");
}

void Client::send_only(char request) {
    require(socket_ >= 0, "Cannot send on a closed socket");
    const auto sent = ::send(socket_, &request, 1, MSG_NOSIGNAL);
    require(sent == 1, "The request byte did not transmit");
}

std::uint64_t Client::query(char request) {
    send_only(request);
    unsigned char reply[9]{};
    read_exact(reply, sizeof reply, "protocol reply");
    require(reply[0] == 'O', "The service did not acknowledge the request");
    std::uint64_t stamp = 0;
    for (unsigned index = 0; index != 8; ++index)
        stamp |= std::uint64_t{reply[index + 1]} << (8 * index);
    return stamp;
}

void Client::read_exact(unsigned char* data, std::size_t size, const std::string& what) {
    std::size_t offset = 0;
    while (offset != size) {
        const auto received = ::recv(socket_, data + offset, size - offset, 0);
        if (received > 0) {
            offset += static_cast<std::size_t>(received);
            continue;
        }
        if (received == 0) fail(what + ": the service closed the connection");
        if (errno == EINTR) continue;
        fail(what + ": " + std::strerror(errno));
    }
}

void Client::expect_rejected(const std::string& what) {
    require(socket_ >= 0, what + ": the socket is closed");
    unsigned char reply[9]{};
    for (;;) {
        const auto received = ::recv(socket_, reply, sizeof reply, 0);
        if (received > 0) fail(what + ": the service answered the request");
        if (received == 0) return;
        if (errno == EINTR) continue;
        require(errno == ECONNRESET || errno == EPIPE, what + ": " + std::strerror(errno));
        return;
    }
}

void Client::expect_closed(const std::string& what) {
    require(socket_ >= 0, what + ": the socket is closed");
    unsigned char byte = 0;
    for (;;) {
        const auto received = ::recv(socket_, &byte, 1, 0);
        if (received == 0) return;
        if (received < 0) {
            if (errno == EINTR) continue;
            require(errno == ECONNRESET || errno == EPIPE, what + ": " + std::strerror(errno));
            return;
        }
        fail(what + ": the service sent an unexpected byte");
    }
}

void Client::close_now() {
    if (socket_ < 0) return;
    ::close(socket_);
    socket_ = -1;
}

// The fixture owns the simulation side. It grants real time windows on
// request, samples the watchdog levels, and holds the scheduled timer.
struct Fixture final : sc_core::sc_module {
    qcom_vp::ObservationClock clock{"clock"};
    qcom_vp::TmeWatchdog watchdog{"watchdog"};
    tlm_utils::simple_initiator_socket<Fixture> watchdog_bus{"watchdog_bus"};
    sc_core::sc_signal<bool, sc_core::SC_MANY_WRITERS> bark{"bark"};
    sc_core::sc_signal<bool, sc_core::SC_MANY_WRITERS> bite{"bite"};
    gs::runonsysc dispatch{"dispatch"};
    // Keep the kernel active until the host grants another time window.
    gs::async_event host_keepalive{true};
    sc_core::sc_event timer_event{"timer_event"};
    sc_core::sc_event window_end{"window_end"};

    std::atomic<std::uint64_t> window_step{0};
    std::atomic<std::uint64_t> windows_done{0};
    std::atomic<std::uint64_t> bark_at{0};
    std::atomic<std::uint64_t> bite_at{0};
    std::atomic<std::uint64_t> timer_at{0};
    std::atomic<bool> bark_raised{false};
    std::atomic<bool> bite_raised{false};
    std::atomic<bool> timer_raised{false};

    SC_HAS_PROCESS(Fixture);
    explicit Fixture(sc_core::sc_module_name name) : sc_module(name) {
        watchdog_bus.bind(watchdog.target_socket);
        watchdog.bark.bind(bark);
        watchdog.bite.bind(bite);
        watchdog.clock_hz.set_value(1000000u);
        SC_THREAD(timer_waiter);
        SC_METHOD(sample_bark);
        sensitive << bark;
        dont_initialize();
        SC_METHOD(sample_bite);
        sensitive << bite;
        dont_initialize();
        SC_METHOD(pause_window);
        sensitive << window_end;
        dont_initialize();
    }

    void pause_window() { sc_core::sc_pause(); }

    void timer_waiter() {
        wait(timer_event);
        timer_at.store(sc_core::sc_time_stamp().value(), std::memory_order_relaxed);
        timer_raised.store(true, std::memory_order_release);
    }

    void sample_bark() {
        if (!bark.read()) return;
        bark_at.store(sc_core::sc_time_stamp().value(), std::memory_order_relaxed);
        bark_raised.store(true, std::memory_order_release);
    }

    void sample_bite() {
        if (!bite.read()) return;
        bite_at.store(sc_core::sc_time_stamp().value(), std::memory_order_relaxed);
        bite_raised.store(true, std::memory_order_release);
    }

    // Grant one time window. sc_main advances the kernel to its end.
    void request_window(std::uint64_t step) {
        window_step.store(step, std::memory_order_release);
        require(dispatch.run_on_sysc([] { sc_core::sc_pause(); }, true),
                "The window handoff was cancelled");
    }

    // Arm the timer and the watchdog. Call this on the SystemC thread.
    void arm_deadlines(std::uint64_t timer_delay) {
        timer_event.notify(sc_core::sc_time::from_value(timer_delay));
        write_watchdog(0x0c, 5);   // Bark after five microsecond ticks.
        write_watchdog(0x10, 10);  // Bite after ten microsecond ticks.
        write_watchdog(0x04, 1);   // Enable the counter.
        write_watchdog(0x00, 1);   // Pet the counter at the current time.
    }

    void write_watchdog(std::uint32_t offset, std::uint32_t value) {
        unsigned char bytes[4]{};
        for (unsigned index = 0; index != 4; ++index) bytes[index] = value >> (8 * index);
        QemuMemTxAttrsExtension attributes;
        attributes.attrs = {};
        attributes.attrs.unspecified = false;
        attributes.attrs.secure = true;
        tlm::tlm_generic_payload transaction;
        transaction.set_extension(&attributes);
        transaction.set_address(offset);
        transaction.set_command(tlm::TLM_WRITE_COMMAND);
        transaction.set_data_ptr(bytes);
        transaction.set_data_length(4);
        transaction.set_streaming_width(4);
        auto delay = sc_core::SC_ZERO_TIME;
        watchdog_bus->b_transport(transaction, delay);
        transaction.clear_extension<QemuMemTxAttrsExtension>();
        require(transaction.is_response_ok(), "The watchdog rejected a register write");
        require(delay == sc_core::SC_ZERO_TIME, "The watchdog delayed a register write");
    }
};

// The service must not advance time while it holds the simulation. It must
// also keep the armed deadlines pending across the hold.
void session(Fixture& fixture, const std::string& path) {
    const auto microsecond = sc_core::sc_time(1, sc_core::SC_US).value();
    Client client{path, std::chrono::milliseconds(2000)};

    require(client.query('T') == 0, "Simulation time advanced before the first window");
    const auto paused = client.query('P');
    require(paused == 0, "The pause reply does not report the frozen time");
    require(client.query('P') == paused, "A repeated pause changed the frozen time");

    // A bounded host delay must not move simulation time.
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    require(client.query('T') == paused, "Time advanced while the client held the pause");

    // Dispatch debugger-style work while the kernel is frozen.
    std::atomic<std::uint64_t> job_time{0};
    std::atomic<bool> job_ran{false};
    const auto dispatched = fixture.dispatch.run_on_sysc([&] {
        job_time.store(sc_core::sc_time_stamp().value());
        job_ran.store(true);
        fixture.arm_deadlines(20 * microsecond);
    }, true);
    require(dispatched, "The paused dispatch job was cancelled");
    require(job_ran.load(), "The paused dispatch job did not run");
    require(job_time.load() == paused, "The dispatch job did not run at the frozen time");
    require(!fixture.bark_raised.load() && !fixture.bite_raised.load() &&
            !fixture.timer_raised.load(), "A deadline fired inside the paused dispatch job");

    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    require(client.query('T') == paused, "Time advanced after the paused dispatch job");

    // A window granted while frozen must not advance time or fire a deadline.
    auto granted = fixture.windows_done.load();
    fixture.request_window(5 * microsecond);
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    require(fixture.windows_done.load() == granted, "A window advanced while the client held the pause");
    require(!fixture.bark_raised.load(), "The watchdog bark fired while the client held the pause");
    require(!fixture.bite_raised.load(), "The watchdog bite fired while the client held the pause");
    require(!fixture.timer_raised.load(), "The armed timer fired while the client held the pause");
    require(client.query('T') == paused, "Time advanced while a frozen window was pending");

    // The resume releases the hold. The pending window then runs.
    require(client.query('R') == paused, "The resume reply does not report the frozen time");
    await([&] { return fixture.windows_done.load() == granted + 1; },
          std::chrono::milliseconds(2000), "The window did not run after the resume");
    // The bark deadline falls on the window boundary, so its process may run
    // in a later delta at the same time.
    await([&] { return fixture.bark_raised.load(); },
          std::chrono::milliseconds(1000), "The watchdog bark did not survive the pause");
    require(fixture.bark_at.load() == paused + 5 * microsecond,
            "The watchdog bark did not keep its armed deadline across the pause");
    require(client.query('T') == paused + 5 * microsecond,
            "The first window did not advance exactly five microseconds");
    require(!fixture.bite_raised.load(), "The watchdog bite fired before its armed deadline");
    require(!fixture.timer_raised.load(), "The timer fired before its armed deadline");

    // The second window reaches the remaining armed deadlines.
    granted = fixture.windows_done.load();
    fixture.request_window(20 * microsecond);
    await([&] { return fixture.windows_done.load() == granted + 1; },
          std::chrono::milliseconds(2000), "The second window did not run");
    require(fixture.bite_raised.load() && fixture.bite_at.load() == paused + 10 * microsecond,
            "The watchdog bite did not keep its armed deadline across the pause");
    require(fixture.timer_raised.load() && fixture.timer_at.load() == paused + 20 * microsecond,
            "The scheduled timer did not fire at its armed deadline");
    auto now = client.query('T');
    require(now == paused + 25 * microsecond,
            "The second window did not advance exactly twenty microseconds");

    // An idle resume is harmless and must not hold the simulation.
    require(client.query('R') == now, "An idle resume failed");
    granted = fixture.windows_done.load();
    fixture.request_window(microsecond);
    await([&] { return fixture.windows_done.load() == granted + 1; },
          std::chrono::milliseconds(2000), "The window after an idle resume did not run");
    now = client.query('T');
    require(now == paused + 26 * microsecond, "An idle resume held the simulation");

    // A repeated pause takes one hold only, so one resume releases it.
    require(client.query('P') == now, "The first pause of the repeated pair failed");
    require(client.query('P') == now, "The repeated pause failed");
    granted = fixture.windows_done.load();
    fixture.request_window(microsecond);
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    require(fixture.windows_done.load() == granted, "The repeated pause did not hold the simulation");
    require(client.query('R') == now, "The resume after a repeated pause failed");
    await([&] { return fixture.windows_done.load() == granted + 1; },
          std::chrono::milliseconds(2000), "The repeated pause left a second hold behind");
    now = client.query('T');
    require(now == paused + 27 * microsecond, "The resume did not release the repeated pause");

    // An unrelated suspension belongs to its owner. Neither an idle resume
    // nor a pause and resume pair from this session may release it.
    std::atomic<bool> held{false};
    const auto hold = fixture.dispatch.run_on_sysc([&] {
        sc_core::sc_suspend_all();
        held.store(true);
    }, true);
    require(hold && held.load(), "The unrelated hold job did not run");
    require(client.query('R') == now, "An idle resume failed while an unrelated hold was active");
    granted = fixture.windows_done.load();
    fixture.request_window(microsecond);
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    require(fixture.windows_done.load() == granted,
            "An idle resume consumed an unrelated suspension");
    require(client.query('P') == now, "The pause failed while an unrelated hold was active");
    require(client.query('R') == now, "The resume failed while an unrelated hold was active");
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    require(fixture.windows_done.load() == granted,
            "The client resume consumed an unrelated suspension");
    require(client.query('T') == now, "Time advanced while an unrelated hold was active");
    const auto release = fixture.dispatch.run_on_sysc([] { sc_core::sc_unsuspend_all(); }, true);
    require(release, "The unrelated release job did not run");
    await([&] { return fixture.windows_done.load() == granted + 1; },
          std::chrono::milliseconds(2000), "The window after the unrelated release did not run");
    now = client.query('T');
    require(now == paused + 28 * microsecond, "The window after the unrelated release did not advance");

    // An unknown command must close the connection and release its hold.
    require(client.query('P') == now, "The pause before the unknown command failed");
    client.send_only('X');
    client.expect_rejected("An unknown command");
    client.close_now();
    granted = fixture.windows_done.load();
    fixture.request_window(microsecond);
    await([&] { return fixture.windows_done.load() == granted + 1; },
          std::chrono::milliseconds(2000), "An unknown command left the simulation suspended");
    now = paused + 29 * microsecond;

    // A disconnect must release the hold of that session.
    {
        Client holder{path, std::chrono::milliseconds(2000)};
        require(holder.query('T') == now, "The reconnected session reported a different time");
        require(holder.query('P') == now, "The pause before the disconnect failed");
        holder.close_now();
    }
    granted = fixture.windows_done.load();
    fixture.request_window(microsecond);
    await([&] { return fixture.windows_done.load() == granted + 1; },
          std::chrono::milliseconds(2000), "A disconnect left the simulation suspended");

    // The service must accept a new connection after a disconnect.
    Client live{path, std::chrono::milliseconds(2000)};
    require(live.query('T') == paused + 30 * microsecond,
            "The new connection did not report the advanced time");

    // end_of_simulation must close a live connection and must not deadlock.
    require(live.query('P') == paused + 30 * microsecond, "The final pause failed");
    shutdown_requested.store(true, std::memory_order_release);
    require(fixture.dispatch.run_on_sysc([] { sc_core::sc_stop(); }, false),
            "The shutdown dispatch was cancelled");
    live.set_limit(std::chrono::milliseconds(5000));
    live.expect_closed("The live connection during end_of_simulation");
    session_done.store(true, std::memory_order_release);
}

} // namespace

int sc_main(int, char**) {
    // The test uses a private directory, so a stale socket cannot survive.
    std::string directory = "/tmp/qcom-vp-observation-clock-XXXXXX";
    require(::mkdtemp(directory.data()) != nullptr, "Cannot create a private socket directory");
    const std::string path = directory + "/observation.sock";

    cci_utils::broker broker{"broker"};
    cci::cci_register_broker(broker);
    Fixture fixture{"fixture"};
    fixture.clock.socket_path.set_value(path);
    sc_core::sc_start(sc_core::SC_ZERO_TIME);

    std::thread host{[&] { session(fixture, path); }};
    // Simulation time advances only inside a window that the client granted.
    // A pending window stays pending while a hold is active.
    std::uint64_t target = 0;
    try {
        while (sc_core::sc_get_status() != sc_core::SC_STOPPED) {
            const auto step = fixture.window_step.exchange(0, std::memory_order_acquire);
            if (step != 0 && target == 0) {
                target = sc_core::sc_time_stamp().value() + step;
                fixture.window_end.notify(sc_core::sc_time::from_value(step));
            }
            if (target == 0) {
                // Serve requests at the current time. This cannot advance time.
                sc_core::sc_start(sc_core::SC_ZERO_TIME);
                if (sc_core::sc_get_status() == sc_core::SC_STOPPED) break;
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
                continue;
            }
            sc_core::sc_start(
                sc_core::sc_time::from_value(target - sc_core::sc_time_stamp().value()));
            if (sc_core::sc_get_status() == sc_core::SC_STOPPED) break;
            if (sc_core::sc_time_stamp().value() == target) {
                target = 0;
                fixture.windows_done.fetch_add(1, std::memory_order_release);
            } else {
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
        }
    } catch (const std::exception& error) {
        fail(error.what());
    }
    require(shutdown_requested.load(), "The simulation ended before the client requested shutdown");
    host.join();
    require(session_done.load(), "The client session did not complete");
    require(::access(path.c_str(), F_OK) != 0 && errno == ENOENT,
            "The observation service retained its socket after shutdown");
    ::rmdir(directory.c_str());
    std::cout << "OBSERVATION_CLOCK_PAUSE_RESUME_PASS" << std::endl;
    return 0;
}
