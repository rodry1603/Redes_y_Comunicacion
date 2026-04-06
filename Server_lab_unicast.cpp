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
#include <thread>
#include <map>
using namespace std;

std::map<string, int> clients;

void threadReadSocket(int client_socket) {
    char buffer[256];
    int n;

    do {
        memset(buffer, 0, sizeof(buffer));

        // --- Leer destinatario ---
        n = read(client_socket, buffer, 3);
        if(n <= 0) break;
        buffer[n] = '\0';
        int dest_len = atoi(buffer);

        n = read(client_socket, buffer, dest_len);
        if(n <= 0) break;
        buffer[n] = '\0';
        string dest(buffer, n);

        // --- Leer mensaje ---
        n = read(client_socket, buffer, 3);
        if(n <= 0) break;
        buffer[n] = '\0';
        int msg_len = atoi(buffer);

        n = read(client_socket, buffer, msg_len);
        if(n <= 0) break;
        buffer[n] = '\0';
        string msg(buffer, n);

        cout << "Dest: [" << dest << "] Msg: [" << msg << "]" << endl;

        // --- Reenviar al destinatario si existe ---
        if(clients.find(dest) != clients.end()){
            int target_fd = clients[dest];

            // Buscar el nickname del remitente
            string sender = "unknown";
            for(auto it = clients.begin(); it != clients.end(); it++){
                if(it->second == client_socket){
                    sender = it->first;
                    break;
                }
            }

            // Construir paquete: [3B longSender][sender][3B longMsg][msg]
            char out[256];
            int offset = 0;
            int sender_len = sender.size();

            snprintf(out + offset, 4, "%03d", sender_len);
            offset += 3;
            memcpy(out + offset, sender.c_str(), sender_len);
            offset += sender_len;

            snprintf(out + offset, 4, "%03d", msg_len);
            offset += 3;
            memcpy(out + offset, msg.c_str(), msg_len);
            offset += msg_len;

            write(target_fd, out, offset);
        } else {
            cout << "Cliente [" << dest << "] no encontrado" << endl;
        }

    } while(1);
}

int main(void) {
    struct sockaddr_in stSockAddr;
    int SocketFD = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);
    char buffer[256];
    int n;

    if(-1 == SocketFD){
        perror("can not create socket");
        exit(EXIT_FAILURE);
    }

    memset(&stSockAddr, 0, sizeof(struct sockaddr_in));
    stSockAddr.sin_family = AF_INET;
    stSockAddr.sin_port = htons(1100);
    stSockAddr.sin_addr.s_addr = INADDR_ANY;

    if(-1 == bind(SocketFD, (const struct sockaddr *)&stSockAddr, sizeof(struct sockaddr_in))){
        perror("error bind failed");
        close(SocketFD);
        exit(EXIT_FAILURE);
    }

    if(-1 == listen(SocketFD, 10)){
        perror("error listen failed");
        close(SocketFD);
        exit(EXIT_FAILURE);
    }

    for(;;){
        int ConnectFD = accept(SocketFD, NULL, NULL);
        if(0 > ConnectFD){
            perror("error accept failed");
            close(SocketFD);
            exit(EXIT_FAILURE);
        }

        // Leer nickname del nuevo cliente
        memset(buffer, 0, sizeof(buffer));
        n = read(ConnectFD, buffer, 255);
        buffer[n] = '\0';

        clients[buffer] = ConnectFD;
        cout << "Nuevo cliente: [" << buffer << "] fd=" << ConnectFD << endl;

        thread t(threadReadSocket, ConnectFD);
        t.detach();
    }

    close(SocketFD);
    return 0;
}
