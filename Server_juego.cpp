#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
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
    int server_fd, client_fd;
    struct sockaddr_in addr;
    socklen_t addrlen = sizeof(addr);

    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    addr.sin_family      = AF_INET;
    addr.sin_port        = htons(5000);
    addr.sin_addr.s_addr = INADDR_ANY;

    bind(server_fd, (struct sockaddr*)&addr, sizeof(addr));
    listen(server_fd, 5);
    printf("Server waiting...\n");

    client_fd = accept(server_fd, (struct sockaddr*)&addr, &addrlen);
    printf("Client connected.\n");

    TresEnRaya game;
    // Servidor siempre es O
    const char MY_PIECE = 'O';

    game.print();

    while(game.winner == 0){
        //Recibir jugada del cliente
        if(!recibir_objeto(client_fd, game)){
            printf("Connection closed or error\n");
            break;
        }

        printf("Cliente jugo en casilla %d\n", game.currentMove + 1);
        game.checkWinner();
        game.print();

        if(game.winner != 0) break;

        //Jugada del servidor
        int move = -1;
        do {
            printf("Server turn (%c) - casilla (1-9): ", MY_PIECE);
            scanf("%d", &move);
            move--;
        } while(move < 0 || move > 8 || game.board[move] != ' ');

        game.board[move] = MY_PIECE;
        game.currentMove = move;
        game.checkWinner();
        game.print();

        if(!enviar_objeto(client_fd, game)){
            printf("Error al enviar\n");
            break;
        }
    }

    if(game.winner == 1)      printf("Gana X (cliente)!\n");
    else if(game.winner == 2) printf("Gana O (servidor)!\n");
    else if(game.winner == 3) printf("Empate!\n");

    close(client_fd);
    close(server_fd);
    return 0;
}
