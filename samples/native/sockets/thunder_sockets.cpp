// thunder_sockets - network sockets for Thunder programs (TCP and UDP), as a native module.
//
//     import "lib/sockets"              // the Thunder side: declarations of everything below
//     var server = tcp_listen("127.0.0.1", 0)
//     var client = tcp_connect("127.0.0.1", socket_port(server))
//
// A socket is an integer handle, like a file descriptor; text goes over the wire as UTF-8
// bytes. Blocking calls (accept, receive) wait until something arrives, or until the
// timeout set with socket_set_timeout runs out. Mistakes in the program - using a handle that
// is not open - stop it with a runtime error, like a built-in would; network conditions
// (refused, closed by the other side, timed out) are reported through return values and
// socket_error(), because a program should be able to handle those.
//
// Built with the toolchain (samples/native/CMakeLists.txt); it needs only <thunder/native.h>
// and the platform's socket library. docs/sockets.md is the reference.

#include <thunder/native.h>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#pragma comment(lib, "ws2_32.lib")
using SocketHandle = SOCKET;
constexpr SocketHandle invalid_socket = INVALID_SOCKET;
#else
#include <arpa/inet.h>
#include <cerrno>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>
using SocketHandle = int;
constexpr SocketHandle invalid_socket = -1;
#endif

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace
{
    const ThunderApi* host = nullptr;

    // One open socket. Handles given to the program are indexes into 'sockets'; a closed slot
    // is never reused, so a stale handle is caught instead of reaching another connection.
    struct Entry
    {
        SocketHandle fd = invalid_socket;
        bool listening = false;
        bool udp = false;
        bool open = false;        // false once closed by the program, or by the other side
        std::int64_t timeout_ms = -1; // -1: wait for ever
        std::string buffer;       // bytes received but not yet handed over (receive_line)
    };

    std::vector<Entry> sockets;
    std::string last_error;
    std::string returned;         // a returned text must live until the VM has copied it
    std::string udp_sender_text;  // "ip:port" of whoever sent the last datagram

    // ------------------------------------------------------------------ platform helpers

    void close_fd(SocketHandle fd)
    {
#if defined(_WIN32)
        closesocket(fd);
#else
        ::close(fd);
#endif
    }

    std::string system_error()
    {
#if defined(_WIN32)
        const int code = WSAGetLastError();
        char* buffer = nullptr;
        const DWORD length = FormatMessageA(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM
                | FORMAT_MESSAGE_IGNORE_INSERTS, nullptr, static_cast<DWORD>(code), 0,
            reinterpret_cast<char*>(&buffer), 0, nullptr);
        std::string text = length > 0 && buffer != nullptr ? std::string(buffer, length) : std::string();
        if (buffer != nullptr)
        {
            LocalFree(buffer);
        }
        while (!text.empty() && (text.back() == '\r' || text.back() == '\n' || text.back() == ' ' || text.back() == '.'))
        {
            text.pop_back();
        }
        return text.empty() ? "Windows socket error " + std::to_string(code) : text;
#else
        return std::strerror(errno);
#endif
    }

    bool startup()
    {
#if defined(_WIN32)
        static bool started = false;
        if (!started)
        {
            WSADATA data;
            started = WSAStartup(MAKEWORD(2, 2), &data) == 0;
        }
        return started;
#else
        return true;
#endif
    }

    // Waits until 'fd' is readable, for at most 'timeout_ms' (negative: for ever).
    // Returns 1 when readable, 0 on timeout, -1 on error.
    int wait_readable(SocketHandle fd, std::int64_t timeout_ms)
    {
        if (timeout_ms < 0)
        {
            return 1; // the blocking call itself waits
        }
        fd_set readable;
        FD_ZERO(&readable);
        FD_SET(fd, &readable);
        timeval limit{};
        limit.tv_sec = static_cast<long>(timeout_ms / 1000);
        limit.tv_usec = static_cast<long>((timeout_ms % 1000) * 1000);
#if defined(_WIN32)
        const int ready = select(0, &readable, nullptr, nullptr, &limit);
#else
        const int ready = select(fd + 1, &readable, nullptr, nullptr, &limit);
#endif
        if (ready < 0)
        {
            last_error = system_error();
            return -1;
        }
        if (ready == 0)
        {
            last_error = "timed out";
        }
        return ready;
    }

    std::string address_text(const sockaddr* address, socklen_t length)
    {
        char name[NI_MAXHOST] = {0};
        char service[NI_MAXSERV] = {0};
        if (getnameinfo(address, length, name, sizeof name, service, sizeof service, NI_NUMERICHOST | NI_NUMERICSERV) != 0)
        {
            return {};
        }
        return std::string(name) + ":" + service;
    }

    // ------------------------------------------------------------------ value helpers

    std::string text_of(ThunderValue value)
    {
        if (value.kind != THUNDER_STRING || value.text == nullptr || value.text_length <= 0)
        {
            return {};
        }
        return std::string(value.text, static_cast<std::size_t>(value.text_length));
    }

    ThunderValue text_result(std::string text)
    {
        returned = std::move(text);
        return thunder_text(returned.data(), static_cast<int64_t>(returned.size()));
    }

    // The entry for a handle the program passed, or nullptr after reporting the mistake.
    Entry* entry_for(ThunderEnv* env, const ThunderValue& handle, const char* function)
    {
        const std::int64_t index = handle.integer;
        if (index < 0 || index >= static_cast<std::int64_t>(sockets.size())
            || sockets[static_cast<std::size_t>(index)].fd == invalid_socket)
        {
            const std::string message = std::string(function) + ": socket " + std::to_string(index) + " is not open";
            host->fail(env, message.c_str());
            return nullptr;
        }
        return &sockets[static_cast<std::size_t>(index)];
    }

    std::int64_t add_entry(Entry entry)
    {
        sockets.push_back(std::move(entry));
        return static_cast<std::int64_t>(sockets.size() - 1);
    }

    // Resolves host:port for connect or bind. An empty host means "any address" when binding
    // and "localhost" when connecting.
    addrinfo* resolve(const std::string& host_name, std::int64_t port, bool passive, bool udp)
    {
        addrinfo hints{};
        hints.ai_family = AF_UNSPEC;
        hints.ai_socktype = udp ? SOCK_DGRAM : SOCK_STREAM;
        hints.ai_protocol = udp ? IPPROTO_UDP : IPPROTO_TCP;
        hints.ai_flags = passive ? AI_PASSIVE : 0;
        addrinfo* result = nullptr;
        const std::string service = std::to_string(port);
        const char* node = host_name.empty() ? (passive ? nullptr : "localhost") : host_name.c_str();
        const int status = getaddrinfo(node, service.c_str(), &hints, &result);
        if (status != 0)
        {
#if defined(_WIN32)
            last_error = system_error();
#else
            last_error = gai_strerror(status);
#endif
            return nullptr;
        }
        return result;
    }

    // ------------------------------------------------------------------ TCP

    // native func tcp_listen(host: string, port: int): int
    ThunderValue sockets_tcp_listen(ThunderEnv*, const ThunderValue* args, int32_t)
    {
        addrinfo* addresses = resolve(text_of(args[0]), args[1].integer, true, false);
        if (addresses == nullptr)
        {
            return thunder_int(-1);
        }
        SocketHandle fd = invalid_socket;
        for (addrinfo* address = addresses; address != nullptr; address = address->ai_next)
        {
            fd = socket(address->ai_family, address->ai_socktype, address->ai_protocol);
            if (fd == invalid_socket)
            {
                continue;
            }
            int yes = 1;
            setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&yes), sizeof yes);
            if (bind(fd, address->ai_addr, static_cast<socklen_t>(address->ai_addrlen)) == 0 && listen(fd, 16) == 0)
            {
                break;
            }
            last_error = system_error();
            close_fd(fd);
            fd = invalid_socket;
        }
        freeaddrinfo(addresses);
        if (fd == invalid_socket)
        {
            return thunder_int(-1);
        }
        Entry entry;
        entry.fd = fd;
        entry.listening = true;
        entry.open = true;
        return thunder_int(add_entry(std::move(entry)));
    }

    // native func tcp_accept(listener: int): int
    ThunderValue sockets_tcp_accept(ThunderEnv* env, const ThunderValue* args, int32_t)
    {
        Entry* listener = entry_for(env, args[0], "tcp_accept");
        if (listener == nullptr)
        {
            return thunder_int(-1);
        }
        if (!listener->listening)
        {
            host->fail(env, "tcp_accept: the socket is not a listener (use tcp_listen)");
            return thunder_int(-1);
        }
        if (wait_readable(listener->fd, listener->timeout_ms) <= 0)
        {
            return thunder_int(-1);
        }
        const SocketHandle fd = accept(listener->fd, nullptr, nullptr);
        if (fd == invalid_socket)
        {
            last_error = system_error();
            return thunder_int(-1);
        }
        Entry entry;
        entry.fd = fd;
        entry.open = true;
        entry.timeout_ms = listener->timeout_ms;
        return thunder_int(add_entry(std::move(entry)));
    }

    // native func tcp_connect(host: string, port: int): int
    ThunderValue sockets_tcp_connect(ThunderEnv*, const ThunderValue* args, int32_t)
    {
        addrinfo* addresses = resolve(text_of(args[0]), args[1].integer, false, false);
        if (addresses == nullptr)
        {
            return thunder_int(-1);
        }
        SocketHandle fd = invalid_socket;
        for (addrinfo* address = addresses; address != nullptr; address = address->ai_next)
        {
            fd = socket(address->ai_family, address->ai_socktype, address->ai_protocol);
            if (fd == invalid_socket)
            {
                continue;
            }
            if (connect(fd, address->ai_addr, static_cast<socklen_t>(address->ai_addrlen)) == 0)
            {
                break;
            }
            last_error = system_error();
            close_fd(fd);
            fd = invalid_socket;
        }
        freeaddrinfo(addresses);
        if (fd == invalid_socket)
        {
            return thunder_int(-1);
        }
        Entry entry;
        entry.fd = fd;
        entry.open = true;
        return thunder_int(add_entry(std::move(entry)));
    }

    // Sends every byte of 'data'; false (with last_error set) if the connection failed.
    bool send_all(Entry& entry, const std::string& data)
    {
        std::size_t sent = 0;
        while (sent < data.size())
        {
#if defined(_WIN32)
            const int flags = 0;
#else
#if defined(MSG_NOSIGNAL)
            const int flags = MSG_NOSIGNAL; // a closed peer must not kill the process with SIGPIPE
#else
            const int flags = 0;
#endif
#endif
            const auto count = send(entry.fd, data.data() + sent, static_cast<int>(data.size() - sent), flags);
            if (count <= 0)
            {
                last_error = system_error();
                entry.open = false;
                return false;
            }
            sent += static_cast<std::size_t>(count);
        }
        return true;
    }

    // native func socket_send(sock: int, text: string): int
    ThunderValue sockets_send(ThunderEnv* env, const ThunderValue* args, int32_t)
    {
        Entry* entry = entry_for(env, args[0], "socket_send");
        if (entry == nullptr)
        {
            return thunder_int(-1);
        }
        const std::string data = text_of(args[1]);
        return thunder_int(send_all(*entry, data) ? static_cast<std::int64_t>(data.size()) : -1);
    }

    // native func socket_send_line(sock: int, text: string): int
    ThunderValue sockets_send_line(ThunderEnv* env, const ThunderValue* args, int32_t)
    {
        Entry* entry = entry_for(env, args[0], "socket_send_line");
        if (entry == nullptr)
        {
            return thunder_int(-1);
        }
        const std::string data = text_of(args[1]) + "\n";
        return thunder_int(send_all(*entry, data) ? static_cast<std::int64_t>(data.size()) : -1);
    }

    // Reads up to 'limit' more bytes into the entry's buffer. Returns false when nothing more
    // will come (closed by the other side, timed out or failed).
    bool fill_buffer(Entry& entry, std::size_t limit)
    {
        if (!entry.open)
        {
            return false;
        }
        if (wait_readable(entry.fd, entry.timeout_ms) <= 0)
        {
            return false;
        }
        std::string chunk(limit, '\0');
        const auto count = recv(entry.fd, chunk.data(), static_cast<int>(chunk.size()), 0);
        if (count < 0)
        {
            last_error = system_error();
            entry.open = false;
            return false;
        }
        if (count == 0)
        {
            last_error = "closed by the other side";
            entry.open = false;
            return false;
        }
        entry.buffer.append(chunk, 0, static_cast<std::size_t>(count));
        return true;
    }

    // native func socket_receive(sock: int, max_bytes: int): string
    ThunderValue sockets_receive(ThunderEnv* env, const ThunderValue* args, int32_t)
    {
        Entry* entry = entry_for(env, args[0], "socket_receive");
        if (entry == nullptr)
        {
            return text_result({});
        }
        const std::size_t limit = args[1].integer > 0 ? static_cast<std::size_t>(args[1].integer) : 4096;
        if (entry->buffer.empty())
        {
            fill_buffer(*entry, limit);
        }
        const std::size_t take = entry->buffer.size() < limit ? entry->buffer.size() : limit;
        std::string result = entry->buffer.substr(0, take);
        entry->buffer.erase(0, take);
        return text_result(std::move(result));
    }

    // native func socket_receive_line(sock: int): string
    ThunderValue sockets_receive_line(ThunderEnv* env, const ThunderValue* args, int32_t)
    {
        Entry* entry = entry_for(env, args[0], "socket_receive_line");
        if (entry == nullptr)
        {
            return text_result({});
        }
        std::size_t newline = entry->buffer.find('\n');
        while (newline == std::string::npos && fill_buffer(*entry, 4096))
        {
            newline = entry->buffer.find('\n');
        }
        std::string line;
        if (newline == std::string::npos)
        {
            line.swap(entry->buffer); // the other side closed: whatever was left is the last line
        }
        else
        {
            line = entry->buffer.substr(0, newline);
            entry->buffer.erase(0, newline + 1);
        }
        if (!line.empty() && line.back() == '\r')
        {
            line.pop_back();
        }
        return text_result(std::move(line));
    }

    // native func socket_open(sock: int): bool
    ThunderValue sockets_open(ThunderEnv*, const ThunderValue* args, int32_t)
    {
        const std::int64_t index = args[0].integer;
        if (index < 0 || index >= static_cast<std::int64_t>(sockets.size()))
        {
            return thunder_bool(0);
        }
        const Entry& entry = sockets[static_cast<std::size_t>(index)];
        return thunder_bool(entry.fd != invalid_socket && (entry.open || !entry.buffer.empty()));
    }

    // native func socket_close(sock: int)
    ThunderValue sockets_close(ThunderEnv*, const ThunderValue* args, int32_t)
    {
        const std::int64_t index = args[0].integer;
        if (index >= 0 && index < static_cast<std::int64_t>(sockets.size()))
        {
            Entry& entry = sockets[static_cast<std::size_t>(index)];
            if (entry.fd != invalid_socket)
            {
                close_fd(entry.fd);
            }
            entry.fd = invalid_socket;
            entry.open = false;
            entry.buffer.clear();
        }
        return thunder_void(); // closing twice, or a handle that never was, is harmless
    }

    // native func socket_set_timeout(sock: int, milliseconds: int): bool
    ThunderValue sockets_set_timeout(ThunderEnv* env, const ThunderValue* args, int32_t)
    {
        Entry* entry = entry_for(env, args[0], "socket_set_timeout");
        if (entry == nullptr)
        {
            return thunder_bool(0);
        }
        entry->timeout_ms = args[1].integer < 0 ? -1 : args[1].integer;
        return thunder_bool(1);
    }

    // native func socket_port(sock: int): int
    ThunderValue sockets_port(ThunderEnv* env, const ThunderValue* args, int32_t)
    {
        Entry* entry = entry_for(env, args[0], "socket_port");
        if (entry == nullptr)
        {
            return thunder_int(-1);
        }
        sockaddr_storage address{};
        socklen_t length = sizeof address;
        if (getsockname(entry->fd, reinterpret_cast<sockaddr*>(&address), &length) != 0)
        {
            last_error = system_error();
            return thunder_int(-1);
        }
        if (address.ss_family == AF_INET)
        {
            return thunder_int(ntohs(reinterpret_cast<const sockaddr_in*>(&address)->sin_port));
        }
        if (address.ss_family == AF_INET6)
        {
            return thunder_int(ntohs(reinterpret_cast<const sockaddr_in6*>(&address)->sin6_port));
        }
        return thunder_int(-1);
    }

    // native func socket_peer(sock: int): string
    ThunderValue sockets_peer(ThunderEnv* env, const ThunderValue* args, int32_t)
    {
        Entry* entry = entry_for(env, args[0], "socket_peer");
        if (entry == nullptr)
        {
            return text_result({});
        }
        sockaddr_storage address{};
        socklen_t length = sizeof address;
        if (getpeername(entry->fd, reinterpret_cast<sockaddr*>(&address), &length) != 0)
        {
            last_error = system_error();
            return text_result({});
        }
        return text_result(address_text(reinterpret_cast<const sockaddr*>(&address), length));
    }

    // native func socket_error(): string
    ThunderValue sockets_error(ThunderEnv*, const ThunderValue*, int32_t)
    {
        return text_result(last_error);
    }

    // ------------------------------------------------------------------ UDP

    // native func udp_open(port: int): int
    ThunderValue sockets_udp_open(ThunderEnv*, const ThunderValue* args, int32_t)
    {
        addrinfo* addresses = resolve("", args[0].integer, true, true);
        if (addresses == nullptr)
        {
            return thunder_int(-1);
        }
        SocketHandle fd = invalid_socket;
        for (addrinfo* address = addresses; address != nullptr; address = address->ai_next)
        {
            if (address->ai_family != AF_INET)
            {
                continue; // one family keeps "host:port" texts simple; IPv4 is what the examples use
            }
            fd = socket(address->ai_family, address->ai_socktype, address->ai_protocol);
            if (fd == invalid_socket)
            {
                continue;
            }
            if (bind(fd, address->ai_addr, static_cast<socklen_t>(address->ai_addrlen)) == 0)
            {
                break;
            }
            last_error = system_error();
            close_fd(fd);
            fd = invalid_socket;
        }
        freeaddrinfo(addresses);
        if (fd == invalid_socket)
        {
            if (last_error.empty())
            {
                last_error = "no IPv4 address to bind to";
            }
            return thunder_int(-1);
        }
        Entry entry;
        entry.fd = fd;
        entry.udp = true;
        entry.open = true;
        return thunder_int(add_entry(std::move(entry)));
    }

    // native func udp_send(sock: int, host: string, port: int, text: string): int
    ThunderValue sockets_udp_send(ThunderEnv* env, const ThunderValue* args, int32_t)
    {
        Entry* entry = entry_for(env, args[0], "udp_send");
        if (entry == nullptr)
        {
            return thunder_int(-1);
        }
        if (!entry->udp)
        {
            host->fail(env, "udp_send: the socket is a TCP socket (use socket_send)");
            return thunder_int(-1);
        }
        addrinfo* addresses = resolve(text_of(args[1]), args[2].integer, false, true);
        if (addresses == nullptr)
        {
            return thunder_int(-1);
        }
        const std::string data = text_of(args[3]);
        std::int64_t result = -1;
        for (addrinfo* address = addresses; address != nullptr; address = address->ai_next)
        {
            if (address->ai_family != AF_INET)
            {
                continue;
            }
            const auto count = sendto(entry->fd, data.data(), static_cast<int>(data.size()), 0, address->ai_addr,
                static_cast<socklen_t>(address->ai_addrlen));
            if (count >= 0)
            {
                result = count;
                break;
            }
            last_error = system_error();
        }
        freeaddrinfo(addresses);
        return thunder_int(result);
    }

    // native func udp_receive(sock: int, max_bytes: int): string
    ThunderValue sockets_udp_receive(ThunderEnv* env, const ThunderValue* args, int32_t)
    {
        Entry* entry = entry_for(env, args[0], "udp_receive");
        if (entry == nullptr)
        {
            return text_result({});
        }
        if (!entry->udp)
        {
            host->fail(env, "udp_receive: the socket is a TCP socket (use socket_receive)");
            return text_result({});
        }
        udp_sender_text.clear();
        if (wait_readable(entry->fd, entry->timeout_ms) <= 0)
        {
            return text_result({});
        }
        const std::size_t limit = args[1].integer > 0 ? static_cast<std::size_t>(args[1].integer) : 65535;
        std::string datagram(limit, '\0');
        sockaddr_storage from{};
        socklen_t length = sizeof from;
        const auto count = recvfrom(entry->fd, datagram.data(), static_cast<int>(datagram.size()), 0,
            reinterpret_cast<sockaddr*>(&from), &length);
        if (count < 0)
        {
            last_error = system_error();
            return text_result({});
        }
        datagram.resize(static_cast<std::size_t>(count));
        udp_sender_text = address_text(reinterpret_cast<const sockaddr*>(&from), length);
        return text_result(std::move(datagram));
    }

    // native func udp_sender(): string
    ThunderValue sockets_udp_sender(ThunderEnv*, const ThunderValue*, int32_t)
    {
        return text_result(udp_sender_text);
    }

    // ------------------------------------------------------------------ names and addresses

    // native func host_address(name: string): string
    ThunderValue sockets_host_address(ThunderEnv*, const ThunderValue* args, int32_t)
    {
        addrinfo* addresses = resolve(text_of(args[0]), 0, false, false);
        if (addresses == nullptr)
        {
            return text_result({});
        }
        std::string text;
        for (addrinfo* address = addresses; address != nullptr && text.empty(); address = address->ai_next)
        {
            char name[NI_MAXHOST] = {0};
            if (getnameinfo(address->ai_addr, static_cast<socklen_t>(address->ai_addrlen), name, sizeof name, nullptr, 0,
                    NI_NUMERICHOST) == 0)
            {
                text = name;
            }
        }
        freeaddrinfo(addresses);
        return text_result(std::move(text));
    }

    // native func host_name(): string
    ThunderValue sockets_host_name(ThunderEnv*, const ThunderValue*, int32_t)
    {
        char name[256] = {0};
        if (gethostname(name, sizeof name - 1) != 0)
        {
            last_error = system_error();
            return text_result({});
        }
        return text_result(name);
    }
}

extern "C" THUNDER_EXPORT int thunder_module_init(ThunderRegistry* registry, const ThunderApi* api)
{
    if (registry == nullptr || api == nullptr || api->abi_version != THUNDER_NATIVE_ABI || !startup())
    {
        return 0;
    }
    host = api;
    // Every function under its English and its Pashto name, so that lib/sockets.th exists in
    // both languages (samples/en/lib and samples/ps/lib).
    const struct
    {
        const char* english;
        const char* pashto;
        ThunderFunction function;
    } exported[] = {
        {"tcp_listen", "اورېدونکی_جوړ_کړه", sockets_tcp_listen},
        {"tcp_accept", "اړیکه_ومنه", sockets_tcp_accept},
        {"tcp_connect", "ونښلېږه", sockets_tcp_connect},
        {"socket_send", "ساکټ_ولېږه", sockets_send},
        {"socket_send_line", "ساکټ_کرښه_ولېږه", sockets_send_line},
        {"socket_receive", "ساکټ_واخله", sockets_receive},
        {"socket_receive_line", "ساکټ_کرښه_واخله", sockets_receive_line},
        {"socket_open", "ساکټ_پرانیستی_دی", sockets_open},
        {"socket_close", "ساکټ_وتړه", sockets_close},
        {"socket_set_timeout", "ساکټ_وخت_حد", sockets_set_timeout},
        {"socket_port", "ساکټ_پورټ", sockets_port},
        {"socket_peer", "ساکټ_مقابل_لوری", sockets_peer},
        {"socket_error", "ساکټ_تېروتنه", sockets_error},
        {"udp_open", "یو_ډي_پي_پرانیزه", sockets_udp_open},
        {"udp_send", "یو_ډي_پي_ولېږه", sockets_udp_send},
        {"udp_receive", "یو_ډي_پي_واخله", sockets_udp_receive},
        {"udp_sender", "یو_ډي_پي_لېږونکی", sockets_udp_sender},
        {"host_address", "د_کوربه_پته", sockets_host_address},
        {"host_name", "د_کوربه_نوم", sockets_host_name},
    };
    for (const auto& function : exported)
    {
        if (!registry->add(registry, function.english, function.function)
            || !registry->add(registry, function.pashto, function.function))
        {
            return 0;
        }
    }
    return 1;
}
