#include "udp_socket.h"
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <sys/select.h>
#include <cstring>
#include <stdexcept>
#include <string>

namespace rdt {

UDPSocket::UDPSocket(int bind_port) {
    fd_ = ::socket(AF_INET, SOCK_DGRAM, 0);
    if (fd_ < 0)
        throw std::runtime_error("No se pudo crear el socket UDP");

    // Permite reutilizar el puerto rápidamente tras reiniciar el proceso
    int opt = 1;
    ::setsockopt(fd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    sockaddr_in addr{};
    std::memset(&addr, 0, sizeof(addr));
    addr.sin_family      = AF_INET;
    addr.sin_port        = htons(static_cast<uint16_t>(bind_port));
    addr.sin_addr.s_addr = INADDR_ANY;

    if (::bind(fd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        ::close(fd_);
        throw std::runtime_error(
            "No se pudo hacer bind en puerto " + std::to_string(bind_port));
    }
}

UDPSocket::~UDPSocket() {
    if (fd_ >= 0) ::close(fd_);
}

void UDPSocket::send_to(const void* buf, int len,
                        const std::string& dest_ip, int dest_port) {
    sockaddr_in dst{};
    std::memset(&dst, 0, sizeof(dst));
    dst.sin_family = AF_INET;
    dst.sin_port   = htons(static_cast<uint16_t>(dest_port));
    ::inet_pton(AF_INET, dest_ip.c_str(), &dst.sin_addr);

    ::sendto(fd_, buf, static_cast<size_t>(len), 0,
             reinterpret_cast<sockaddr*>(&dst), sizeof(dst));
}

int UDPSocket::recv_from(void* buf, int max_len,
                         int timeout_ms, Addr& src) {
    // Espera con select() hasta timeout_ms milisegundos
    fd_set fds;
    FD_ZERO(&fds);
    FD_SET(fd_, &fds);

    timeval tv;
    tv.tv_sec  = timeout_ms / 1000;
    tv.tv_usec = (timeout_ms % 1000) * 1000;

    int ready = ::select(fd_ + 1, &fds, nullptr, nullptr, &tv);
    if (ready <= 0) return -1;  // timeout o error

    sockaddr_in from{};
    std::memset(&from, 0, sizeof(from));
    socklen_t from_len = sizeof(from);

    int n = ::recvfrom(fd_, buf, static_cast<size_t>(max_len), 0,
                       reinterpret_cast<sockaddr*>(&from), &from_len);
    if (n < 0) return -1;

    char ip_buf[INET_ADDRSTRLEN];
    ::inet_ntop(AF_INET, &from.sin_addr, ip_buf, sizeof(ip_buf));
    src.ip   = ip_buf;
    src.port = ntohs(from.sin_port);
    return n;
}

} // namespace rdt
