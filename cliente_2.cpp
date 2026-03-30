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
    int FDclient = socket(AF_INET,SOCK_STREAM,IPPROTO_TCP);
    int n;
    int res;
    char buffer[256];
    char dest[124];
    char msg[124];
    if(FDclient ==-1){
        perror("error");
        exit(EXIT_FAILURE);
    }
    
    stSockAddr.sin_family = AF_INET;
    stSockAddr.sin_port = htons(1100);
    res = inet_pton(AF_INET,"172.31.215.158",&stSockAddr.sin_addr);
    
    if(res == -1){
        perror("error en el primer parametro");
        close(FDclient);
        exit(EXIT_FAILURE);
    }
    else if (res == 0){
        perror("error en el segundo parametro");
        close(FDclient);
        exit(EXIT_FAILURE);
    }    

    if(-1 == connect(FDclient, (const struct sockaddr*)&stSockAddr,sizeof(struct sockaddr_in))){
        perror("error en conexion");
        close(FDclient);
        exit(EXIT_FAILURE);
    }
    for(;;){
        printf("Msg to: ");
        scanf("%123s", dest);
        printf("Msg: ");
        scanf("%123s", msg);

        int dest_len = strlen(dest);
        int msg_len = strlen(msg);

        int offset = 0;

        snprintf(buffer + offset, 4, "%03d", dest_len);
        offset += 3;

        memcpy(buffer + offset, dest, dest_len);
        offset += dest_len;

        sprintf(buffer + offset, "%03d", msg_len);
        offset += 3;

        memcpy(buffer + offset, msg, msg_len);
        offset += msg_len;

        n = write(FDclient,buffer,offset);
        if(n == -1){
            perror("error al enviar");
            break;
        }

        n = read(FDclient,buffer,3);
        buffer[n] = '\0';
        int l = atoi(buffer);
        n = read(FDclient,buffer,l);
        buffer[n] = '\0';
        char nickname[n+1];
        strcpy(nickname,buffer);
        printf("Msg from: [%s]\n",nickname);
        n = read(FDclient,buffer,3);
        buffer[n] = '\0';
        int ll = atoi(buffer);
        n=read(FDclient,buffer,ll);
        buffer[n] = '\0';
        char msg[n+1];
        strcpy(msg,buffer);
        printf("Msg: [%s]\n",msg);
    }
    shutdown(FDclient,SHUT_RDWR);
    close(FDclient);
    return 0;

  }






























