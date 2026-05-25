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

mutex recv_mutex;
map<long, map<int, string>> recv_fragments;
map<long, int>    recv_total;
map<long, string> recv_filename;
map<long, string> recv_origin;

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

//Leer campo con prefijo N bytes
string leer_campo(const char* buf, int buf_len, int& offset, int prefix_size) {
    if(offset + prefix_size > buf_len) return "";
    char tmp[16]; memcpy(tmp, buf + offset, prefix_size); tmp[prefix_size] = '\0';
    int len = atoi(tmp);
    offset += prefix_size;
    if(offset + len > buf_len) return "";
    string val(buf + offset, len);
    offset += len;
    return val;
}

void try_reassemble(long seq) {
    recv_mutex.lock();
    if(recv_total.count(seq) == 0 || recv_fragments.count(seq) == 0) {
        recv_mutex.unlock(); return;
    }
    int total = recv_total[seq];
    if((int)recv_fragments[seq].size() < total) {
        recv_mutex.unlock(); return;
    }
    string full_data;
    for(int i = 1; i <= total; i++) {
        if(recv_fragments[seq].count(i) == 0) {
            recv_mutex.unlock(); return;
        }
        full_data += recv_fragments[seq][i];
    }
    string fname = recv_filename[seq];
    string orig  = recv_origin[seq];
    recv_fragments.erase(seq);
    recv_total.erase(seq);
    recv_filename.erase(seq);
    recv_origin.erase(seq);
    recv_mutex.unlock();

    print_first_100("CLIENTE RECEPTOR - archivo reconstruido", full_data.c_str(), full_data.size());

    string saved = "recv_" + orig + "_" + fname;
    ofstream ofs(saved, ios::binary);
    if(ofs.is_open()) {
        ofs.write(full_data.c_str(), full_data.size());
        ofs.close();
        printf("[CLIENTE] Archivo guardado: %s (%zu bytes)\n", saved.c_str(), full_data.size());
    } else {
        printf("[CLIENTE] ERROR: no se pudo guardar %s\n", saved.c_str());
    }
    fflush(stdout);
}

mutex ack_mutex;
map<int, string> pending_ack;


void recv_thread() {
    char buf[BUF_SIZE];
    while(true) {
        struct sockaddr_in from;
        socklen_t fromlen = sizeof(from);
        int n = recvfrom(sockfd, buf, BUF_SIZE - 1, 0,
                         (struct sockaddr*)&from, &fromlen);
        if(n <= 0) continue;
        buf[n] = '\0';

        //ACK / NACK
        if(n >= 3 && (strncmp(buf, "ACK", 3) == 0 || strncmp(buf, "NACK", 4) == 0)) {
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

        //OK
        if(n >= 2 && strncmp(buf, "OK", 2) == 0) {
            printf("[CLIENTE] Servidor: OK\n");
            fflush(stdout);
            continue;
        }

        //ERR
        if(n >= 3 && strncmp(buf, "ERR", 3) == 0) {
            printf("[CLIENTE] Servidor ERROR: %s\n", buf);
            fflush(stdout);
            continue;
        }

        //BROADCAST recibido S→C
        if(buf[0] == 'b') {
            int offset = 1;
            string sender = leer_campo(buf, n, offset, 3);
            string msg    = leer_campo(buf, n, offset, 7);
            if(sender.empty() || msg.empty()) continue;
            printf("\n[Broadcast][%s]: %s\n", sender.c_str(), msg.c_str());
            fflush(stdout);
            continue;
        }

        //UNICAST recibido S→C
        if(buf[0] == 'u') {
            int offset = 1;
            string sender = leer_campo(buf, n, offset, 7);
            string msg    = leer_campo(buf, n, offset, 5);
            if(sender.empty() || msg.empty()) continue;
            printf("\n[Privado][%s]: %s\n", sender.c_str(), msg.c_str());
            fflush(stdout);
            continue;
        }

        //LIST recibido S→C
        if(buf[0] == 't') {
            int offset = 1;
            string json = leer_campo(buf, n, offset, 5);
            if(json.empty()) continue;

            printf("\n--- Clientes conectados ---\n");
            size_t pos = json.find('[');
            size_t end = json.find(']');
            if(pos != string::npos && end != string::npos) {
                string lista = json.substr(pos + 1, end - pos - 1);
                size_t start = 0;
                while(start < lista.size()) {
                    size_t comma = lista.find(',', start);
                    string entry = lista.substr(start,
                        comma == string::npos ? string::npos : comma - start);
                    // quitar comillas
                    string clean;
                    for(char c : entry) if(c != '"') clean += c;
                    if(!clean.empty()) printf("  - %s\n", clean.c_str());
                    if(comma == string::npos) break;
                    start = comma + 1;
                }
            }
            printf("---------------------------\n");
            fflush(stdout);
            continue;
        }

        //FILE recibido (paquete de segmento)
        if(buf[0] == 'F') {
            int offset = 1;

            int dest_size = (unsigned char)buf[offset++];
            string dest(buf + offset, dest_size); offset += dest_size;

            char tmp[8];
            memcpy(tmp, buf + offset, 3); tmp[3] = '\0';
            int fn_size = atoi(tmp); offset += 3;
            string filename(buf + offset, fn_size); offset += fn_size;

            memcpy(tmp, buf + offset, 5); tmp[5] = '\0';
            int orig_size = atoi(tmp); offset += 5;
            string origin(buf + offset, orig_size); offset += orig_size;

            char seq_str[13]; memcpy(seq_str, buf + offset, 12); seq_str[12] = '\0';
            long seq_num = atol(seq_str); offset += 12;

            char sz_str[23]; memcpy(sz_str, buf + offset, 22); sz_str[22] = '\0';
            int payload_size = atoi(sz_str); offset += 22;

            char tot_str[5]; memcpy(tot_str, buf + offset, 4); tot_str[4] = '\0';
            int total_segs = atoi(tot_str); offset += 4;

            char cur_str[5]; memcpy(cur_str, buf + offset, 4); cur_str[4] = '\0';
            int current_seg = atoi(cur_str); offset += 4;

            int payload_left = n - offset - 5;
            if(payload_left < 0) continue;
            string payload(buf + offset, payload_left); offset += payload_left;

            char csum_str[6]; memcpy(csum_str, buf + offset, 5); csum_str[5] = '\0';
            uint16_t recv_csum = (uint16_t)atoi(csum_str);
            uint16_t calc_csum = calc_checksum(payload.c_str(), payload_left);

            printf("[CLIENTE RECEPTOR] Segmento %d/%d de [%s] -> '%s' | %d bytes\n",
                   current_seg, total_segs, origin.c_str(), filename.c_str(), payload_left);
            print_first_100("CLIENTE RECEPTOR segmento", payload.c_str(), payload_left);

            if(calc_csum != recv_csum) {
                printf("[CLIENTE RECEPTOR] Checksum ERROR seg %d\n", current_seg);
                fflush(stdout);
                continue;
            }

            recv_mutex.lock();
            recv_fragments[seq_num][current_seg] = payload;
            recv_total[seq_num]    = total_segs;
            recv_filename[seq_num] = filename;
            recv_origin[seq_num]   = origin;
            recv_mutex.unlock();

            try_reassemble(seq_num);
        }
    }
}

bool send_segment(const string& dest, const string& filename,
                  long seq_num, int total_segs, int current_seg,
                  const char* payload, int payload_size) {
    int dest_size = dest.size();
    int fn_size   = filename.size();
    int orig_size = my_nick.size();

    int pkt_size = 1 + 1 + dest_size + 3 + fn_size + 5 + orig_size
                 + 12 + 22 + 4 + 4 + payload_size + 5;

    vector<char> pkt(pkt_size);
    int offset = 0;

    pkt[offset++] = 'F';
    pkt[offset++] = (char)dest_size;
    memcpy(&pkt[offset], dest.c_str(), dest_size); offset += dest_size;
    snprintf(&pkt[offset], 4, "%03d", fn_size); offset += 3;
    memcpy(&pkt[offset], filename.c_str(), fn_size); offset += fn_size;
    snprintf(&pkt[offset], 6, "%05d", orig_size); offset += 5;
    memcpy(&pkt[offset], my_nick.c_str(), orig_size); offset += orig_size;
    snprintf(&pkt[offset], 13, "%012ld", seq_num); offset += 12;
    snprintf(&pkt[offset], 23, "%022d", payload_size); offset += 22;
    snprintf(&pkt[offset], 5, "%04d", total_segs); offset += 4;
    snprintf(&pkt[offset], 5, "%04d", current_seg); offset += 4;
    memcpy(&pkt[offset], payload, payload_size); offset += payload_size;
    uint16_t csum = calc_checksum(payload, payload_size);
    snprintf(&pkt[offset], 6, "%05u", csum); offset += 5;

    print_first_100("CLIENTE EMISOR segmento", payload, payload_size);

    ack_mutex.lock();
    pending_ack.erase(current_seg);
    ack_mutex.unlock();

    for(int attempt = 0; attempt < MAX_RETRIES; attempt++) {
        sendto(sockfd, pkt.data(), offset, 0,
               (const struct sockaddr*)&servaddr, servlen);

        auto start = chrono::steady_clock::now();
        while(true) {
            double elapsed = chrono::duration<double>(
                chrono::steady_clock::now() - start).count();
            if(elapsed >= TIMEOUT_SEC) break;

            ack_mutex.lock();
            if(pending_ack.count(current_seg)) {
                string result = pending_ack[current_seg];
                pending_ack.erase(current_seg);
                ack_mutex.unlock();
                if(result == "ACK") return true;
                printf("[CLIENTE] NACK seg %d, reintentando...\n", current_seg);
                fflush(stdout);
                break;
            }
            ack_mutex.unlock();
            usleep(10000);
        }
        printf("[CLIENTE] Timeout seg %d (intento %d/%d)\n",
               current_seg, attempt + 1, MAX_RETRIES);
        fflush(stdout);
    }
    return false;
}

void send_file(const string& filepath, const string& dest) {
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

    string filename = filepath;
    size_t slash = filepath.find_last_of("/\\");
    if(slash != string::npos) filename = filepath.substr(slash + 1);

    int total_segs = (file_len + PAYLOAD_MAX - 1) / PAYLOAD_MAX;
    if(total_segs == 0) total_segs = 1;

    printf("[CLIENTE] Enviando '%s' a [%s] | %d bytes | %d segmentos\n",
           filename.c_str(), dest.c_str(), file_len, total_segs);
    print_first_100("CLIENTE EMISOR - archivo completo", file_data.data(), file_len);
    fflush(stdout);

    for(int seg = 1; seg <= total_segs; seg++) {
        int off      = (seg - 1) * PAYLOAD_MAX;
        int seg_size = (off + PAYLOAD_MAX <= file_len) ? PAYLOAD_MAX : (file_len - off);
        bool ok = send_segment(dest, filename, SEQ_NUM, total_segs, seg,
                               file_data.data() + off, seg_size);
        if(!ok) {
            printf("[CLIENTE] FALLO en segmento %d/%d\n", seg, total_segs);
            return;
        }
        printf("[CLIENTE] Segmento %d/%d OK\n", seg, total_segs);
        fflush(stdout);
    }
    printf("[CLIENTE] Transferencia completada: '%s' -> [%s]\n",
           filename.c_str(), dest.c_str());
    fflush(stdout);
}

void do_login() {
    int nick_size = my_nick.size();
    vector<char> pkt(1 + 1 + nick_size);
    pkt[0] = 'L'; pkt[1] = (char)nick_size;
    memcpy(&pkt[2], my_nick.c_str(), nick_size);
    sendto(sockfd, pkt.data(), pkt.size(), 0,
           (const struct sockaddr*)&servaddr, servlen);
}

void do_logout() {
    int nick_size = my_nick.size();
    vector<char> pkt(1 + 1 + nick_size);
    pkt[0] = 'O'; pkt[1] = (char)nick_size;
    memcpy(&pkt[2], my_nick.c_str(), nick_size);
    sendto(sockfd, pkt.data(), pkt.size(), 0,
           (const struct sockaddr*)&servaddr, servlen);
}

//Broadcast C-S
void do_broadcast(const string& msg) {
    vector<char> pkt;
    pkt.push_back('B');
    char tmp[16];
    snprintf(tmp, 8, "%07d", (int)msg.size());
    for(int i = 0; i < 7; i++) pkt.push_back(tmp[i]);
    for(char c : msg) pkt.push_back(c);
    sendto(sockfd, pkt.data(), pkt.size(), 0,
           (const struct sockaddr*)&servaddr, servlen);
}

// Unicast C-S
void do_unicast(const string& msg, const string& dest) {
    vector<char> pkt;
    pkt.push_back('U');
    char tmp[16];
    snprintf(tmp, 6, "%05d", (int)msg.size());
    for(int i = 0; i < 5; i++) pkt.push_back(tmp[i]);
    for(char c : msg) pkt.push_back(c);
    snprintf(tmp, 8, "%07d", (int)dest.size());
    for(int i = 0; i < 7; i++) pkt.push_back(tmp[i]);
    for(char c : dest) pkt.push_back(c);
    sendto(sockfd, pkt.data(), pkt.size(), 0,
           (const struct sockaddr*)&servaddr, servlen);
}

//List C-S
void do_list() {
    char pkt = 'T';
    sendto(sockfd, &pkt, 1, 0,
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

    struct sockaddr_in localaddr;
    memset(&localaddr, 0, sizeof(localaddr));
    localaddr.sin_family      = AF_INET;
    localaddr.sin_addr.s_addr = INADDR_ANY;
    localaddr.sin_port        = 0;
    if(bind(sockfd, (const struct sockaddr*)&localaddr, sizeof(localaddr)) < 0) {
        perror("bind"); exit(1);
    }

    cout << "Nickname: ";
    getline(cin, my_nick);

    thread t(recv_thread);
    t.detach();

    do_login();
    usleep(200000);

    string input;
    do {
        cout << "\nAccion (B: broadcast, U: unicast, T: lista, F: archivo, O: logout): ";
        getline(cin, input);
        if(input.empty()) continue;
        char op = input[0];

        if(op == 'B') {
            string msg;
            cout << "Mensaje: ";
            getline(cin, msg);
            do_broadcast(msg);

        } else if(op == 'U') {
            string dest, msg;
            cout << "Destinatario: ";
            getline(cin, dest);
            cout << "Mensaje: ";
            getline(cin, msg);
            do_unicast(msg, dest);

        } else if(op == 'T') {
            do_list();

        } else if(op == 'F') {
            string filepath, dest;
            cout << "Ruta del archivo: ";
            getline(cin, filepath);
            cout << "Destinatario: ";
            getline(cin, dest);
            send_file(filepath, dest);

        } else if(op == 'O') {
            do_logout();
            usleep(100000);
            break;

        } else {
            cout << "Accion no reconocida" << endl;
        }
    } while(true);

    close(sockfd);
    return 0;
}