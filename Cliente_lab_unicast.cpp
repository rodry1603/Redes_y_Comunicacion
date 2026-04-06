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
using namespace std;

void threadReadSocket(int client_socket) {
    char buffer[256];
    int n;

    do {
        memset(buffer, 0, sizeof(buffer));

        // --- Leer remitente ---
        n = read(client_socket, buffer, 3);
        if(n <= 0) break;
        buffer[n] = '\0';
        int sender_len = atoi(buffer);

        n = read(client_socket, buffer, sender_len);
        if(n <= 0) break;
        buffer[n] = '\0';
        string sender(buffer, n);

        // --- Leer mensaje ---
        n = read(client_socket, buffer, 3);
        if(n <= 0) break;
        buffer[n] = '\0';
        int msg_len = atoi(buffer);

        n = read(client_socket, buffer, msg_len);
        if(n <= 0) break;
        buffer[n] = '\0';
        string msg(buffer, n);

        // Imprimir en una línea nueva sin mezclar con prompts
        cout << "\n[" << sender << "]: " << msg << "\n" << endl;
        cout.flush();

    } while(1);
}

int main(void) {
    struct sockaddr_in stSockAddr;
    int Res;
    int SocketFD = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);

    if(-1 == SocketFD){
        perror("cannot create socket");
        exit(EXIT_FAILURE);
    }

    memset(&stSockAddr, 0, sizeof(struct sockaddr_in));
    stSockAddr.sin_family = AF_INET;
    stSockAddr.sin_port = htons(1100);
    Res = inet_pton(AF_INET, "172.31.215.158", &stSockAddr.sin_addr);

    if(0 > Res){
        perror("error: first parameter is not a valid address family");
        close(SocketFD);
        exit(EXIT_FAILURE);
    } else if(0 == Res){
        perror("char string (second parameter does not contain valid ipaddress)");
        close(SocketFD);
        exit(EXIT_FAILURE);
    }

    if(-1 == connect(SocketFD, (const struct sockaddr *)&stSockAddr, sizeof(struct sockaddr_in))){
        perror("connect failed");
        close(SocketFD);
        exit(EXIT_FAILURE);
    }

    // --- Enviar nickname ---
    string nickname;
    cout << "Enter your nickname: ";
    getline(cin, nickname);
    write(SocketFD, nickname.c_str(), nickname.size());

    // --- Thread para recibir mensajes ---
    thread t(threadReadSocket, SocketFD);
    t.detach();

    // --- Loop principal: enviar mensajes ---
    string dest, msg;
    char buffer[256];

    do {
        cout << "Destinatario: ";
        getline(cin, dest);

        cout << "Mensaje: ";
        getline(cin, msg);

        if(dest.empty() || msg.empty()) continue;

        int dest_len = dest.size();
        int msg_len  = msg.size();
        int offset   = 0;

        snprintf(buffer + offset, 4, "%03d", dest_len);
        offset += 3;
        memcpy(buffer + offset, dest.c_str(), dest_len);
        offset += dest_len;

        snprintf(buffer + offset, 4, "%03d", msg_len);
        offset += 3;
        memcpy(buffer + offset, msg.c_str(), msg_len);
        offset += msg_len;

        write(SocketFD, buffer, offset);

    } while(1);

    shutdown(SocketFD, SHUT_RDWR);
    close(SocketFD);
    return 0;
}
