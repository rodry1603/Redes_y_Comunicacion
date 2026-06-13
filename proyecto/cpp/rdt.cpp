#include "rdt.h"
#include <cstring>
#include <cstdio>
#include <map>
#include <vector>
#include <string>
#include <sstream>
#include <stdexcept>
#include <algorithm>
#include <chrono>

namespace rdt {

// ── Constructor ────────────────────────────────────────────────────────────
RDTNode::RDTNode(int listen_port, uint16_t my_node_id, LogFn log_fn)
    : sock_(listen_port), my_id_(my_node_id), log_fn_(std::move(log_fn)) {}

// ══════════════════════════════════════════════════════════════════════════
//  DISPLAY DE DATAGRAMAS  (estilo terminal de la imagen)
//
//  Formato por campo (orden del protocolo):
//    checksum : 4 dígitos decimales
//    node_id  : 4 dígitos decimales
//    flags    : 4 dígitos decimales  (0000 | 0001 | 0003)
//    seq      : 8 dígitos decimales
//    type     : 1 carácter           (M | D | m | a | N)
//    data_size: 8 dígitos decimales
//    data     : 485 bytes → printable = char, no-printable = '#'
//
//  Total display = 4+4+4+8+1+8+485 = 514 chars → ~7 líneas de 80 cols
//  Primera línea: "WRITE:>>>" / "READ :>>>" + contenido
//  Última línea : contenido + "<<<"
// ══════════════════════════════════════════════════════════════════════════
void RDTNode::print_packet(const Packet& pkt,
                           const std::string& direction) const {
    if (!log_fn_) return;

    char buf[16];
    std::string s;
    s.reserve(520);

    // ── Campos del header ─────────────────────────────────────────────────
    std::snprintf(buf, sizeof(buf), "%04u",
                  static_cast<unsigned>(pkt.hdr.checksum)); s += buf;  // checksum
    std::snprintf(buf, sizeof(buf), "%04u",
                  static_cast<unsigned>(pkt.hdr.node_id));  s += buf;  // node_id
    std::snprintf(buf, sizeof(buf), "%04u",
                  static_cast<unsigned>(pkt.hdr.flags));    s += buf;  // flags
    std::snprintf(buf, sizeof(buf), "%08u",
                  static_cast<unsigned>(pkt.hdr.seq));      s += buf;  // seq
    s += static_cast<char>(pkt.hdr.type);                              // type
    std::snprintf(buf, sizeof(buf), "%08u",
                  static_cast<unsigned>(pkt.hdr.data_size)); s += buf; // data_size

    // ── Bytes de datos: printable → carácter, resto → '#' ─────────────────
    for (int i = 0; i < MAX_DATA; ++i) {
        uint8_t b = pkt.data[i];
        s += (b >= 32 && b <= 126) ? static_cast<char>(b) : '#';
    }

    // ── Partir en líneas de 80 chars ──────────────────────────────────────
    const int    LINE_W = 80;
    const std::string pfx = direction + ":>>>";  // "WRITE:>>>" o "READ :>>>"

    std::vector<std::string> lines;
    bool   first = true;
    size_t pos   = 0;
    while (pos < s.size()) {
        size_t avail = first
            ? static_cast<size_t>(LINE_W - static_cast<int>(pfx.size()))
            : static_cast<size_t>(LINE_W);
        std::string chunk = s.substr(pos, std::min(avail, s.size() - pos));
        lines.push_back(first ? pfx + chunk : chunk);
        pos  += avail;
        first = false;
    }
    if (lines.empty()) lines.push_back(pfx);
    lines.back() += "<<<";

    for (const auto& l : lines) log_fn_(l);
}

// ══════════════════════════════════════════════════════════════════════════
//  ENVIAR ACK / NACK
// ══════════════════════════════════════════════════════════════════════════
void RDTNode::send_response(uint8_t resp_type, uint32_t seq,
                            const std::string& ip, int port) {
    Packet ack{};
    std::memset(&ack, 0, sizeof(ack));
    ack.hdr.node_id   = my_id_;
    ack.hdr.flags     = FLAG_END;
    ack.hdr.seq       = seq;
    ack.hdr.type      = resp_type;  // 'a' o 'N'
    ack.hdr.data_size = 0;
    seal_packet(ack);

    sock_.send_to(&ack, PACKET_SIZE, ip, port);
    print_packet(ack, "WRITE");  // ← display del datagrama enviado
}

// ══════════════════════════════════════════════════════════════════════════
//  STOP-AND-WAIT: enviar UN paquete y esperar ACK
// ══════════════════════════════════════════════════════════════════════════
void RDTNode::send_one(const Packet& pkt,
                       const std::string& dest_ip, int dest_port) {
    for (int attempt = 1; attempt <= MAX_RETRIES; ++attempt) {
        // ── Enviar datagrama ──────────────────────────────────────────────
        print_packet(pkt, "WRITE");          // display ANTES de enviar
        sock_.send_to(&pkt, PACKET_SIZE, dest_ip, dest_port);

        // ── Esperar ACK/NACK (500 ms) ─────────────────────────────────────
        Packet resp{};
        Addr   src;
        int n = sock_.recv_from(&resp, PACKET_SIZE, TIMEOUT_MS, src);

        if (n < 0) {
            log("[RDT:" + std::to_string(my_id_) +
                "] Timeout seq=" + std::to_string(pkt.hdr.seq) +
                " (intento " + std::to_string(attempt) + ") — retransmitiendo");
            continue;
        }

        print_packet(resp, "READ ");         // display del ACK/NACK recibido

        if (!verify_checksum(resp)) {
            log("[RDT:" + std::to_string(my_id_) +
                "] Checksum inválido en respuesta — retransmitiendo seq=" +
                std::to_string(pkt.hdr.seq));
            continue;
        }

        // Ignorar paquetes de datos que puedan llegar en esta ventana
        if (resp.hdr.type != TYPE_ACK && resp.hdr.type != TYPE_NACK) {
            log("[RDT:" + std::to_string(my_id_) +
                "] Paquete no-ACK ignorado, reintento seq=" +
                std::to_string(pkt.hdr.seq));
            continue;
        }

        if (resp.hdr.type == TYPE_NACK) {
            log("[RDT:" + std::to_string(my_id_) + "] NACK seq=" +
                std::to_string(resp.hdr.seq) + " — retransmitiendo");
            continue;
        }

        if (resp.hdr.type == TYPE_ACK && resp.hdr.seq == pkt.hdr.seq) {
            return;  // ✓ ACK correcto
        }

        log("[RDT:" + std::to_string(my_id_) + "] ACK stale seq=" +
            std::to_string(resp.hdr.seq) +
            " esperado=" + std::to_string(pkt.hdr.seq) + " — reintentando");
    }

    throw std::runtime_error(
        "[RDT] Max reintentos superados para seq=" +
        std::to_string(pkt.hdr.seq));
}

// ══════════════════════════════════════════════════════════════════════════
//  ENVIAR MENSAJE COMPLETO (con fragmentación)
// ══════════════════════════════════════════════════════════════════════════
int RDTNode::send(const std::string& dest_ip, int dest_port,
                  uint16_t dest_id, uint8_t type,
                  const std::vector<uint8_t>& data) {
    uint32_t total    = static_cast<uint32_t>(data.size());
    uint32_t num_pkts = (total + MAX_DATA - 1) / MAX_DATA;
    if (total == 0) num_pkts = 1;

    {
        std::ostringstream os;
        os << "[RDT:" << my_id_ << "] Enviando tipo='"
           << static_cast<char>(type) << "' "
           << total << " bytes → " << num_pkts << " paquetes"
           << " → " << dest_ip << ":" << dest_port;
        log(os.str());
    }

    for (uint32_t seq = 0; seq < num_pkts; ++seq) {
        Packet pkt{};
        std::memset(&pkt, 0, sizeof(pkt));

        pkt.hdr.node_id   = dest_id;
        pkt.hdr.seq       = seq;
        pkt.hdr.type      = type;
        pkt.hdr.data_size = total;

        if      (num_pkts == 1)          pkt.hdr.flags = FLAG_END;
        else if (seq == 0)               pkt.hdr.flags = FLAG_START;
        else if (seq == num_pkts - 1)    pkt.hdr.flags = FLAG_END;
        else                             pkt.hdr.flags = FLAG_BODY;

        uint32_t offset = seq * static_cast<uint32_t>(MAX_DATA);
        uint32_t chunk  = std::min(static_cast<uint32_t>(MAX_DATA),
                                   total - offset);
        if (total > 0)
            std::memcpy(pkt.data, data.data() + offset, chunk);

        seal_packet(pkt);
        send_one(pkt, dest_ip, dest_port);  // stop-and-wait
    }

    log("[RDT:" + std::to_string(my_id_) + "] Envío completo (" +
        std::to_string(num_pkts) + " paquetes)");
    return static_cast<int>(num_pkts);
}

// ══════════════════════════════════════════════════════════════════════════
//  RECIBIR MENSAJE COMPLETO (con reensamblaje)
// ══════════════════════════════════════════════════════════════════════════
RDTNode::Message RDTNode::recv(int timeout_ms) {
    std::map<uint32_t, std::vector<uint8_t>> fragments;
    uint32_t expected_size = 0;
    uint8_t  msg_type      = 0;
    bool     got_end       = false;
    uint32_t end_seq       = 0;

    auto deadline = std::chrono::steady_clock::now() +
                    std::chrono::milliseconds(timeout_ms);

    log("[RDT:" + std::to_string(my_id_) + "] Esperando mensaje...");

    while (!got_end) {
        auto now = std::chrono::steady_clock::now();
        int remaining = static_cast<int>(
            std::chrono::duration_cast<std::chrono::milliseconds>(
                deadline - now).count());
        if (remaining <= 0) {
            log("[RDT:" + std::to_string(my_id_) + "] Timeout global");
            return {};
        }

        int wait = std::min(remaining, TIMEOUT_MS);
        Packet pkt{};
        Addr   src;
        int n = sock_.recv_from(&pkt, PACKET_SIZE, wait, src);
        if (n < 0) continue;

        print_packet(pkt, "READ ");          // display de TODO lo recibido

        // Ignorar ACK/NACK (son respuestas al flujo de envío propio)
        if (pkt.hdr.type == TYPE_ACK || pkt.hdr.type == TYPE_NACK)
            continue;

        // ── Verificar checksum ────────────────────────────────────────────
        if (!verify_checksum(pkt)) {
            log("[RDT:" + std::to_string(my_id_) +
                "] Checksum inválido seq=" + std::to_string(pkt.hdr.seq) +
                " → NACK");
            send_response(TYPE_NACK, pkt.hdr.seq, src.ip, src.port);
            continue;
        }

        uint32_t seq = pkt.hdr.seq;
        if (expected_size == 0 && pkt.hdr.data_size > 0)
            expected_size = pkt.hdr.data_size;
        if (msg_type == 0) msg_type = pkt.hdr.type;

        // ── Duplicado: re-enviar ACK ──────────────────────────────────────
        if (fragments.count(seq)) {
            send_response(TYPE_ACK, seq, src.ip, src.port);
            log("[RDT:" + std::to_string(my_id_) +
                "] Duplicado seq=" + std::to_string(seq) + " — re-ACK");
            continue;
        }

        // ── Almacenar fragmento ───────────────────────────────────────────
        uint32_t offset = seq * static_cast<uint32_t>(MAX_DATA);
        uint32_t chunk  = (expected_size > 0)
            ? std::min(static_cast<uint32_t>(MAX_DATA), expected_size - offset)
            : static_cast<uint32_t>(MAX_DATA);

        fragments[seq] = std::vector<uint8_t>(pkt.data, pkt.data + chunk);
        send_response(TYPE_ACK, seq, src.ip, src.port);  // ACK → se imprime

        {
            std::ostringstream os;
            os << "[RDT:" << my_id_ << "] Almacenado seq=" << seq
               << " chunk=" << chunk << "B"
               << "  type='" << static_cast<char>(msg_type) << "'";
            log(os.str());
        }

        if (pkt.hdr.flags == FLAG_END) {
            end_seq = seq;
            got_end = true;
        }
    }

    // ── Reensamblar ───────────────────────────────────────────────────────
    Message msg;
    msg.type = msg_type;
    msg.data.reserve(expected_size);
    for (uint32_t i = 0; i <= end_seq; ++i) {
        if (!fragments.count(i)) {
            log("[RDT:" + std::to_string(my_id_) +
                "] FALTA fragmento seq=" + std::to_string(i));
            return {};
        }
        const auto& f = fragments[i];
        msg.data.insert(msg.data.end(), f.begin(), f.end());
    }

    log("[RDT:" + std::to_string(my_id_) + "] Recepción completa tipo='" +
        std::string(1, static_cast<char>(msg.type)) +
        "' " + std::to_string(msg.data.size()) + " bytes / " +
        std::to_string(end_seq + 1) + " paquetes");
    return msg;
}

} // namespace rdt
