#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <map>
#include <string>
#include <thread>
#include <iostream>

using namespace std;

map<string,int> clients;
void threadReadSocket(int client_socket){ 
    char local_buffer[1000];
    int n;
    do{
        n=read(client_socket,local_buffer,255);
        for(map<string,int>::iterator it=clients.begin(); it!=clients.end();++it){ 
            cout<< it->first << it->second;
            write(it->second,local_buffer,n);
        }
    }while(1);
}

int main(void){
    struct sockaddr_in stSockAddr;
    int SocketFD = socket(AF_INET,SOCK_STREAM,IPPROTO_TCP);
    int n;
    char buffer[256];
    char dest[124];
    char msg[124];

    if(-1 == SocketFD){
        perror("error al conectar el socket");
        exit(EXIT_FAILURE);
    }
    
    memset(&stSockAddr,0,sizeof(struct sockaddr_in));

    stSockAddr.sin_family = AF_INET;
    stSockAddr.sin_port = htons(1101);
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

            n = read(clientFD,buffer,255);//read nickname
            buffer[n] = '\0';
            clients[buffer] = clientFD;

            thread (threadReadSocket,clientFD).detach();

            printf("Msg to:");
            scanf("%123s", dest);
            printf("Msg:");
            scanf("%123s", msg);
            int dest_len = strlen(dest);
            int msg_len = strlen(msg);
            int offset = 0;

            snprintf(buffer + offset, 4, "%03d", dest_len);
            offset += 3;
            memcpy(buffer + offset, dest, dest_len);
            offset += dest_len;
            snprintf(buffer + offset, 4, "%03d", msg_len);
            offset += 3;
            memcpy(buffer + offset, msg, msg_len);
            offset += msg_len;
            
            n = write(clientFD,buffer,offset);
            if(n == -1){
                perror("error al enviar");
                break;
            }
        }
        shutdown(clientFD,SHUT_RDWR);
        close(clientFD);
    }
    close(SocketFD);
    return 0;


}