#include "automation/local_mcp.hpp"
#include "automation/session.hpp"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <array>
#include <cerrno>
#include <csignal>
#include <cstring>
#include <fcntl.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>
namespace sketchy {
namespace {
[[noreturn]] void fail(const char *message) { throw InspectionError("TRANSPORT_ERROR", message); }
struct Descriptor {
    int value{-1};
    ~Descriptor() {
        if (value >= 0)
            ::close(value);
    }
};
struct Nonblocking {
    int fd, flags;
    explicit Nonblocking(int fd) : fd(fd), flags(fcntl(fd, F_GETFL)) {
        if (flags < 0 || fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0)
            fail("Cannot configure bridge descriptor");
    }
    ~Nonblocking() { fcntl(fd, F_SETFL, flags); }
};
void sendBytes(int fd, QByteArray &bytes, bool socket) {
    const auto n = socket ? ::send(fd, bytes.constData(), bytes.size(), MSG_NOSIGNAL)
                          : ::write(fd, bytes.constData(), bytes.size());
    if (n > 0)
        bytes.remove(0, n);
    else if (n < 0 && errno != EINTR && errno != EAGAIN && errno != EWOULDBLOCK)
        fail("Cannot write MCP bridge stream");
}
} // namespace
QString checkedMcpSocketPath(const QString &path, bool existing) {
    if (path.isEmpty())
        fail("MCP socket path is required");
    const QFileInfo info(path);
    const auto parent = info.absoluteDir().canonicalPath();
    struct stat directory{}, endpoint{};
    if (parent.isEmpty() || ::stat(QFile::encodeName(parent).constData(), &directory) != 0 ||
        !S_ISDIR(directory.st_mode) || directory.st_uid != geteuid() ||
        (directory.st_mode & 0777) != 0700)
        fail("MCP socket parent must exist, be user-owned and have mode 0700");
    const auto result = QDir(parent).filePath(info.fileName());
    const auto encoded = QFile::encodeName(result);
    if (encoded.size() >= qsizetype(sizeof(sockaddr_un::sun_path)))
        fail("MCP socket path is too long");
    const bool found = ::lstat(encoded.constData(), &endpoint) == 0;
    if (!found && errno != ENOENT)
        fail("Cannot inspect MCP endpoint");
    if (existing && (!found || !S_ISSOCK(endpoint.st_mode) || endpoint.st_uid != geteuid() ||
                     (endpoint.st_mode & 0077) != 0))
        fail("MCP endpoint must be a private user-owned filesystem socket");
    if (!existing && found)
        fail("MCP endpoint already exists; refusing to replace it");
    return result;
}
void checkMcpPeer(int descriptor) {
    ucred credentials{};
    socklen_t size = sizeof(credentials);
    if (getsockopt(descriptor, SOL_SOCKET, SO_PEERCRED, &credentials, &size) != 0 ||
        size != sizeof(credentials) || credentials.uid != geteuid())
        fail("MCP peer must belong to the current user");
}
int bridgeMcpStdio(const QString &path) {
    const auto endpoint = QFile::encodeName(checkedMcpSocketPath(path, true));
    Descriptor socket{::socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC | SOCK_NONBLOCK, 0)};
    if (socket.value < 0)
        fail("Cannot create MCP bridge socket");
    sockaddr_un address{};
    address.sun_family = AF_UNIX;
    std::memcpy(address.sun_path, endpoint.constData(), endpoint.size());
    if (::connect(socket.value, reinterpret_cast<sockaddr *>(&address), sizeof(address)) != 0) {
        if (errno != EINPROGRESS)
            fail("Cannot connect to native MCP endpoint");
        pollfd pending{socket.value, POLLOUT, 0};
        int status{};
        socklen_t size = sizeof(status);
        if (::poll(&pending, 1, 5000) <= 0 ||
            getsockopt(socket.value, SOL_SOCKET, SO_ERROR, &status, &size) != 0 || status)
            fail("Native MCP connection timed out or failed");
    }
    checkMcpPeer(socket.value);
    Nonblocking input(STDIN_FILENO), output(STDOUT_FILENO);
    // Writes to a closed stdout should become a transport error rather than SIGPIPE.
    const auto previous = std::signal(SIGPIPE, SIG_IGN);
    struct RestoreSignal {
        decltype(previous) handler;
        ~RestoreSignal() { std::signal(SIGPIPE, handler); }
    } restore{previous};
    QByteArray incoming, outgoing, toSocket;
    bool eof{}, closing{}, disconnected{};
    std::array<char, 16384> buffer{};
    for (;;) {
        if (toSocket.isEmpty() && !closing) {
            const auto newline = incoming.indexOf('\n');
            if (newline >= 0) {
                toSocket = incoming.first(newline + 1);
                incoming.remove(0, newline + 1);
            } else if (eof && !incoming.isEmpty()) {
                toSocket = incoming + '\n';
                incoming.clear();
            } else if (eof) {
                toSocket = "{\"jsonrpc\":\"2.0\",\"method\":\"notifications/sketchyup/close\"}\n";
                closing = true;
            }
        }
        if (disconnected && outgoing.isEmpty())
            return closing && toSocket.isEmpty() ? 0 : 1;
        std::array<pollfd, 3> fds{
            {{STDIN_FILENO,
              short(!eof && incoming.size() < sessionWireBytes + 1 && toSocket.isEmpty() ? POLLIN
                                                                                         : 0),
              0},
             {STDOUT_FILENO, short(!outgoing.isEmpty() ? POLLOUT : 0), 0},
             {socket.value,
              short(!disconnected ? ((outgoing.size() < sessionResponseBytes ? POLLIN : 0) |
                                     (!toSocket.isEmpty() ? POLLOUT : 0))
                                  : 0),
              0}}};
        // Disabled descriptors must not spin on POLLHUP while stdout is backpressured.
        for (auto &fd : fds)
            if (!fd.events)
                fd.fd = -1;
        if (::poll(fds.data(), fds.size(), -1) < 0) {
            if (errno == EINTR)
                continue;
            fail("MCP bridge poll failed");
        }
        if (fds[0].revents & (POLLIN | POLLHUP)) {
            const auto n =
                ::read(STDIN_FILENO, buffer.data(),
                       std::min<qsizetype>(buffer.size(), sessionWireBytes + 1 - incoming.size()));
            if (n > 0)
                incoming.append(buffer.data(), n);
            else if (n == 0)
                eof = true;
            else if (errno != EINTR && errno != EAGAIN)
                fail("Cannot read MCP bridge input");
            if (incoming.size() > sessionWireBytes && incoming.indexOf('\n') < 0)
                fail("MCP input line exceeds 66 KiB");
        }
        if (fds[1].revents & POLLOUT)
            sendBytes(STDOUT_FILENO, outgoing, false);
        if (fds[1].revents & (POLLERR | POLLHUP | POLLNVAL))
            fail("MCP output closed");
        if (outgoing.size() < sessionResponseBytes &&
            (fds[2].revents & (POLLIN | POLLHUP | POLLERR))) {
            const auto n = ::recv(
                socket.value, buffer.data(),
                std::min<qsizetype>(buffer.size(), sessionResponseBytes - outgoing.size()), 0);
            if (n > 0)
                outgoing.append(buffer.data(), n);
            else if (n == 0)
                disconnected = true;
            else if (errno != EINTR && errno != EAGAIN)
                fail("Cannot read native MCP socket");
        }
        if (!disconnected && (fds[2].revents & POLLOUT))
            sendBytes(socket.value, toSocket, true);
        if (fds[0].revents & (POLLERR | POLLNVAL))
            fail("MCP input closed unexpectedly");
    }
}
} // namespace sketchy
