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
    int FDclient = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    int n;
    int res;
    char buffer[256];

    if(FDclient == -1){
        perror("error");
        exit(EXIT_FAILURE);
    }

    stSockAddr.sin_family = AF_INET;
    stSockAddr.sin_port = htons(1100);
    res = inet_pton(AF_INET, "172.31.215.158", &stSockAddr.sin_addr);

    if(res == -1){
        perror("error en el primer parametro");
        close(FDclient);
        exit(EXIT_FAILURE);
    }
    else if(res == 0){
        perror("error en el segundo parametro");
        close(FDclient);
        exit(EXIT_FAILURE);
    }

    if(-1 == connect(FDclient, (const struct sockaddr*)&stSockAddr, sizeof(struct sockaddr_in))){
        perror("error en conexion");
        close(FDclient);
        exit(EXIT_FAILURE);
    }

    for(;;){
        printf("Destinatario: ");
        fflush(stdout);
        int dest_len = leer_input(buffer + 3, 123);
        snprintf(buffer, 4, "%03d", dest_len);
        buffer[3 + dest_len] = '\0';

        int msg_offset = 3 + dest_len;
        printf("Mensaje: ");
        fflush(stdout);
        int msg_len = leer_input(buffer + msg_offset + 3, 123);
        snprintf(buffer + msg_offset, 4, "%03d", msg_len);
        buffer[msg_offset + 3 + msg_len] = '\0';

        int total = msg_offset + 3 + msg_len;

        n = write(FDclient, buffer, total);
        if(n == -1){
            perror("error al enviar");
            break;
        }

        // --- Recibir respuesta ---
        n = read(FDclient, buffer, 3);
        buffer[n] = '\0';
        int l = atoi(buffer);

        n = read(FDclient, buffer, l);
        buffer[n] = '\0';
        char nickname[n + 1];
        strcpy(nickname, buffer);
        printf("Msg from: [%s]\n", nickname);

        n = read(FDclient, buffer, 3);
        buffer[n] = '\0';
        int ll = atoi(buffer);

        n = read(FDclient, buffer, ll);
        buffer[n] = '\0';
        char received_msg[n + 1];
        strcpy(received_msg, buffer);
        printf("Msg: [%s]\n", received_msg);
    }

    shutdown(FDclient, SHUT_RDWR);
    close(FDclient);
    return 0;
}
