#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <iostream>
#include <fstream>
#include <string>
#include <thread>
#include <vector>
#include <map>
#include <mutex>
#include <chrono>
using namespace std;

/*
 * CLIENTE UDP - Protocolo de transferencia de archivos segmentados
 *
 * PAQUETE DE DATOS enviado al servidor:
 *   [1B  'F']
 *   [1B  dest_nick_size]   [dest_nick_size  B dest_nick]
 *   [3B  filename_size]    [filename_size   B filename]
 *   [5B  orig_nick_size]   [orig_nick_size  B orig_nick]
 *   [12B seq_num]          (ej. "000000000666")
 *   [22B payload_size]     (tamaño del payload de este segmento)
 *   [4B  total_segs]
 *   [4B  current_seg]      (base-1)
 *   [payload bytes]
 *   [5B  checksum]         (ASCII decimal, 5 dígitos)
 *
 * ACK/NACK recibido del servidor:
 *   "ACK <sq#> <total> <current>\n"
 *   "NACK <sq#> <total> <current>\n"
 *
 * Paquete de archivo recibido (desde otro cliente vía servidor):
 *   Mismo formato que el de envío.
 */

#define SERVER_IP   "172.31.215.158"
#define SERVER_PORT 45001
#define BUF_SIZE    65536
#define PAYLOAD_MAX 492
#define SEQ_NUM     666
#define TIMEOUT_SEC 2
#define MAX_RETRIES 5

int sockfd;
struct sockaddr_in servaddr;
socklen_t servlen;
string my_nick;

// Fragmentos recibidos: seq_num -> {seg_idx -> data}
mutex recv_mutex;
map<long, map<int, string>> recv_fragments;  // seq -> (seg# -> payload)
map<long, int> recv_total;                   // seq -> total_segs
map<long, string> recv_filename;             // seq -> filename
map<long, string> recv_origin;               // seq -> origin nick

uint16_t calc_checksum(const char* data, int len) {
    uint32_t sum = 0;
    for(int i = 0; i < len; i++) sum += (unsigned char)data[i];
    return (uint16_t)(sum % 65536);
}

void print_first_100(const char* label, const char* data, int len) {
    int to_print = len < 100 ? len : 100;
    printf("[%s] Primeros %d bytes (hex):\n  ", label, to_print);
    for(int i = 0; i < to_print; i++) {
        printf("%02X ", (unsigned char)data[i]);
        if((i+1) % 16 == 0) printf("\n  ");
    }
    printf("\n");
    fflush(stdout);
}

// Intenta reconstruir y guardar si ya llegaron todos los fragmentos
void try_reassemble(long seq) {
    recv_mutex.lock();
    if(recv_total.count(seq) == 0 || recv_fragments.count(seq) == 0) {
        recv_mutex.unlock(); return;
    }
    int total = recv_total[seq];
    if((int)recv_fragments[seq].size() < total) {
        recv_mutex.unlock(); return;
    }
    // Reconstruir
    string full_data;
    for(int i = 1; i <= total; i++) {
        if(recv_fragments[seq].count(i) == 0) {
            recv_mutex.unlock(); return; // falta un segmento
        }
        full_data += recv_fragments[seq][i];
    }
    string fname    = recv_filename[seq];
    string orig     = recv_origin[seq];
    recv_fragments.erase(seq);
    recv_total.erase(seq);
    recv_filename.erase(seq);
    recv_origin.erase(seq);
    recv_mutex.unlock();

    // Imprimir primeros 100 bytes del archivo reconstruido
    print_first_100("CLIENTE RECEPTOR - archivo reconstruido", full_data.c_str(), full_data.size());

    // Guardar archivo
    string saved = "recv_" + orig + "_" + fname;
    ofstream ofs(saved, ios::binary);
    if(ofs.is_open()) {
        ofs.write(full_data.c_str(), full_data.size());
        ofs.close();
        printf("[CLIENTE] Archivo reconstruido y guardado: %s (%zu bytes)\n",
               saved.c_str(), full_data.size());
    } else {
        printf("[CLIENTE] ERROR: no se pudo guardar %s\n", saved.c_str());
    }
    fflush(stdout);
}

// Thread receptor: escucha ACK/NACK/file-packets del servidor
// Para cada ACK/NACK, los almacenamos en un mapa protegido
mutex ack_mutex;
map<int, string> pending_ack;   // current_seg -> "ACK" | "NACK"

void recv_thread() {
    char buf[BUF_SIZE];
    while(true) {
        struct sockaddr_in from;
        socklen_t fromlen = sizeof(from);
        int n = recvfrom(sockfd, buf, BUF_SIZE - 1, 0,
                         (struct sockaddr*)&from, &fromlen);
        if(n <= 0) continue;
        buf[n] = '\0';

        // ACK / NACK (texto)
        if(n >= 3 && (strncmp(buf, "ACK", 3) == 0 || strncmp(buf, "NACK", 4) == 0)) {
            // Parse: "ACK <seq> <total> <current>\n"
            char type[8]; long seq; int total, current;
            if(sscanf(buf, "%7s %ld %d %d", type, &seq, &total, &current) == 4) {
                ack_mutex.lock();
                pending_ack[current] = string(type);
                ack_mutex.unlock();
                printf("[CLIENTE] %s recibido: seq=%ld seg=%d/%d\n",
                       type, seq, current, total);
                fflush(stdout);
            }
            continue;
        }

        // OK / ERR (login/logout)
        if(n >= 2 && strncmp(buf, "OK", 2) == 0) {
            printf("[CLIENTE] Servidor: OK\n");
            fflush(stdout);
            continue;
        }
        if(n >= 3 && strncmp(buf, "ERR", 3) == 0) {
            printf("[CLIENTE] Servidor ERROR: %s\n", buf);
            fflush(stdout);
            continue;
        }

        // Paquete de archivo entrante (op == 'F')
        if(buf[0] == 'F') {
            int offset = 1;

            // dest nick
            int dest_size = (unsigned char)buf[offset++];
            string dest(buf + offset, dest_size); offset += dest_size;

            // filename
            char tmp[8];
            memcpy(tmp, buf + offset, 3); tmp[3] = '\0';
            int fn_size = atoi(tmp); offset += 3;
            string filename(buf + offset, fn_size); offset += fn_size;

            // origin nick
            memcpy(tmp, buf + offset, 5); tmp[5] = '\0';
            int orig_size = atoi(tmp); offset += 5;
            string origin(buf + offset, orig_size); offset += orig_size;

            // seq_num
            char seq_str[13]; memcpy(seq_str, buf + offset, 12); seq_str[12] = '\0';
            long seq_num = atol(seq_str); offset += 12;

            // payload size
            char sz_str[23]; memcpy(sz_str, buf + offset, 22); sz_str[22] = '\0';
            int payload_size = atoi(sz_str); offset += 22;

            // total_segs
            char tot_str[5]; memcpy(tot_str, buf + offset, 4); tot_str[4] = '\0';
            int total_segs = atoi(tot_str); offset += 4;

            // current_seg
            char cur_str[5]; memcpy(cur_str, buf + offset, 4); cur_str[4] = '\0';
            int current_seg = atoi(cur_str); offset += 4;

            // payload
            int payload_left = n - offset - 5;
            if(payload_left < 0) continue;
            string payload(buf + offset, payload_left); offset += payload_left;

            // checksum
            char csum_str[6]; memcpy(csum_str, buf + offset, 5); csum_str[5] = '\0';
            uint16_t recv_csum = (uint16_t)atoi(csum_str);
            uint16_t calc_csum = calc_checksum(payload.c_str(), payload_left);

            printf("[CLIENTE RECEPTOR] Segmento %d/%d de [%s] -> archivo '%s' | %d bytes | seq=%ld\n",
                   current_seg, total_segs, origin.c_str(), filename.c_str(),
                   payload_left, seq_num);

            // Imprimir primeros 100 bytes del segmento recibido
            print_first_100("CLIENTE RECEPTOR segmento", payload.c_str(), payload_left);

            if(calc_csum != recv_csum) {
                printf("[CLIENTE RECEPTOR] Checksum ERROR en seg %d (recv=%u calc=%u) - descartando\n",
                       current_seg, recv_csum, calc_csum);
                fflush(stdout);
                continue;
            }

            // Guardar fragmento
            recv_mutex.lock();
            recv_fragments[seq_num][current_seg] = payload;
            recv_total[seq_num]    = total_segs;
            recv_filename[seq_num] = filename;
            recv_origin[seq_num]   = origin;
            recv_mutex.unlock();

            // Intentar reconstruir
            try_reassemble(seq_num);
        }
    }
}

// Construye y envía un paquete de segmento de archivo
// Retorna true si recibe ACK, false si NACK o timeout
bool send_segment(const string& dest, const string& filename,
                  long seq_num, int total_segs, int current_seg,
                  const char* payload, int payload_size) {

    int dest_size   = dest.size();
    int fn_size     = filename.size();
    int orig_size   = my_nick.size();

    int pkt_size = 1
                 + 1 + dest_size
                 + 3 + fn_size
                 + 5 + orig_size
                 + 12   // seq_num
                 + 22   // payload_size
                 + 4    // total_segs
                 + 4    // current_seg
                 + payload_size
                 + 5;   // checksum

    vector<char> pkt(pkt_size);
    int offset = 0;

    pkt[offset++] = 'F';

    // dest nick
    pkt[offset++] = (char)dest_size;
    memcpy(&pkt[offset], dest.c_str(), dest_size); offset += dest_size;

    // filename (3 dígitos)
    snprintf(&pkt[offset], 4, "%03d", fn_size); offset += 3;
    memcpy(&pkt[offset], filename.c_str(), fn_size); offset += fn_size;

    // origin nick (5 dígitos)
    snprintf(&pkt[offset], 6, "%05d", orig_size); offset += 5;
    memcpy(&pkt[offset], my_nick.c_str(), orig_size); offset += orig_size;

    // seq_num (12 dígitos)
    snprintf(&pkt[offset], 13, "%012ld", seq_num); offset += 12;

    // payload size (22 dígitos)
    snprintf(&pkt[offset], 23, "%022d", payload_size); offset += 22;

    // total_segs (4 dígitos)
    snprintf(&pkt[offset], 5, "%04d", total_segs); offset += 4;

    // current_seg (4 dígitos)
    snprintf(&pkt[offset], 5, "%04d", current_seg); offset += 4;

    // payload
    memcpy(&pkt[offset], payload, payload_size); offset += payload_size;

    // checksum (5 dígitos)
    uint16_t csum = calc_checksum(payload, payload_size);
    snprintf(&pkt[offset], 6, "%05u", csum); offset += 5;

    // Imprimir primeros 100 bytes del segmento que vamos a enviar
    print_first_100("CLIENTE EMISOR segmento", payload, payload_size);

    // Limpiar ACK pendiente para este segmento
    ack_mutex.lock();
    pending_ack.erase(current_seg);
    ack_mutex.unlock();

    for(int attempt = 0; attempt < MAX_RETRIES; attempt++) {
        sendto(sockfd, pkt.data(), offset, 0,
               (const struct sockaddr*)&servaddr, servlen);

        // Esperar ACK con timeout
        auto start = chrono::steady_clock::now();
        while(true) {
            auto now = chrono::steady_clock::now();
            double elapsed = chrono::duration<double>(now - start).count();
            if(elapsed >= TIMEOUT_SEC) break;

            ack_mutex.lock();
            if(pending_ack.count(current_seg)) {
                string result = pending_ack[current_seg];
                pending_ack.erase(current_seg);
                ack_mutex.unlock();
                if(result == "ACK") return true;
                else {
                    printf("[CLIENTE] NACK recibido para seg %d, reintentando...\n", current_seg);
                    fflush(stdout);
                    break; // retry
                }
            }
            ack_mutex.unlock();
            usleep(10000); // 10ms
        }
        printf("[CLIENTE] Timeout seg %d (intento %d/%d)\n", current_seg, attempt+1, MAX_RETRIES);
        fflush(stdout);
    }
    return false;
}

void send_file(const string& filepath, const string& dest) {
    // Leer archivo
    ifstream ifs(filepath, ios::binary | ios::ate);
    if(!ifs.is_open()) {
        printf("[CLIENTE] ERROR: no se puede abrir '%s'\n", filepath.c_str());
        return;
    }
    int file_len = (int)ifs.tellg();
    ifs.seekg(0, ios::beg);
    vector<char> file_data(file_len);
    ifs.read(file_data.data(), file_len);
    ifs.close();

    // Nombre del archivo
    string filename = filepath;
    size_t slash = filepath.find_last_of("/\\");
    if(slash != string::npos) filename = filepath.substr(slash + 1);

    // Calcular segmentos
    int total_segs = (file_len + PAYLOAD_MAX - 1) / PAYLOAD_MAX;
    if(total_segs == 0) total_segs = 1;

    printf("[CLIENTE] Enviando '%s' a [%s] | %d bytes | %d segmentos | seq=%d\n",
           filename.c_str(), dest.c_str(), file_len, total_segs, SEQ_NUM);

    // Imprimir primeros 100 bytes del archivo completo a enviar
    print_first_100("CLIENTE EMISOR - archivo completo", file_data.data(), file_len);
    fflush(stdout);

    for(int seg = 1; seg <= total_segs; seg++) {
        int offset   = (seg - 1) * PAYLOAD_MAX;
        int seg_size = (offset + PAYLOAD_MAX <= file_len) ? PAYLOAD_MAX : (file_len - offset);

        bool ok = send_segment(dest, filename, SEQ_NUM, total_segs, seg,
                               file_data.data() + offset, seg_size);
        if(!ok) {
            printf("[CLIENTE] FALLO en segmento %d/%d — transferencia abortada\n", seg, total_segs);
            return;
        }
        printf("[CLIENTE] Segmento %d/%d enviado OK\n", seg, total_segs);
        fflush(stdout);
    }
    printf("[CLIENTE] Transferencia completada: '%s' -> [%s]\n", filename.c_str(), dest.c_str());
    fflush(stdout);
}

void do_login() {
    int nick_size = my_nick.size();
    int pkt_size  = 1 + 1 + nick_size;
    vector<char> pkt(pkt_size);
    pkt[0] = 'L';
    pkt[1] = (char)nick_size;
    memcpy(&pkt[2], my_nick.c_str(), nick_size);
    sendto(sockfd, pkt.data(), pkt_size, 0,
           (const struct sockaddr*)&servaddr, servlen);
}

void do_logout() {
    int nick_size = my_nick.size();
    int pkt_size  = 1 + 1 + nick_size;
    vector<char> pkt(pkt_size);
    pkt[0] = 'O';
    pkt[1] = (char)nick_size;
    memcpy(&pkt[2], my_nick.c_str(), nick_size);
    sendto(sockfd, pkt.data(), pkt_size, 0,
           (const struct sockaddr*)&servaddr, servlen);
}

int main(void) {
    sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if(sockfd < 0) { perror("socket"); exit(1); }

    memset(&servaddr, 0, sizeof(servaddr));
    servaddr.sin_family = AF_INET;
    servaddr.sin_port   = htons(SERVER_PORT);
    if(inet_pton(AF_INET, SERVER_IP, &servaddr.sin_addr) <= 0) {
        perror("inet_pton"); exit(1);
    }
    servlen = sizeof(servaddr);

    // Bind a puerto local para recibir
    struct sockaddr_in localaddr;
    memset(&localaddr, 0, sizeof(localaddr));
    localaddr.sin_family      = AF_INET;
    localaddr.sin_addr.s_addr = INADDR_ANY;
    localaddr.sin_port        = 0; // OS asigna puerto libre
    if(bind(sockfd, (const struct sockaddr*)&localaddr, sizeof(localaddr)) < 0) {
        perror("bind"); exit(1);
    }

    cout << "Nickname: ";
    getline(cin, my_nick);

    // Iniciar thread receptor
    thread t(recv_thread);
    t.detach();

    // Login
    do_login();
    usleep(200000); // esperar OK

    string input;
    do {
        cout << "\nAccion (F: enviar archivo, O: logout): ";
        getline(cin, input);
        if(input.empty()) continue;
        char op = input[0];

        if(op == 'F') {
            string filepath, dest;
            cout << "Ruta del archivo (imagen <= 10KB): ";
            getline(cin, filepath);
            cout << "Destinatario: ";
            getline(cin, dest);
            send_file(filepath, dest);

        } else if(op == 'O') {
            do_logout();
            usleep(100000);
            break;
        } else {
            cout << "Accion no reconocida (F o O)" << endl;
        }
    } while(true);

    close(sockfd);
    return 0;
}
