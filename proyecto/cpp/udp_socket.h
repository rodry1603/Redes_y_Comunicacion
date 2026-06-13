#pragma once
#include <string>
#include <stdexcept>
#include <cstdint>

namespace rdt {

// Dirección IP + puerto de origen de un datagrama recibido
struct Addr {
    std::string ip;
    int         port = 0;
};

/* Wrapper mínimo de socket UDP.
 * Cada nodo (maestro o esclavo) crea uno y lo vincula a su puerto fijo. */
class UDPSocket {
public:
    // bind_port: puerto en el que este nodo escucha
    explicit UDPSocket(int bind_port);
    ~UDPSocket();

    // No copiable
    UDPSocket(const UDPSocket&)            = delete;
    UDPSocket& operator=(const UDPSocket&) = delete;

    /* Envía `len` bytes de `buf` a dest_ip:dest_port */
    void send_to(const void* buf, int len,
                 const std::string& dest_ip, int dest_port);

    /* Recibe hasta max_len bytes en buf.
     * Rellena src con la dirección del remitente.
     * timeout_ms ≥ 0: espera máxima; 0 = no-bloqueante.
     * Retorna bytes recibidos, o -1 en timeout/error. */
    int recv_from(void* buf, int max_len,
                  int timeout_ms, Addr& src);

private:
    int fd_;
};

} // namespace rdt