#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

int main(void){
    struct sockaddr_in stSockAddr;
    int SocketFD = socket(AF_INET,SOCK_STREAM,IPPROTO_TCP);
    int n;
    char buffer[256];

    if(-1 == SocketFD){
        perror("error al conectar el socket");
        exit(EXIT_FAILURE);
    }
    
    memset(&stSockAddr,0,sizeof(struct sockaddr_in));

    stSockAddr.sin_family = AF_INET;
    stSockAddr.sin_port = htons(1100);
    stSockAddr.sin_addr.s_addr = INADDR_ANY;

    if(-1 == bind(SocketFD, (const struct sockaddr *)&stSockAddr , sizeof(struct sockaddr_in) )){
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
        int clientFD = accept(SocketFD,NULL,NULL);
        for(;;){
            bzero(buffer,256);
            n = read(clientFD,buffer,255);
            printf("Cli: [%s]\n",buffer);
            printf("Enter msg: ");
            scanf("%s",buffer);
            n = write(clientFD,buffer,255);
        }
            shutdown(clientFD,SHUT_RDWR);
            close(clientFD);
    }
    close(SocketFD);
    return 0;


}












