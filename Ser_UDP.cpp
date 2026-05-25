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

// Calcula checksum
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

        //LOGIN
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

        //FILE TRANSFER
        } else if(op == 'F') {
            int offset = 1;

            // dest nick
            if(n < offset + 1) continue;
            int dest_size = (unsigned char)buf[offset++];
            if(n < offset + dest_size) continue;
            string dest(buf + offset, dest_size);
            offset += dest_size;

            // filename (3B)
            if(n < offset + 3) continue;
            char tmp[8]; memcpy(tmp, buf + offset, 3); tmp[3] = '\0';
            int fn_size = atoi(tmp);
            offset += 3;
            if(n < offset + fn_size) continue;
            string filename(buf + offset, fn_size);
            offset += fn_size;

            // origen nick (5B)
            if(n < offset + 5) continue;
            memcpy(tmp, buf + offset, 5); tmp[5] = '\0';
            int orig_size = atoi(tmp);
            offset += 5;
            if(n < offset + orig_size) continue;
            string origin(buf + offset, orig_size);
            offset += orig_size;

            // seq_num (12B)
            if(n < offset + 12) continue;
            char seq_str[13]; memcpy(seq_str, buf + offset, 12); seq_str[12] = '\0';
            long seq_num = atol(seq_str);
            offset += 12;

            // payload size (22B)
            if(n < offset + 22) continue;
            char sz_str[23]; memcpy(sz_str, buf + offset, 22); sz_str[22] = '\0';
            int payload_size = atoi(sz_str);
            offset += 22;

            // total_segs (4B)
            if(n < offset + 4) continue;
            char tot_str[5]; memcpy(tot_str, buf + offset, 4); tot_str[4] = '\0';
            int total_segs = atoi(tot_str);
            offset += 4;

            // current_seg (4B)
            if(n < offset + 4) continue;
            char cur_str[5]; memcpy(cur_str, buf + offset, 4); cur_str[4] = '\0';
            int current_seg = atoi(cur_str);
            offset += 4;

            // payload
            int payload_left = n - offset - 5; // last 5 = checksum
            if(payload_left < 0) continue;
            const char* payload = buf + offset;
            offset += payload_left;

            // checksum (5B)
            if(n < offset + 5) continue;
            char csum_str[6]; memcpy(csum_str, buf + offset, 5); csum_str[5] = '\0';
            uint16_t recv_csum = (uint16_t)atoi(csum_str);

            uint16_t calc_csum = calc_checksum(payload, payload_left);

            printf("[SERVER] Segmento recibido: '%s' de [%s] -> [%s] | seg %d/%d | seq=%ld | payload=%d bytes\n",
                   filename.c_str(), origin.c_str(), dest.c_str(),
                   current_seg, total_segs, seq_num, payload_left);

            // Imprimir primeros 100 bytes del payload en ser
            print_first_100("SERVER payload", payload, payload_left);

            // Verificar checksum
            if(calc_csum != recv_csum) {
            
                char nack[128];
                int nlen = snprintf(nack, sizeof(nack),
                                    "NACK %ld %d %d\n", seq_num, total_segs, current_seg);
                clients_mutex.lock();
                auto it_src = clients.find(origin);
                if(it_src != clients.end()) {
                    send_msg(sockfd, &it_src->second.addr, it_src->second.addr_len, nack, nlen);
                }
                clients_mutex.unlock();
                printf("[SERVER] NACK enviado (checksum error: recv=%u calc=%u)\n",
                       recv_csum, calc_csum);
                fflush(stdout);
                continue;
            }

            // Reenviar al destino
            clients_mutex.lock();
            auto it_dst = clients.find(dest);
            if(it_dst == clients.end()) {
                clients_mutex.unlock();
                // Informar error al origen
                char err[128];
                int elen = snprintf(err, sizeof(err), "ERR Destino '%s' no encontrado\n", dest.c_str());
                clients_mutex.lock();
                auto it_src2 = clients.find(origin);
                if(it_src2 != clients.end())
                    send_msg(sockfd, &it_src2->second.addr, it_src2->second.addr_len, err, elen);
                clients_mutex.unlock();
                printf("[SERVER] Destino [%s] no encontrado\n", dest.c_str());
                fflush(stdout);
                continue;
            }
            // Reenviar paquete original (incluyendo cabecera) al destino
            send_msg(sockfd, &it_dst->second.addr, it_dst->second.addr_len, buf, n);

            // ACK al origen
            auto it_src = clients.find(origin);
            if(it_src != clients.end()) {
                char ack[128];
                int alen = snprintf(ack, sizeof(ack),
                                    "ACK %ld %d %d\n", seq_num, total_segs, current_seg);
                send_msg(sockfd, &it_src->second.addr, it_src->second.addr_len, ack, alen);
                printf("[SERVER] ACK enviado: seq=%ld seg=%d/%d\n",
                       seq_num, current_seg, total_segs);
            }
            clients_mutex.unlock();
            fflush(stdout);

        } else {
            // Desconocido
            char err[] = "ERR Operacion desconocida\n";
            send_msg(sockfd, &cliaddr, clilen, err, strlen(err));
        }
    }

    close(sockfd);
    return 0;
}
