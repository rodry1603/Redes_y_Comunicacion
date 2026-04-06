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
    char local_buffer[256];
    int n;

    do {
        memset(local_buffer, 0, sizeof(local_buffer));
        n = read(client_socket, local_buffer, 255);
        if (n > 0) {
            cout << local_buffer << endl;
            // Broadcast message to all connected clients
            for (std::map<string, int>::iterator it = clients.begin(); it != clients.end(); it++) {
                write(it->second, local_buffer, n);
            }
        }
    } while (1);
}

int main(void)
{
    struct sockaddr_in stSockAddr;
    int SocketFD = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);
    char buffer[256];
    int n;

    if (-1 == SocketFD)
    {
        perror("can not create socket");
        exit(EXIT_FAILURE);
    }

    memset(&stSockAddr, 0, sizeof(struct sockaddr_in));

    stSockAddr.sin_family = AF_INET;
    stSockAddr.sin_port = htons(1100);
    stSockAddr.sin_addr.s_addr = INADDR_ANY;

    if (-1 == bind(SocketFD, (const struct sockaddr *)&stSockAddr, sizeof(struct sockaddr_in)))
    {
        perror("error bind failed");
        close(SocketFD);
        exit(EXIT_FAILURE);
    }

    if (-1 == listen(SocketFD, 10))
    {
        perror("error listen failed");
        close(SocketFD);
        exit(EXIT_FAILURE);
    }

    for (;;)
    {
        int ConnectFD = accept(SocketFD, NULL, NULL);

        if (0 > ConnectFD)
        {
            perror("error accept failed");
            close(SocketFD);
            exit(EXIT_FAILURE);
        }

        // Read the nickname from the new client
        memset(buffer, 0, sizeof(buffer));
        n = read(ConnectFD, buffer, 255);
        buffer[n] = '\0'; // Null-terminate the string

        clients[buffer] = ConnectFD;
        cout << "New client connected: " << buffer << " (fd=" << ConnectFD << ")" << endl;

        // Start a thread to handle this client
        thread t(threadReadSocket, ConnectFD);
        t.detach();
    }

    close(SocketFD);
    return 0;
}
