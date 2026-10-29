// SPDX-License-Identifier: BSD-3-Clause-Clear
#pragma once

#include <systemc>
#include <cci_configuration>
#include <async_event.h>
#include <runonsysc.h>

#include <array>
#include <atomic>
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>

#include <poll.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/un.h>
#include <unistd.h>

namespace qcom_vp {

// Hold simulated time for one external observer.
// The observer sends one request byte: P pauses, R resumes, T reads the time.
// A successful request returns nine bytes: 'O' and eight timestamp bytes.
// The timestamp is a little-endian 64-bit sc_time value.
// The module does not touch guest memory, guest registers, or watchdog state.
class ObservationClock final : public sc_core::sc_module {
public:
    cci::cci_param<std::string> socket_path{
        "socket_path", "", "Unix socket path for observer requests"};

    explicit ObservationClock(sc_core::sc_module_name name)
        : sc_module(name)
        , dispatch_("dispatch")
        , suspend_keepalive_(false)
    {
    }

    ~ObservationClock() override { teardown(); }

private:
    enum class request : std::uint8_t { pause, resume, query };

    struct answer {
        bool dispatched = false;
        std::uint64_t timestamp = 0;
    };

    static constexpr std::size_t reply_size = 9;
    static constexpr std::size_t timestamp_size = 8;
    static constexpr int accept_timeout_ms = 100;

    // All suspend and unsuspend calls run in one dispatch process.
    // The patched kernel pairs sc_suspend_all with the calling process.
    gs::runonsysc dispatch_;
    // This channel keeps the kernel parked while the hold is active.
    // Without it, a starved kernel leaves sc_start and cannot answer T.
    gs::async_event suspend_keepalive_;

    std::thread worker_;
    std::mutex fd_mutex_;
    std::atomic<bool> stopping_{false};

    // The SystemC kernel thread owns this state.
    bool held_ = false;

    bool started_ = false;
    bool torn_down_ = false;
    int listen_fd_ = -1;
    int conn_fd_ = -1;
    bool owns_path_ = false;
    dev_t path_device_ = 0;
    ino_t path_inode_ = 0;
    std::string bound_path_;
    std::string label_;

    void end_of_elaboration() override
    {
        const std::string path = socket_path.get_value();
        if (path.size() + 1 > sizeof(sockaddr_un::sun_path))
            throw std::invalid_argument("Observation clock socket path is too long");
    }

    void start_of_simulation() override
    {
        if (started_) return;
        started_ = true;
        label_ = name();
        const std::string path = socket_path.get_value();
        if (path.empty()) {
            std::cerr << "OBSERVATION_CLOCK_DISABLED model=" << label_ << " reason=no_path\n";
            return;
        }
        open_server(path);
        std::cerr << "OBSERVATION_CLOCK_READY model=" << label_ << " path=" << path << '\n';
        worker_ = std::thread(&ObservationClock::serve, this);
    }

    void end_of_simulation() override
    {
        // The kernel stops here, so no dispatch job can run.
        // Keep the suspension counter paired with its dispatch process.
        if (held_) {
            std::cerr << "OBSERVATION_CLOCK_STATE model=" << label_
                      << " state=stopped_while_paused time_ticks="
                      << sc_core::sc_time_stamp().value() << '\n';
            suspend_keepalive_.async_detach_suspending();
            held_ = false;
        }
        teardown();
    }

    // Create the listening socket. Fail on an existing path.
    void open_server(const std::string& path)
    {
        const int fd = ::socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
        if (fd < 0) {
            std::cerr << "OBSERVATION_CLOCK_FAILED model=" << label_
                      << " reason=socket error=" << errno << '\n';
            throw std::runtime_error("Observation clock cannot create a Unix socket");
        }
        sockaddr_un address{};
        address.sun_family = AF_UNIX;
        std::memcpy(address.sun_path, path.c_str(), path.size() + 1);
        if (::bind(fd, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) < 0) {
            const int error = errno;
            std::cerr << "OBSERVATION_CLOCK_FAILED model=" << label_ << " reason=bind path="
                      << path << " error=" << error << '\n';
            ::close(fd);
            throw std::runtime_error("Observation clock cannot bind the socket path");
        }
        bound_path_ = path;
        struct stat info{};
        if (::lstat(path.c_str(), &info) != 0 || !S_ISSOCK(info.st_mode)) {
            ::close(fd);
            throw std::runtime_error("Observation clock cannot inspect its socket path");
        }
        path_device_ = info.st_dev;
        path_inode_ = info.st_ino;
        owns_path_ = true;
        if (::listen(fd, 1) < 0) {
            const int error = errno;
            std::cerr << "OBSERVATION_CLOCK_FAILED model=" << label_
                      << " reason=listen error=" << error << '\n';
            ::close(fd);
            unlink_own_path();
            throw std::runtime_error("Observation clock cannot listen on the socket path");
        }
        listen_fd_ = fd;
    }

    // Remove the path only when this instance created it.
    void unlink_own_path()
    {
        if (!owns_path_) return;
        owns_path_ = false;
        if (bound_path_.empty()) return;
        struct stat info{};
        if (::lstat(bound_path_.c_str(), &info) != 0) return;
        if (!S_ISSOCK(info.st_mode)) return;
        if (info.st_dev != path_device_ || info.st_ino != path_inode_) return;
        ::unlink(bound_path_.c_str());
    }

    // Stop the accept loop, wake blocked I/O, cancel jobs, and join the worker.
    void teardown()
    {
        if (torn_down_) return;
        torn_down_ = true;
        stopping_.store(true);
        // Cancel queued jobs so a worker inside run_on_sysc returns false.
        dispatch_.end_of_simulation();
        {
            std::lock_guard<std::mutex> lock(fd_mutex_);
            if (conn_fd_ >= 0) ::shutdown(conn_fd_, SHUT_RDWR);
            if (listen_fd_ >= 0) ::shutdown(listen_fd_, SHUT_RDWR);
        }
        if (worker_.joinable()) worker_.join();
        {
            std::lock_guard<std::mutex> lock(fd_mutex_);
            if (conn_fd_ >= 0) {
                ::close(conn_fd_);
                conn_fd_ = -1;
            }
            if (listen_fd_ >= 0) {
                ::close(listen_fd_);
                listen_fd_ = -1;
            }
        }
        unlink_own_path();
    }

    void serve()
    {
        while (!stopping_.load()) {
            int client = -1;
            while (!stopping_.load()) {
                const int fd = listen_fd_;
                if (fd < 0) return;
                // A bounded wait keeps the join safe when shutdown misses accept.
                pollfd ready{fd, POLLIN, 0};
                const int watched = ::poll(&ready, 1, accept_timeout_ms);
                if (watched == 0) continue;
                if (watched < 0) {
                    if (errno == EINTR) continue;
                    if (stopping_.load()) return;
                    std::cerr << "OBSERVATION_CLOCK_FAILED model=" << label_
                              << " reason=poll error=" << errno << '\n';
                    return;
                }
                sockaddr_un peer{};
                socklen_t peer_size = sizeof(peer);
                client = ::accept(fd, reinterpret_cast<sockaddr*>(&peer), &peer_size);
                if (client >= 0) break;
                if (errno == EINTR) continue;
                if (stopping_.load()) return;
                std::cerr << "OBSERVATION_CLOCK_FAILED model=" << label_
                          << " reason=accept error=" << errno << '\n';
                return;
            }
            if (client < 0) return;
            {
                std::lock_guard<std::mutex> lock(fd_mutex_);
                if (stopping_.load()) {
                    ::close(client);
                    return;
                }
                conn_fd_ = client;
            }
            serve_client(client);
            {
                std::lock_guard<std::mutex> lock(fd_mutex_);
                if (conn_fd_ == client) conn_fd_ = -1;
                ::close(client);
            }
        }
    }

    // Serve one connection. Release this session's hold before the next accept.
    void serve_client(int client)
    {
        bool paused = false;
        while (!stopping_.load()) {
            std::uint8_t code = 0;
            if (!receive_byte(client, code)) break;
            if (code != 'P' && code != 'R' && code != 'T') {
                std::cerr << "OBSERVATION_CLOCK_FAILED model=" << label_
                          << " reason=command value=" << static_cast<unsigned>(code) << '\n';
                break;
            }
            const request command =
                code == 'P' ? request::pause
                            : (code == 'R' ? request::resume : request::query);
            const answer result = dispatch_command(command);
            if (!result.dispatched) {
                std::cerr << "OBSERVATION_CLOCK_FAILED model=" << label_
                          << " reason=dispatch\n";
                break;
            }
            if (command == request::pause) paused = true;
            if (command == request::resume) paused = false;
            if (!send_reply(client, result.timestamp)) break;
        }
        release_session_hold(paused);
    }

    // Release a hold that this session owns while the kernel still runs.
    void release_session_hold(bool paused)
    {
        if (!paused || stopping_.load()) return;
        const answer result = dispatch_command(request::resume);
        if (!result.dispatched)
            std::cerr << "OBSERVATION_CLOCK_FAILED model=" << label_
                      << " reason=release\n";
    }

    answer dispatch_command(request command)
    {
        // Shared state stays valid if the kernel cancels a running job.
        const auto shared = std::make_shared<answer>();
        const bool dispatched =
            dispatch_.run_on_sysc([this, command, shared] { apply(command, *shared); }, true);
        if (!dispatched) return answer{};
        answer result = *shared;
        result.dispatched = true;
        return result;
    }

    // A pause freezes time only when no process is unsuspendable.
    // SystemC runs timed events while a process holds sc_unsuspendable().
    // libgssync.md states that suspend_all waits for unsuspendable processes.
    // A dispatch job yields at the current time, so it cannot advance time.
    // Run in the dispatch process on the SystemC kernel thread.
    void apply(request command, answer& result)
    {
        switch (command) {
        case request::pause:
            if (!held_) {
                suspend_keepalive_.async_attach_suspending();
                sc_core::sc_suspend_all();
                held_ = true;
                log_state("paused");
            }
            break;
        case request::resume:
            if (held_) {
                sc_core::sc_unsuspend_all();
                suspend_keepalive_.async_detach_suspending();
                held_ = false;
                log_state("running");
            }
            break;
        case request::query:
            break;
        }
        result.timestamp = sc_core::sc_time_stamp().value();
    }

    void log_state(const char* state) const
    {
        std::cerr << "OBSERVATION_CLOCK_STATE model=" << label_ << " state=" << state
                  << " time_ticks=" << sc_core::sc_time_stamp().value() << '\n';
    }

    static bool receive_byte(int client, std::uint8_t& code)
    {
        for (;;) {
            const ssize_t count = ::recv(client, &code, 1, 0);
            if (count == 1) return true;
            if (count == 0) return false;
            if (errno == EINTR) continue;
            return false;
        }
    }

    static bool send_reply(int client, std::uint64_t timestamp)
    {
        std::array<std::uint8_t, reply_size> reply{};
        reply[0] = static_cast<std::uint8_t>('O');
        for (std::size_t index = 0; index < timestamp_size; ++index)
            reply[index + 1] =
                static_cast<std::uint8_t>((timestamp >> (8 * index)) & 0xffu);
        std::size_t written = 0;
        while (written < reply.size()) {
            const ssize_t count = ::send(client, reply.data() + written,
                                         reply.size() - written, MSG_NOSIGNAL);
            if (count > 0) {
                written += static_cast<std::size_t>(count);
                continue;
            }
            if (count < 0 && errno == EINTR) continue;
            return false;
        }
        return true;
    }
};

} // namespace qcom_vp
