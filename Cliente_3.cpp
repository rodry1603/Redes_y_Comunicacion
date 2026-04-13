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
        //Leer tipo de operacion
        n = read(client_socket, buffer, 1);
        if(n <= 0) break;
        char op = buffer[0];

        if(op == 'K'){
            //OK
            cout << "[OK]" << endl;

        } else if(op == 'E'){
            //Error
            n = read(client_socket, buffer, 5);
            if(n <= 0) break;
            buffer[n] = '\0';
            int msg_len = atoi(buffer);

            n = read(client_socket, buffer, msg_len);
            if(n <= 0) break;
            buffer[n] = '\0';
            string msg(buffer, n);
            cout << "[ERROR]: " << msg << endl;

        } else if(op == 'U'){
            //Unicast recibido
            n = read(client_socket, buffer, 3);
            if(n <= 0) break;
            buffer[n] = '\0';
            int sender_len = atoi(buffer);

            n = read(client_socket, buffer, sender_len);
            if(n <= 0) break;
            buffer[n] = '\0';
            string sender(buffer, n);

            n = read(client_socket, buffer, 3);
            if(n <= 0) break;
            buffer[n] = '\0';
            int msg_len = atoi(buffer);

            n = read(client_socket, buffer, msg_len);
            if(n <= 0) break;
            buffer[n] = '\0';
            string msg(buffer, n);

            cout << "\n[Privado][" << sender << "]: " << msg << endl;

        } else if(op == 'B'){
            //Broadcast recibido:
            n = read(client_socket, buffer, 3);
            if(n <= 0) break;
            buffer[n] = '\0';
            int sender_len = atoi(buffer);

            n = read(client_socket, buffer, sender_len);
            if(n <= 0) break;
            buffer[n] = '\0';
            string sender(buffer, n);

            n = read(client_socket, buffer, 3);
            if(n <= 0) break;
            buffer[n] = '\0';
            int msg_len = atoi(buffer);

            n = read(client_socket, buffer, msg_len);
            if(n <= 0) break;
            buffer[n] = '\0';
            string msg(buffer, n);

            cout << "\n[Broadcast][" << sender << "]: " << msg << endl;
        }

        cout.flush();

    } while(1);
}

int main(void){
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

    //Login
    string nickname;
    cout << "Nickname: ";
    getline(cin, nickname);

    char buffer[256];
    int offset = 0;
    int nick_len = nickname.size();

    buffer[offset++] = 'L';
    snprintf(buffer + offset, 5, "%04d", nick_len);
    offset += 4;
    memcpy(buffer + offset, nickname.c_str(), nick_len);
    offset += nick_len;
    write(SocketFD, buffer, offset);

    //Thread para recibir mensajes
    thread t(threadReadSocket, SocketFD);
    t.detach();

    
    string input;
    do {
        cout << "\nAccion (U=unicast, B=broadcast, O=logout): ";
        getline(cin, input);
        if(input.empty()) continue;
        char op = input[0];

        if(op == 'O'){
            // --- Logout ---
            buffer[0] = 'O';
            write(SocketFD, buffer, 1);
            break;

        } else if(op == 'U'){
            //Unicast
            string dest, msg;
            cout << "Destinatario: ";
            getline(cin, dest);
            cout << "Mensaje: ";
            getline(cin, msg);

            int dest_len = dest.size();
            int msg_len  = msg.size();
            offset = 0;

            buffer[offset++] = 'U';
            snprintf(buffer + offset, 4, "%03d", dest_len);
            offset += 3;
            memcpy(buffer + offset, dest.c_str(), dest_len);
            offset += dest_len;
            snprintf(buffer + offset, 4, "%03d", msg_len);
            offset += 3;
            memcpy(buffer + offset, msg.c_str(), msg_len);
            offset += msg_len;

            write(SocketFD, buffer, offset);

        } else if(op == 'B'){
            // Broadcast
            string msg;
            cout << "Mensaje: ";
            getline(cin, msg);

            int msg_len = msg.size();
            offset = 0;

            buffer[offset++] = 'B';
            snprintf(buffer + offset, 4, "%03d", msg_len);
            offset += 3;
            memcpy(buffer + offset, msg.c_str(), msg_len);
            offset += msg_len;

            write(SocketFD, buffer, offset);

        } else {
            cout << "Accion no reconocida" << endl;
        }

    } while(1);

    shutdown(SocketFD, SHUT_RDWR);
    close(SocketFD);
    return 0;
}
