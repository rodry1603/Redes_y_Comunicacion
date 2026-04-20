#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <iostream>
using namespace std;

class TresEnRaya {
public:
    char board[9];
    int  currentMove;
    int  winner;

    TresEnRaya(){
        memset(board, ' ', sizeof(board));
        currentMove = -1;
        winner      =  0;
    }

    void print(){
        printf("\n");
        printf(" %c | %c | %c\n", board[0], board[1], board[2]);
        printf("---|---|---\n");
        printf(" %c | %c | %c\n", board[3], board[4], board[5]);
        printf("---|---|---\n");
        printf(" %c | %c | %c\n", board[6], board[7], board[8]);
        printf("\n");
    }

    void checkWinner(){
        int lines[8][3] = {
            {0,1,2},{3,4,5},{6,7,8},
            {0,3,6},{1,4,7},{2,5,8},
            {0,4,8},{2,4,6}
        };
        for(int i = 0; i < 8; i++){
            char a = board[lines[i][0]];
            char b = board[lines[i][1]];
            char c = board[lines[i][2]];
            if(a != ' ' && a == b && b == c){
                winner = (a == 'X') ? 1 : 2;
                return;
            }
        }
        bool full = true;
        for(int i = 0; i < 9; i++)
            if(board[i] == ' '){ full = false; break; }
        if(full) winner = 3;
    }
};

bool enviar_objeto(int fd, TresEnRaya &game){
    ssize_t total = 0, n;
    while(total < (ssize_t)sizeof(game)){
        n = write(fd, ((char*)&game) + total, sizeof(game) - total);
        if(n <= 0) return false;
        total += n;
    }
    return true;
}

bool recibir_objeto(int fd, TresEnRaya &game){
    ssize_t total = 0, n;
    while(total < (ssize_t)sizeof(game)){
        n = read(fd, ((char*)&game) + total, sizeof(game) - total);
        if(n <= 0) return false;
        total += n;
    }
    return true;
}

int main(){
    int sock;
    struct sockaddr_in server;

    sock = socket(AF_INET, SOCK_STREAM, 0);
    server.sin_family      = AF_INET;
    server.sin_port        = htons(5000);
    server.sin_addr.s_addr = inet_addr("127.0.0.1");

    connect(sock, (struct sockaddr*)&server, sizeof(server));
    printf("Connected to server.\n");

    TresEnRaya game;
    //Cliente siempre es X
    const char MY_PIECE = 'X';

    game.print();

    while(game.winner == 0){
        //Jugada del cliente
        int move = -1;
        do {
            printf("Client turn (%c) - casilla (1-9): ", MY_PIECE);
            scanf("%d", &move);
            move--;
        } while(move < 0 || move > 8 || game.board[move] != ' ');

        game.board[move] = MY_PIECE;
        game.currentMove = move;
        game.checkWinner();
        game.print();

        if(!enviar_objeto(sock, game)){
            printf("Error al enviar\n");
            break;
        }

        if(game.winner != 0) break;

        //Esperar jugada del servidor
        printf("Esperando jugada del servidor...\n");

        if(!recibir_objeto(sock, game)){
            printf("Connection closed or error\n");
            break;
        }

        printf("Servidor jugo en casilla %d\n", game.currentMove + 1);
        game.print();
    }

    if(game.winner == 1)      printf("Gana X (cliente)!\n");
    else if(game.winner == 2) printf("Gana O (servidor)!\n");
    else if(game.winner == 3) printf("Empate!\n");

    close(sock);
    return 0;
}
