#pragma once
#include "protocol.h"
#include "udp_socket.h"
#include <vector>
#include <string>
#include <functional>

namespace rdt {

using LogFn = std::function<void(const std::string&)>;

/* ═══════════════════════════════════════════════════════════════════════════
 *  RDTNode – comunicación confiable sobre UDP (stop-and-wait)
 *
 *  EMISOR  : send()   → fragmenta → para cada paquete:
 *                         imprime WRITE:>>>…<<<
 *                         envía por UDP
 *                         espera ACK 'a'  (500 ms timeout)
 *                         si NACK 'N' o timeout → retransmite
 *
 *  RECEPTOR: recv()   → recibe paquete
 *                         imprime READ :>>>…<<<
 *                         verifica checksum
 *                         si OK  → envía ACK 'a'   (se imprime WRITE:>>>…<<<)
 *                         si MAL → envía NACK 'N'  (se imprime WRITE:>>>…<<<)
 *                         al recibir FLAG_END → reensambla y retorna mensaje
 *
 *  La función print_packet genera el display estilo terminal de la imagen.
 * ═══════════════════════════════════════════════════════════════════════════ */
class RDTNode {
public:
    RDTNode(int listen_port, uint16_t my_node_id = 0, LogFn log_fn = nullptr);

    /* Mensaje completo (con type = TYPE_MATRIX / TYPE_DATA / TYPE_MATRIX_SLAVE).
     * Retorna número de paquetes enviados. Lanza std::runtime_error si falla. */
    int send(const std::string& dest_ip, int dest_port,
             uint16_t dest_id, uint8_t type,
             const std::vector<uint8_t>& data);

    /* Recibe mensaje completo. Retorna {type, data} o {0, empty} en timeout. */
    struct Message {
        uint8_t              type = 0;
        std::vector<uint8_t> data;
        bool empty() const { return data.empty(); }
    };
    Message recv(int timeout_ms = 15000);

private:
    void send_one(const Packet& pkt, const std::string& ip, int port);
    void send_response(uint8_t resp_type, uint32_t seq,
                       const std::string& ip, int port);

    /* Imprime el datagrama en el formato:
     *   WRITE:>>>[checksum][node_id][flags][seq][type][data_size][DATA...]<<<
     *   READ :>>>[checksum][node_id][flags][seq][type][data_size][DATA...]<<<
     * Cada campo numérico = decimal con ceros a la izquierda.
     * Bytes no imprimibles = '#'. Líneas de 80 chars. */
    void print_packet(const Packet& pkt, const std::string& direction) const;

    void log(const std::string& msg) const { if (log_fn_) log_fn_(msg); }

    UDPSocket sock_;
    uint16_t  my_id_;
    LogFn     log_fn_;
};

} // namespace rdt
