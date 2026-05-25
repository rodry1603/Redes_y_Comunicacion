#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <iostream>
#include <string>
#include <map>
#include <mutex>
#include <thread>
#include <vector>
#include <sstream>
using namespace std;

#define PORT 45001
#define BUF_SIZE 65536
#define PAYLOAD_MAX 492

struct ClientInfo {
    struct sockaddr_in addr;
    socklen_t addr_len;
};

map<string, ClientInfo> clients;
mutex clients_mutex;

uint16_t calc_checksum(const char* data, int len) {
    uint32_t sum = 0;
    for(int i = 0; i < len; i++) sum += (unsigned char)data[i];
    return (uint16_t)(sum % 65536);
}

void print_first_100(const char* label, const char* data, int len) {
    int to_print = len < 100 ? len : 100;
    printf("[%s] Primeros %d bytes (hex): ", label, to_print);
    for(int i = 0; i < to_print; i++) printf("%02X ", (unsigned char)data[i]);
    printf("\n");
    fflush(stdout);
}

void send_msg(int sockfd, const struct sockaddr_in* addr, socklen_t addrlen,
              const char* msg, int len) {
    sendto(sockfd, msg, len, 0, (const struct sockaddr*)addr, addrlen);
}

//Buscar nick por dirección 
string find_nick_by_addr(const struct sockaddr_in& addr) {
    for(auto& kv : clients) {
        if(kv.second.addr.sin_addr.s_addr == addr.sin_addr.s_addr &&
           kv.second.addr.sin_port        == addr.sin_port)
            return kv.first;
    }
    return "";
}

//Leer campo con prefijo de N bytes

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

//Agregar campo con prefijo de N bytes a vector
void agregar_campo(vector<char>& pkt, const string& data, int prefix_size) {
    char tmp[16];
    snprintf(tmp, prefix_size + 1, "%0*d", prefix_size, (int)data.size());
    for(int i = 0; i < prefix_size; i++) pkt.push_back(tmp[i]);
    for(char c : data) pkt.push_back(c);
}

int main(void) {
    int sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if(sockfd < 0) { perror("socket"); exit(1); }

    int opt = 1;
    setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in servaddr;
    memset(&servaddr, 0, sizeof(servaddr));
    servaddr.sin_family      = AF_INET;
    servaddr.sin_addr.s_addr = INADDR_ANY;
    servaddr.sin_port        = htons(PORT);

    if(bind(sockfd, (const struct sockaddr*)&servaddr, sizeof(servaddr)) < 0) {
        perror("bind"); exit(1);
    }

    printf("[SERVIDOR UDP] Escuchando en puerto %d...\n", PORT);
    fflush(stdout);

    char buf[BUF_SIZE];

    for(;;) {
        struct sockaddr_in cliaddr;
        socklen_t clilen = sizeof(cliaddr);

        int n = recvfrom(sockfd, buf, BUF_SIZE, 0,
                         (struct sockaddr*)&cliaddr, &clilen);
        if(n <= 0) continue;

        char op = buf[0];

        // LOGIN
        if(op == 'L') {
            if(n < 2) continue;
            int nick_size = (unsigned char)buf[1];
            if(n < 2 + nick_size) continue;
            string nick(buf + 2, nick_size);

            clients_mutex.lock();
            bool exists = clients.count(nick) > 0;
            if(!exists) {
                ClientInfo ci;
                ci.addr     = cliaddr;
                ci.addr_len = clilen;
                clients[nick] = ci;
            }
            clients_mutex.unlock();

            if(exists) {
                const char* err = "ERR Nickname ya en uso\n";
                send_msg(sockfd, &cliaddr, clilen, err, strlen(err));
                printf("[SERVER] Login rechazado: nick '%s' ya existe\n", nick.c_str());
            } else {
                const char* ok = "OK\n";
                send_msg(sockfd, &cliaddr, clilen, ok, strlen(ok));
                printf("[SERVER] Cliente registrado: [%s]\n", nick.c_str());
            }
            fflush(stdout);

        //LOGOUT
        } else if(op == 'O') {
            if(n < 2) continue;
            int nick_size = (unsigned char)buf[1];
            if(n < 2 + nick_size) continue;
            string nick(buf + 2, nick_size);

            clients_mutex.lock();
            clients.erase(nick);
            clients_mutex.unlock();
            printf("[SERVER] Cliente desconectado: [%s]\n", nick.c_str());
            fflush(stdout);

        //BROADCAST
        } else if(op == 'B') {
            int offset = 1;
            string msg = leer_campo(buf, n, offset, 7);
            if(msg.empty()) continue;

            clients_mutex.lock();
            string sender = find_nick_by_addr(cliaddr);
            if(sender.empty()) {
                clients_mutex.unlock();
                const char* err = "ERR No autenticado\n";
                send_msg(sockfd, &cliaddr, clilen, err, strlen(err));
                continue;
            }

            //[b][3B nick][nick][7B msg][msg]
            vector<char> pkt;
            pkt.push_back('b');
            agregar_campo(pkt, sender, 3);
            agregar_campo(pkt, msg, 7);

            for(auto& kv : clients) {
                if(kv.first != sender)
                    send_msg(sockfd, &kv.second.addr, kv.second.addr_len,
                             pkt.data(), pkt.size());
            }
            clients_mutex.unlock();

            const char* ok = "OK\n";
            send_msg(sockfd, &cliaddr, clilen, ok, strlen(ok));
            printf("[SERVER] Broadcast de [%s]: %s\n", sender.c_str(), msg.c_str());
            fflush(stdout);

        //UNICAST C-S
        } else if(op == 'U') {
            int offset = 1;
            string msg  = leer_campo(buf, n, offset, 5);
            string dest = leer_campo(buf, n, offset, 7);
            if(msg.empty() || dest.empty()) continue;

            clients_mutex.lock();
            string sender = find_nick_by_addr(cliaddr);
            if(sender.empty()) {
                clients_mutex.unlock();
                const char* err = "ERR No autenticado\n";
                send_msg(sockfd, &cliaddr, clilen, err, strlen(err));
                continue;
            }

            if(clients.find(dest) == clients.end()) {
                clients_mutex.unlock();
                string err = "ERR Usuario " + dest + " no encontrado\n";
                send_msg(sockfd, &cliaddr, clilen, err.c_str(), err.size());
                printf("[SERVER] Unicast fallido: [%s] no existe\n", dest.c_str());
                fflush(stdout);
                continue;
            }

            // S-C
            vector<char> pkt;
            pkt.push_back('u');
            agregar_campo(pkt, sender, 7);
            agregar_campo(pkt, msg, 5);

            ClientInfo& dst = clients[dest];
            send_msg(sockfd, &dst.addr, dst.addr_len, pkt.data(), pkt.size());
            clients_mutex.unlock();

            const char* ok = "OK\n";
            send_msg(sockfd, &cliaddr, clilen, ok, strlen(ok));
            printf("[SERVER] Unicast de [%s] -> [%s]: %s\n",
                   sender.c_str(), dest.c_str(), msg.c_str());
            fflush(stdout);

        //LIST C-S
        } else if(op == 'T') {
            string json = "{\"clients\":[";
            clients_mutex.lock();
            bool first = true;
            for(auto& kv : clients) {
                if(!first) json += ",";
                json += "\"" + kv.first + "\"";
                first = false;
            }
            clients_mutex.unlock();
            json += "]}";

            // S-C
            vector<char> pkt;
            pkt.push_back('t');
            agregar_campo(pkt, json, 5);
            send_msg(sockfd, &cliaddr, clilen, pkt.data(), pkt.size());
            printf("[SERVER] List enviado a cliente\n");
            fflush(stdout);

        //FILE TRANSFER
        } else if(op == 'F') {
            int offset = 1;

            // dest nick (1B size)
            if(n < offset + 1) continue;
            int dest_size = (unsigned char)buf[offset++];
            if(n < offset + dest_size) continue;
            string dest(buf + offset, dest_size);
            offset += dest_size;

            // filename (3B size)
            if(n < offset + 3) continue;
            char tmp[8]; memcpy(tmp, buf + offset, 3); tmp[3] = '\0';
            int fn_size = atoi(tmp); offset += 3;
            if(n < offset + fn_size) continue;
            string filename(buf + offset, fn_size);
            offset += fn_size;

            // origen nick (5B size)
            if(n < offset + 5) continue;
            memcpy(tmp, buf + offset, 5); tmp[5] = '\0';
            int orig_size = atoi(tmp); offset += 5;
            if(n < offset + orig_size) continue;
            string origin(buf + offset, orig_size);
            offset += orig_size;

            // seq_num (12B)
            if(n < offset + 12) continue;
            char seq_str[13]; memcpy(seq_str, buf + offset, 12); seq_str[12] = '\0';
            long seq_num = atol(seq_str); offset += 12;

            // payload size (22B)
            if(n < offset + 22) continue;
            char sz_str[23]; memcpy(sz_str, buf + offset, 22); sz_str[22] = '\0';
            int payload_size = atoi(sz_str); offset += 22;

            // total_segs (4B)
            if(n < offset + 4) continue;
            char tot_str[5]; memcpy(tot_str, buf + offset, 4); tot_str[4] = '\0';
            int total_segs = atoi(tot_str); offset += 4;

            // current_seg (4B)
            if(n < offset + 4) continue;
            char cur_str[5]; memcpy(cur_str, buf + offset, 4); cur_str[4] = '\0';
            int current_seg = atoi(cur_str); offset += 4;

            // payload
            int payload_left = n - offset - 5;
            if(payload_left < 0) continue;
            const char* payload = buf + offset;
            offset += payload_left;

            // checksum (5B)
            if(n < offset + 5) continue;
            char csum_str[6]; memcpy(csum_str, buf + offset, 5); csum_str[5] = '\0';
            uint16_t recv_csum = (uint16_t)atoi(csum_str);
            uint16_t calc_csum = calc_checksum(payload, payload_left);

            printf("[SERVER] Segmento: '%s' de [%s] -> [%s] | seg %d/%d | seq=%ld\n",
                   filename.c_str(), origin.c_str(), dest.c_str(),
                   current_seg, total_segs, seq_num);
            print_first_100("SERVER payload", payload, payload_left);

            if(calc_csum != recv_csum) {
                char nack[128];
                int nlen = snprintf(nack, sizeof(nack),
                                    "NACK %ld %d %d\n", seq_num, total_segs, current_seg);
                clients_mutex.lock();
                auto it_src = clients.find(origin);
                if(it_src != clients.end())
                    send_msg(sockfd, &it_src->second.addr, it_src->second.addr_len, nack, nlen);
                clients_mutex.unlock();
                printf("[SERVER] NACK (checksum error: recv=%u calc=%u)\n", recv_csum, calc_csum);
                fflush(stdout);
                continue;
            }

            clients_mutex.lock();
            auto it_dst = clients.find(dest);
            if(it_dst == clients.end()) {
                auto it_src2 = clients.find(origin);
                if(it_src2 != clients.end()) {
                    string err = "ERR Destino '" + dest + "' no encontrado\n";
                    send_msg(sockfd, &it_src2->second.addr, it_src2->second.addr_len,
                             err.c_str(), err.size());
                }
                clients_mutex.unlock();
                printf("[SERVER] Destino [%s] no encontrado\n", dest.c_str());
                fflush(stdout);
                continue;
            }

            send_msg(sockfd, &it_dst->second.addr, it_dst->second.addr_len, buf, n);

            auto it_src = clients.find(origin);
            if(it_src != clients.end()) {
                char ack[128];
                int alen = snprintf(ack, sizeof(ack),
                                    "ACK %ld %d %d\n", seq_num, total_segs, current_seg);
                send_msg(sockfd, &it_src->second.addr, it_src->second.addr_len, ack, alen);
                printf("[SERVER] ACK: seq=%ld seg=%d/%d\n", seq_num, current_seg, total_segs);
            }
            clients_mutex.unlock();
            fflush(stdout);

        } else {
            char err[] = "ERR Operacion desconocida\n";
            send_msg(sockfd, &cliaddr, clilen, err, strlen(err));
        }
    }

    close(sockfd);
    return 0;
}