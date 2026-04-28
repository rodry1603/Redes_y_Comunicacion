#include <iostream>
#include <unistd.h>
#include <cstring>
#include <cerrno>
#include <netinet/in.h>
#include <sys/socket.h>
#include <signal.h>

#define PORT 8080
#define BUFFER_SIZE 8   // pequeño para forzar fragmentación

void handle_client(int client_socket) {
    char buffer[BUFFER_SIZE];
    std::string acumulador;

    while (true) {
        ssize_t bytes = recv(client_socket, buffer, BUFFER_SIZE, 0);

        if (bytes > 0) {
            // Simular latencia
            usleep(200000);

            std::string fragmento(buffer, bytes);
            std::cout << "[Fragmento]: " << fragmento << std::endl;

            acumulador += fragmento;

            size_t pos;
            while ((pos = acumulador.find('\n')) != std::string::npos) {
                std::string mensaje = acumulador.substr(0, pos);
                acumulador.erase(0, pos + 1);

                std::cout << "[Mensaje completo]: " << mensaje << std::endl;

                std::string respuesta = "ACK: " + mensaje + "\n";

                ssize_t sent = send(client_socket,
                                    respuesta.c_str(),
                                    respuesta.size(),
                                    MSG_NOSIGNAL); // evita SIGPIPE

                if (sent < 0) {
                    std::cerr << "Error en send: " << strerror(errno) << std::endl;
                    return;
                }
            }
        }
        else if (bytes == 0) {
            std::cout << "Cliente cerró la conexión (EOF)\n";
            break;
        }
        else {
            if (errno == EINTR) {
                continue; // reintentar
            }

            if (errno == ECONNRESET) {
                std::cout << "Cliente se desconectó abruptamente\n";
            } else {
                std::cerr << "Error en recv: " << strerror(errno) << std::endl;
            }
            break;
        }
    }

    close(client_socket);
    std::cout << "Conexión cerrada\n";
}

int main() {
    // Evitar que el proceso muera por SIGPIPE
    signal(SIGPIPE, SIG_IGN);

    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        perror("socket");
        return 1;
    }

    // Reutilizar puerto
    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(PORT);

    if (bind(server_fd, (sockaddr*)&address, sizeof(address)) < 0) {
        perror("bind");
        return 1;
    }

    if (listen(server_fd, 5) < 0) {
        perror("listen");
        return 1;
    }

    std::cout << "Servidor escuchando en puerto " << PORT << std::endl;

    while (true) {
        sockaddr_in client_addr{};
        socklen_t addrlen = sizeof(client_addr);

        int client_socket = accept(server_fd,
                                   (sockaddr*)&client_addr,
                                   &addrlen);

        if (client_socket < 0) {
            if (errno == EINTR) continue;
            perror("accept");
            continue;
        }

        std::cout << "Cliente conectado\n";

        // Versión simple: un cliente a la vez
        // (puedes extender a threads luego)
        handle_client(client_socket);
    }

    close(server_fd);
    return 0;
}