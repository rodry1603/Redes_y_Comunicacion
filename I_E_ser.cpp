#include <iostream>
#include <unistd.h>
#include <cstring>
#include <cerrno>
#include <netinet/in.h>
#include <sys/socket.h>
#include <signal.h>
#include <cstdint>

#define PORT 8080
#define BUFFER_SIZE 8   // pequeño para forzar fragmentación

#define HEADER_SIZE 10

//lee exactamente n bytes haciendo loop
ssize_t recv_exact(int sock, char* buf, size_t n) {
    size_t total = 0;
    while (total < n) {
        ssize_t r = recv(sock, buf + total, n - total, 0);
        if (r > 0) {
            total += r;
        } else if (r == 0) {
            return (total == 0) ? 0 : -1;
        } else {
            if (errno == EINTR) continue;
            return -1;
        }
    }
    return (ssize_t)total;
}

//parsing
bool validate_protocol(const char* header, const char* carga, uint32_t size) {
    if (header[5] != 'J') {
        std::cerr << "[BASURA] pos 6 esperaba 'J', recibio: '" << header[5] << "'\n";
        return false;
    }
    if (header[6] != '{') {
        std::cerr << "[BASURA] pos 7 esperaba '{', recibio: '" << header[6] << "'\n";
        return false;
    }
    if (carga[size - 1] != '}') {
        std::cerr << "[BASURA] ultimo byte esperaba '}', recibio: '" << carga[size-1] << "'\n";
        return false;
    }
    return true;
}


void handle_client(int client_socket) {
    char buffer[BUFFER_SIZE];
    std::string acumulador;

    while (true) {
        char header[HEADER_SIZE];
        std::cout << "[Servidor esperando datos del cliente...]\n";  
        ssize_t bytes = recv_exact(client_socket, header, HEADER_SIZE);

        if (bytes > 0) {
            // Simular latencia  
            usleep(200000);

          
            uint32_t carga_size = 0;
            carga_size |= (uint8_t)header[0] << 24;
            carga_size |= (uint8_t)header[1] << 16;
            carga_size |= (uint8_t)header[2] << 8;
            carga_size |= (uint8_t)header[3];

            if (carga_size == 0 || carga_size > 1024 * 1024) {
                std::cerr << "[BASURA] Tamaño inválido: " << carga_size << " — descartando, sigo esperando\n";
                continue;  
            }

            
            char* carga = new char[carga_size + 1];
            carga[0] = header[6]; 

            if (carga_size > 1) {
                ssize_t pr = recv_exact(client_socket, carga + 1, carga_size - 1);
                if (pr == 0) {
                    std::cout << "Cliente cerró la conexión (EOF)\n";
                    delete[] carga;
                    break;
                }
                if (pr < 0) {
                   
                    if (errno == ECONNRESET || errno == EPIPE) {
                        std::cout << "======================================\n";
                        std::cout << "[KILL -9] Cliente eliminado abruptamente (recv = " << pr << ", errno = ECONNRESET)\n";
                        std::cout << "======================================\n";
                    } else {
                        std::cerr << "Error en recv carga: " << strerror(errno) << std::endl;
                    }
                    delete[] carga;
                    break;
                }
            }
            carga[carga_size] = '\0';

            // mostrar fragmento
            std::string fragmento(carga, carga_size);
            std::cout << "[Fragmento]: " << fragmento << std::endl;

            
            if (!validate_protocol(header, carga, carga_size)) {
                delete[] carga;
                std::cout << "[Servidor descarto basura, sigue esperando...]\n"; 
                continue;  
            }

            // acumular y buscar mensaje completo
            acumulador += fragmento;
            delete[] carga;

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
            // desconexión normal (recv = 0)
            std::cout << "Cliente cerró la conexión (EOF)\n";
            break;
        }
        else {
            if (errno == EINTR) {
                continue; // reintentar
            }

            
            if (errno == ECONNRESET) {
                std::cout << "======================================\n";
                std::cout << "[KILL -9] Cliente eliminado abruptamente (recv = " << bytes << ", errno = ECONNRESET)\n";
                std::cout << "======================================\n";
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
