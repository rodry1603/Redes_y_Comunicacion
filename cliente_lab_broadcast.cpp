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
    char local_buffer[256];
    int n;

    do {
        memset(local_buffer, 0, sizeof(local_buffer));
        n = read(client_socket, local_buffer, 255);
        if (n > 0) {
            cout << local_buffer;
            cout.flush();
        }
    } while (1);
}

int main(void)
{
    struct sockaddr_in stSockAddr;
    int Res;
    int SocketFD = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);

    if (-1 == SocketFD)
    {
        perror("cannot create socket");
        exit(EXIT_FAILURE);
    }

    memset(&stSockAddr, 0, sizeof(struct sockaddr_in));

    stSockAddr.sin_family = AF_INET;
    stSockAddr.sin_port = htons(1100);
    Res = inet_pton(AF_INET, "172.31.215.158", &stSockAddr.sin_addr);

    if (0 > Res)
    {
        perror("error: first parameter is not a valid address family");
        close(SocketFD);
        exit(EXIT_FAILURE);
    }
    else if (0 == Res)
    {
        perror("char string (second parameter does not contain valid ipaddress)");
        close(SocketFD);
        exit(EXIT_FAILURE);
    }

    if (-1 == connect(SocketFD, (const struct sockaddr *)&stSockAddr, sizeof(struct sockaddr_in)))
    {
        perror("connect failed");
        close(SocketFD);
        exit(EXIT_FAILURE);
    }

    // Ask for nickname and send it first
    string nickname;
    cout << "Enter your nickname: ";
    cin >> nickname;
    write(SocketFD, nickname.c_str(), nickname.size());

    // Start thread to read incoming messages
    thread t(threadReadSocket, SocketFD);
    t.detach();

    // Main loop: read from stdin and send to server
    string input;
    do {
        cin >> input;
        if (!input.empty()) {
            write(SocketFD, input.c_str(), input.size());
        }
    } while (1);

    shutdown(SocketFD, SHUT_RDWR);
    close(SocketFD);
    return 0;
}
