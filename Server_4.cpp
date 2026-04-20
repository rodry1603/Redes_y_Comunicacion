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
#include <algorithm>
using namespace std;

map<string, int> clients;
mutex clients_mutex;

//"funciones de envio"

void send_ok(int fd){
    char pkt = 'K';
    write(fd, &pkt, 1);
}

void send_error(int fd, const string &msg){
    string pkt;
    pkt += 'E';
    char tmp[8];
    snprintf(tmp, 6, "%05d", (int)msg.size());
    pkt.append(tmp, 5);
    pkt += msg;
    write(fd, pkt.c_str(), pkt.size());
}

//"funciones de lectura"

// Leer campo con prefijo de N bytes de longitud
bool leer_campo(int fd, string &out, int prefix_size){
    char buf[16];
    int n = read(fd, buf, prefix_size);
    if(n <= 0) return false;
    buf[n] = '\0';
    int len = atoi(buf);

    out.clear();
    char blk[1024];
    int remaining = len;
    while(remaining > 0){
        int to_read = min(remaining, (int)sizeof(blk));
        n = read(fd, blk, to_read);
        if(n <= 0) return false;
        out.append(blk, n);
        remaining -= n;
    }
    return true;
}

//"funciones de escritura"

// Agregar campo [NB longitud][contenido] a un string paquete
void agregar_campo(string &pkt, const string &data, int prefix_size){
    char tmp[16];
    snprintf(tmp, prefix_size + 1, "%0*d", prefix_size, (int)data.size());
    pkt.append(tmp, prefix_size);
    pkt += data;
}

//Buscar remitente por fd
string buscar_sender(int fd){
    for(auto it = clients.begin(); it != clients.end(); it++)
        if(it->second == fd) return it->first;
    return "unknown";
}


void threadReadSocket(int client_socket){
    char op_buf[2];
    int n;

    do {
        n = read(client_socket, op_buf, 1);
        if(n <= 0) break;
        char op = op_buf[0];

        //LOGOUT
        if(op == 'O'){
            clients_mutex.lock();
            string sender = buscar_sender(client_socket);
            clients.erase(sender);
            clients_mutex.unlock();
            cout << "Cliente desconectado: [" << buscar_sender(client_socket) << "]" << endl;
            send_ok(client_socket);
            break;

        //BROADCAST
        } else if(op == 'B'){
            string msg;
            if(!leer_campo(client_socket, msg, 7)) break;

            clients_mutex.lock();
            string sender = buscar_sender(client_socket);

            
            string pkt;
            pkt += 'b';
            agregar_campo(pkt, sender, 3);
            agregar_campo(pkt, msg,    7);

            for(auto it = clients.begin(); it != clients.end(); it++)
                if(it->second != client_socket)
                    write(it->second, pkt.c_str(), pkt.size());
            clients_mutex.unlock();
            send_ok(client_socket);

        //UNICAST
        } else if(op == 'U'){
            string msg, dest;
            if(!leer_campo(client_socket, msg,  5)) break;
            if(!leer_campo(client_socket, dest, 7)) break;

            clients_mutex.lock();
            string sender = buscar_sender(client_socket);

            if(clients.find(dest) != clients.end()){
                int target_fd = clients[dest];
                clients_mutex.unlock();

                
                string pkt;
                pkt += 'u';
                agregar_campo(pkt, sender, 7);
                agregar_campo(pkt, msg,    5);
                write(target_fd, pkt.c_str(), pkt.size());
                send_ok(client_socket);
            } else {
                clients_mutex.unlock();
                send_error(client_socket, "Usuario no encontrado");
            }

        //LIST
        } else if(op == 'T'){
            string json = "{\"clients\":[";
            clients_mutex.lock();
            bool first = true;
            for(auto it = clients.begin(); it != clients.end(); it++){
                if(!first) json += ",";
                json += "\"" + it->first + "\"";
                first = false;
            }
            clients_mutex.unlock();
            json += "]}";

            
            string pkt;
            pkt += 't';
            agregar_campo(pkt, json, 5);
            write(client_socket, pkt.c_str(), pkt.size());

        //FILE 
        } else if(op == 'F'){
            string content, filename, dest;
            if(!leer_campo(client_socket, content,  5)) break;
            if(!leer_campo(client_socket, filename, 5)) break;
            if(!leer_campo(client_socket, dest,     5)) break;

            clients_mutex.lock();
            string sender = buscar_sender(client_socket);

            if(clients.find(dest) != clients.end()){
                int target_fd = clients[dest];
                clients_mutex.unlock();

                
                string pkt;
                pkt += 'f';
                agregar_campo(pkt, content,  5);
                agregar_campo(pkt, filename, 5);
                agregar_campo(pkt, sender,   5);
                write(target_fd, pkt.c_str(), pkt.size());
                send_ok(client_socket);
            } else {
                clients_mutex.unlock();
                send_error(client_socket, "Usuario no encontrado");
            }

        } else {
            send_error(client_socket, "Operacion desconocida");
        }

    } while(1);

    close(client_socket);
}


int main(void){
    struct sockaddr_in stSockAddr;
    int SocketFD = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);

    if(-1 == SocketFD){ perror("can not create socket"); exit(EXIT_FAILURE); }

    memset(&stSockAddr, 0, sizeof(stSockAddr));
    stSockAddr.sin_family      = AF_INET;
    stSockAddr.sin_port        = htons(1100);
    stSockAddr.sin_addr.s_addr = INADDR_ANY;

    if(-1 == bind(SocketFD, (const struct sockaddr*)&stSockAddr, sizeof(stSockAddr)))
        { perror("bind failed");   close(SocketFD); exit(EXIT_FAILURE); }
    if(-1 == listen(SocketFD, 10))
        { perror("listen failed"); close(SocketFD); exit(EXIT_FAILURE); }

    for(;;){
        int ConnectFD = accept(SocketFD, NULL, NULL);
        if(0 > ConnectFD){ perror("accept failed"); close(SocketFD); exit(EXIT_FAILURE); }

        // LOGIN
        char op;
        if(read(ConnectFD, &op, 1) <= 0 || op != 'L'){
            send_error(ConnectFD, "Se esperaba login");
            close(ConnectFD); continue;
        }

        string nickname;
        if(!leer_campo(ConnectFD, nickname, 4)){
            close(ConnectFD); continue;
        }

        clients_mutex.lock();
        if(clients.find(nickname) != clients.end()){
            clients_mutex.unlock();
            send_error(ConnectFD, "Nickname ya en uso");
            close(ConnectFD); continue;
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
