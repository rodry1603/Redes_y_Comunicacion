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

bool leer_nick_msg(int fd, string &nick, string &msg){
    char buffer[256];
    int n;

    // Leer nick
    n = read(fd, buffer, 3);
    if(n <= 0) return false;
    buffer[n] = '\0';
    int nick_len = atoi(buffer);

    n = read(fd, buffer, nick_len);
    if(n <= 0) return false;
    nick = string(buffer, n);

    // Leer mensaje
    n = read(fd, buffer, 3);
    if(n <= 0) return false;
    buffer[n] = '\0';
    int msg_len = atoi(buffer);

    n = read(fd, buffer, msg_len);
    if(n <= 0) return false;
    msg = string(buffer, n);

    return true;
}


int construir_nick_msg(char *out, char op, const string &nick, const string &msg){
    int offset = 0;
    int nick_len = nick.size();
    int msg_len  = msg.size();

    out[offset++] = op;
    snprintf(out + offset, 4, "%03d", nick_len);
    offset += 3;
    memcpy(out + offset, nick.c_str(), nick_len);
    offset += nick_len;
    snprintf(out + offset, 4, "%03d", msg_len);
    offset += 3;
    memcpy(out + offset, msg.c_str(), msg_len);
    offset += msg_len;

    return offset;
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
            string dest, msg;
            if(!leer_nick_msg(client_socket, dest, msg)) break;

            string sender = "unknown";
            clients_mutex.lock();
            for(auto it = clients.begin(); it != clients.end(); it++)
                if(it->second == client_socket){ sender = it->first; break; }

            if(clients.find(dest) != clients.end()){
                int target_fd = clients[dest];
                clients_mutex.unlock();

                char out[256];
                int total = construir_nick_msg(out, 'U', sender, msg);
                write(target_fd, out, total);
                send_ok(client_socket);
            } else {
                clients_mutex.unlock();
                send_error(client_socket, "Usuario no encontrado");
            }
        } else if(op == 'B'){
            //Broadcast
            char buffer[256];
            int n;
            n = read(client_socket, buffer, 3);
            if(n <= 0) break;
            buffer[n] = '\0';
            int msg_len = atoi(buffer);

            n = read(client_socket, buffer, msg_len);
            if(n <= 0) break;
            string msg(buffer, n);

            string sender = "unknown";
            clients_mutex.lock();
            for(auto it = clients.begin(); it != clients.end(); it++)
                if(it->second == client_socket){ sender = it->first; break; }

            char out[256];
            int total = construir_nick_msg(out, 'B', sender, msg);
            for(auto it = clients.begin(); it != clients.end(); it++)
                if(it->second != client_socket)
                    write(it->second, out, total);
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

        // --- Login: L [4B longNick][Nick] ---
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
