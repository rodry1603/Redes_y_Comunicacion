#pragma once
#include <cstdint>
#include <cstring>

namespace rdt {

/* ═══════════════════════════════════════════════════════════════════════════
 *  Datagrama de 500 bytes  (protocolo propio sobre UDP)
 *
 *  ┌──────────┬─────────┬───────┬─────┬──────┬───────────┬──────────────────┐
 *  │ checksum │ node_id │ flags │ seq │ type │ data_size │  data + padding  │
 *  │   2 B    │   2 B   │  2 B  │ 4 B │  1 B │    4 B    │      485 B       │
 *  └──────────┴─────────┴───────┴─────┴──────┴───────────┴──────────────────┘
 *
 *  checksum  : Σ bytes[2..499] % 100
 *  node_id   : ID del nodo DESTINO
 *  flags     : 0x0001 = inicio  │  0x0000 = cuerpo  │  0x0003 = final
 *  seq       : número de secuencia (0, 1, 2 …)
 *  type      :  MAESTRO→ESCLAVO  M = matriz de pesos  D = dataset
 *               ESCLAVO→MAESTRO  m = pesos devueltos
 *               CONTROL          a = ACK (minúscula)  N = NACK
 *  data_size : tamaño total del mensaje (todos los fragmentos juntos)
 * ═══════════════════════════════════════════════════════════════════════════ */

// ── Tamaños ────────────────────────────────────────────────────────────────
constexpr int PACKET_SIZE  = 500;
constexpr int HEADER_SIZE  = 15;                      // 2+2+2+4+1+4
constexpr int MAX_DATA     = PACKET_SIZE - HEADER_SIZE; // 485 bytes payload

// ── Flags ──────────────────────────────────────────────────────────────────
constexpr uint16_t FLAG_START = 0x0001;
constexpr uint16_t FLAG_BODY  = 0x0000;
constexpr uint16_t FLAG_END   = 0x0003;   // binario 11

// ── Tipos de mensaje ────────────────────────────────────────────────────────
//  Maestro → Esclavo:
constexpr uint8_t TYPE_MATRIX       = 'M';  // pesos que maestro envía al esclavo
constexpr uint8_t TYPE_DATA         = 'D';  // porción del dataset
//  Esclavo → Maestro / control:
constexpr uint8_t TYPE_MATRIX_SLAVE = 'm';  // pesos que esclavo devuelve al maestro
constexpr uint8_t TYPE_ACK          = 'a';  // confirmación de recepción (minúscula)
constexpr uint8_t TYPE_NACK         = 'N';  // rechazo — datagrama corrupto

// ── Timeout / reintentos ───────────────────────────────────────────────────
constexpr int TIMEOUT_MS   = 500;
constexpr int MAX_RETRIES  = 20;

// ── Estructuras (packed: sin padding del compilador) ──────────────────────
#pragma pack(push, 1)
struct Header {
    uint16_t checksum;   // 2 B
    uint16_t node_id;    // 2 B – ID destino
    uint16_t flags;      // 2 B – 0x0001 | 0x0000 | 0x0003
    uint32_t seq;        // 4 B – nro. de secuencia
    uint8_t  type;       // 1 B – M | D | m | a | N
    uint32_t data_size;  // 4 B – tamaño total del mensaje
};                       // = 15 B total

struct Packet {
    Header  hdr;
    uint8_t data[MAX_DATA];  // 485 B (payload + padding 0x00)
};
#pragma pack(pop)

static_assert(sizeof(Header) == 15,          "Header debe ser 15 bytes");
static_assert(sizeof(Packet) == PACKET_SIZE, "Packet debe ser 500 bytes");

// ── Checksum: Σ bytes[2..499] % 100 ──────────────────────────────────────
inline uint16_t compute_checksum(const Packet& p) {
    const uint8_t* raw = reinterpret_cast<const uint8_t*>(&p);
    uint32_t sum = 0;
    for (int i = 2; i < PACKET_SIZE; ++i) sum += raw[i];
    return static_cast<uint16_t>(sum % 100);
}
inline bool verify_checksum(const Packet& p) {
    return p.hdr.checksum == compute_checksum(p);
}
inline void seal_packet(Packet& p) {
    p.hdr.checksum = compute_checksum(p);
}

} // namespace rdt
