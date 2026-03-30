#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

int leer_input(char *buf, int max){
    int n = read(STDIN_FILENO, buf, max);
    if(n > 0 && buf[n-1] == '\n')
        n--;
    buf[n] = '\0';
    return n;
}

int main(void){
    struct sockaddr_in stSockAddr;
    int SocketFD = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    int n;
    char buffer[256];
    char dest[124];
    char msg[124];

    if(-1 == SocketFD){
        perror("error al conectar el socket");
        exit(EXIT_FAILURE);
    }

    memset(&stSockAddr, 0, sizeof(struct sockaddr_in));
    stSockAddr.sin_family = AF_INET;
    stSockAddr.sin_port = htons(1100);
    stSockAddr.sin_addr.s_addr = INADDR_ANY;

    if(-1 == bind(SocketFD, (const struct sockaddr *)&stSockAddr, sizeof(struct sockaddr_in))){
        perror("error en la conexion");
        close(SocketFD);
        exit(EXIT_FAILURE);
    }
    if(-1 == listen(SocketFD, 10)){
        perror("error al escuchar");
        close(SocketFD);
        exit(EXIT_FAILURE);
    }

    for(;;){
        int clientFD = accept(SocketFD, NULL, NULL);
        for(;;){
            bzero(buffer, 256);

            // --- Recibir ---
            n = read(clientFD, buffer, 3);
            buffer[n] = '\0';
            int l = atoi(buffer);

            n = read(clientFD, buffer, l);
            buffer[n] = '\0';
            char nickname[n + 1];
            strcpy(nickname, buffer);
            printf("Msg from: [%s]\n", nickname);

            n = read(clientFD, buffer, 3);
            buffer[n] = '\0';
            int ll = atoi(buffer);

            n = read(clientFD, buffer, ll);
            buffer[n] = '\0';
            char received_msg[n + 1];
            strcpy(received_msg, buffer);
            printf("Msg: [%s]\n", received_msg);

            // --- Enviar ---
            printf("Msg to: ");
            fflush(stdout);
            int dest_len = leer_input(dest, 123);

            printf("Msg: ");
            fflush(stdout);
            int msg_len = leer_input(msg, 123);

            int offset = 0;
            snprintf(buffer + offset, 4, "%03d", dest_len);
            offset += 3;
            memcpy(buffer + offset, dest, dest_len);
            offset += dest_len;

            snprintf(buffer + offset, 4, "%03d", msg_len);
            offset += 3;
            memcpy(buffer + offset, msg, msg_len);
            offset += msg_len;

            n = write(clientFD, buffer, offset);
            if(n == -1){
                perror("error al enviar");
                break;
            }
        }
        shutdown(clientFD, SHUT_RDWR);
        close(clientFD);
    }
    close(SocketFD);
    return 0;
}
