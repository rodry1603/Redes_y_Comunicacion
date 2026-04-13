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
#include <mutex>
using namespace std;

map<string, int> clients;
mutex clients_mutex;


void send_ok(int fd){
    char pkt = 'K';
    write(fd, &pkt, 1);
}


void send_error(int fd, const string &msg){
    char buffer[256];
    int msg_len = msg.size();
    int offset = 0;
    buffer[offset++] = 'E';
    snprintf(buffer + offset, 6, "%05d", msg_len);
    offset += 5;
    memcpy(buffer + offset, msg.c_str(), msg_len);
    offset += msg_len;
    write(fd, buffer, offset);
}

void threadReadSocket(int client_socket) {
    char buffer[256];
    int n;

    do {
        //Leer tipo de operacion
        n = read(client_socket, buffer, 1);
        if(n <= 0) break;
        char op = buffer[0];

        if(op == 'O'){
            //Logout
            clients_mutex.lock();
            for(auto it = clients.begin(); it != clients.end(); it++){
                if(it->second == client_socket){
                    cout << "Cliente desconectado: [" << it->first << "]" << endl;
                    clients.erase(it);
                    break;
                }
            }
            clients_mutex.unlock();
            send_ok(client_socket);
            break;

        } else if(op == 'U'){
            // Unicast
            n = read(client_socket, buffer, 3);
            if(n <= 0) break;
            buffer[n] = '\0';
            int dest_len = atoi(buffer);

            n = read(client_socket, buffer, dest_len);
            if(n <= 0) break;
            buffer[n] = '\0';
            string dest(buffer, n);

            n = read(client_socket, buffer, 3);
            if(n <= 0) break;
            buffer[n] = '\0';
            int msg_len = atoi(buffer);

            n = read(client_socket, buffer, msg_len);
            if(n <= 0) break;
            buffer[n] = '\0';
            string msg(buffer, n);

            // Buscar remitente
            string sender = "unknown";
            clients_mutex.lock();
            for(auto it = clients.begin(); it != clients.end(); it++){
                if(it->second == client_socket){
                    sender = it->first;
                    break;
                }
            }

            // Buscar destinatario y enviar
            if(clients.find(dest) != clients.end()){
                int target_fd = clients[dest];
                clients_mutex.unlock();

                // Construir paquete unicast
                char out[256];
                int offset = 0;
                int sender_len = sender.size();

                out[offset++] = 'U';
                snprintf(out + offset, 4, "%03d", sender_len);
                offset += 3;
                memcpy(out + offset, sender.c_str(), sender_len);
                offset += sender_len;
                snprintf(out + offset, 4, "%03d", msg_len);
                offset += 3;
                memcpy(out + offset, msg.c_str(), msg_len);
                offset += msg_len;

                write(target_fd, out, offset);
                send_ok(client_socket);
            } else {
                clients_mutex.unlock();
                send_error(client_socket, "Usuario no encontrado");
            }

        } else if(op == 'B'){
            //Broadcast
            n = read(client_socket, buffer, 3);
            if(n <= 0) break;
            buffer[n] = '\0';
            int msg_len = atoi(buffer);

            n = read(client_socket, buffer, msg_len);
            if(n <= 0) break;
            buffer[n] = '\0';
            string msg(buffer, n);

            // Buscar remitente
            string sender = "unknown";
            clients_mutex.lock();
            for(auto it = clients.begin(); it != clients.end(); it++){
                if(it->second == client_socket){
                    sender = it->first;
                    break;
                }
            }

            // Construir paquete broadcast
            char out[256];
            int offset = 0;
            int sender_len = sender.size();

            out[offset++] = 'B';
            snprintf(out + offset, 4, "%03d", sender_len);
            offset += 3;
            memcpy(out + offset, sender.c_str(), sender_len);
            offset += sender_len;
            snprintf(out + offset, 4, "%03d", msg_len);
            offset += 3;
            memcpy(out + offset, msg.c_str(), msg_len);
            offset += msg_len;

            // Enviar a todos menos al remitente
            for(auto it = clients.begin(); it != clients.end(); it++){
                if(it->second != client_socket){
                    write(it->second, out, offset);
                }
            }
            clients_mutex.unlock();
            send_ok(client_socket);

        } else {
            send_error(client_socket, "Operacion desconocida");
        }

    } while(1);

    close(client_socket);
}

int main(void){
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

        //Login
        n = read(ConnectFD, buffer, 1);
        if(n <= 0 || buffer[0] != 'L'){
            send_error(ConnectFD, "Se esperaba login");
            close(ConnectFD);
            continue;
        }

        n = read(ConnectFD, buffer, 4);
        buffer[n] = '\0';
        int nick_len = atoi(buffer);

        n = read(ConnectFD, buffer, nick_len);
        buffer[n] = '\0';
        string nickname(buffer, n);

        clients_mutex.lock();
        if(clients.find(nickname) != clients.end()){
            clients_mutex.unlock();
            send_error(ConnectFD, "Nickname ya en uso");
            close(ConnectFD);
            continue;
        }
        clients[nickname] = ConnectFD;
        clients_mutex.unlock();

        cout << "Nuevo cliente: [" << nickname << "] fd=" << ConnectFD << endl;
        send_ok(ConnectFD);

        thread t(threadReadSocket, ConnectFD);
        t.detach();
    }

    close(SocketFD);
    return 0;
}
