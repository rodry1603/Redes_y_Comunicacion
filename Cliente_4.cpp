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
#include <algorithm>
using namespace std;

// funciones de lectura

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

//funcion de escritura

void agregar_campo(string &pkt, const string &data, int prefix_size){
    char tmp[16];
    snprintf(tmp, prefix_size + 1, "%0*d", prefix_size, (int)data.size());
    pkt.append(tmp, prefix_size);
    pkt += data;
}

void threadReadSocket(int client_socket){
    char op_buf[2];
    int n;

    do {
        n = read(client_socket, op_buf, 1);
        if(n <= 0) break;
        char op = op_buf[0];

        //OK
        if(op == 'K'){
            cout << "[OK]" << endl;

        //ERROR
        } else if(op == 'E'){
            string msg;
            if(!leer_campo(client_socket, msg, 5)) break;
            cout << "[ERROR]: " << msg << endl;

        //BROADCAST
        } else if(op == 'b'){
            string sender, msg;
            if(!leer_campo(client_socket, sender, 3)) break;
            if(!leer_campo(client_socket, msg,    7)) break;
            cout << "\n[Broadcast][" << sender << "]: " << msg << endl;

        //UNICAST
        } else if(op == 'u'){
            string sender, msg;
            if(!leer_campo(client_socket, sender, 7)) break;
            if(!leer_campo(client_socket, msg,    5)) break;
            cout << "\n[Privado][" << sender << "]: " << msg << endl;

        //LIST
        } else if(op == 't'){
            string json;
            if(!leer_campo(client_socket, json, 5)) break;

            cout << "\n--- Clientes conectados ---" << endl;
            size_t pos = json.find('[');
            size_t end = json.find(']');
            if(pos != string::npos && end != string::npos){
                string lista = json.substr(pos + 1, end - pos - 1);
                size_t start = 0;
                while(start < lista.size()){
                    size_t comma = lista.find(',', start);
                    string entry = lista.substr(start, comma == string::npos ? string::npos : comma - start);
                    entry.erase(remove(entry.begin(), entry.end(), '"'), entry.end());
                    if(!entry.empty()) cout << "  - " << entry << endl;
                    if(comma == string::npos) break;
                    start = comma + 1;
                }
            }
            cout << "---------------------------" << endl;

        //FILE
        } else if(op == 'f'){
            string content, filename, sender;
            if(!leer_campo(client_socket, content,  5)) break;
            if(!leer_campo(client_socket, filename, 5)) break;
            if(!leer_campo(client_socket, sender,   5)) break;

            FILE *f = fopen(filename.c_str(), "wb");
            if(f){
                fwrite(content.c_str(), 1, content.size(), f);
                fclose(f);
                cout << "\n[Archivo de " << sender << "]: " << filename << " guardado." << endl;
            } else {
                cout << "\n[Error]: No se pudo guardar " << filename << endl;
            }
        }

        cout.flush();

    } while(1);
}


int main(void){
    struct sockaddr_in stSockAddr;
    int Res;
    int SocketFD = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);

    if(-1 == SocketFD){ perror("cannot create socket"); exit(EXIT_FAILURE); }

    memset(&stSockAddr, 0, sizeof(stSockAddr));
    stSockAddr.sin_family = AF_INET;
    stSockAddr.sin_port   = htons(1100);
    Res = inet_pton(AF_INET, "172.31.215.158", &stSockAddr.sin_addr);

    if(0 > Res){ perror("invalid address family"); close(SocketFD); exit(EXIT_FAILURE); }
    else if(0 == Res){ perror("invalid ip address"); close(SocketFD); exit(EXIT_FAILURE); }

    if(-1 == connect(SocketFD, (const struct sockaddr*)&stSockAddr, sizeof(stSockAddr)))
        { perror("connect failed"); close(SocketFD); exit(EXIT_FAILURE); }

    //LOGIN
    string nickname;
    cout << "Nickname: ";
    getline(cin, nickname);

    string login_pkt;
    login_pkt += 'L';
    agregar_campo(login_pkt, nickname, 4);
    write(SocketFD, login_pkt.c_str(), login_pkt.size());

    
    thread t(threadReadSocket, SocketFD);
    t.detach();

    // Loop principal
    string input;
    do {
        cout << "\nAccion (U=unicast, B=broadcast, T=lista, F=archivo, O=logout): ";
        getline(cin, input);
        if(input.empty()) continue;
        char op = input[0];

        //LOGOUT
        if(op == 'O'){
            char pkt = 'O';
            write(SocketFD, &pkt, 1);
            break;

        //BROADCAST
        } else if(op == 'B'){
            string msg;
            cout << "Mensaje: ";
            getline(cin, msg);

            string pkt;
            pkt += 'B';
            agregar_campo(pkt, msg, 7);
            write(SocketFD, pkt.c_str(), pkt.size());

        //UNICAST
        } else if(op == 'U'){
            string dest, msg;
            cout << "Destinatario: ";
            getline(cin, dest);
            cout << "Mensaje: ";
            getline(cin, msg);

            string pkt;
            pkt += 'U';
            agregar_campo(pkt, msg,  5);
            agregar_campo(pkt, dest, 7);
            write(SocketFD, pkt.c_str(), pkt.size());

        // LIST
        } else if(op == 'T'){
            char pkt = 'T';
            write(SocketFD, &pkt, 1);

        //FILE
        } else if(op == 'F'){
            string filename, dest;
            cout << "Archivo (.txt): ";
            getline(cin, filename);
            cout << "Destinatario: ";
            getline(cin, dest);

            FILE *f = fopen(filename.c_str(), "rb");
            if(!f){ cout << "[Error]: No se pudo abrir " << filename << endl; continue; }
            fseek(f, 0, SEEK_END);
            long fsize = ftell(f);
            fseek(f, 0, SEEK_SET);
            string content(fsize, '\0');
            fread(&content[0], 1, fsize, f);
            fclose(f);

            string pkt;
            pkt += 'F';
            agregar_campo(pkt, content,  5);
            agregar_campo(pkt, filename, 5);
            agregar_campo(pkt, dest,     5);
            write(SocketFD, pkt.c_str(), pkt.size());

        } else {
            cout << "Accion no reconocida" << endl;
        }

    } while(1);

    shutdown(SocketFD, SHUT_RDWR);
    close(SocketFD);
    return 0;
}
