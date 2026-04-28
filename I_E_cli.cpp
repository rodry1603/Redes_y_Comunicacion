#include <iostream>
#include <unistd.h>
#include <arpa/inet.h>
#include <cstring>
#include <cstdint>

#define PORT 8080


std::string build_frame(const std::string& json) {
    uint32_t carga_size = (uint32_t)json.size();

    char header[10];
    header[0] = (carga_size >> 24) & 0xFF;
    header[1] = (carga_size >> 16) & 0xFF;
    header[2] = (carga_size >>  8) & 0xFF;
    header[3] = (carga_size      ) & 0xFF;
    header[4] = 0x01;
    header[5] = 'J';
    header[6] = json[0];
    header[7] = (json.size() > 1) ? json[1] : 0x00;
    header[8] = (json.size() > 2) ? json[2] : 0x00;
    header[9] = (json.size() > 3) ? json[3] : 0x00;

    std::string frame(header, 10);
    frame += json.substr(1);
    return frame;
}


void send_fragmented(int sock, const std::string& msg) {
    for (size_t i = 0; i < msg.size(); i += 3) {
        std::string chunk = msg.substr(i, 3);

        send(sock, chunk.c_str(), chunk.size(), 0);

        std::cout << "[Enviado fragmento]: ";
        for (unsigned char c : chunk) {
            if (c >= 32 && c < 127) std::cout << c;
            else std::cout << "\\x" << std::hex << (int)c << std::dec;
        }
        std::cout << std::endl;

        usleep(100000); // 100 ms (latencia artificial)
    }
}

int main() {
    int sock = 0;
    sockaddr_in serv_addr;

    sock = socket(AF_INET, SOCK_STREAM, 0);

    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(PORT);

    inet_pton(AF_INET, "172.31.215.158", &serv_addr.sin_addr);

    connect(sock, (sockaddr*)&serv_addr, sizeof(serv_addr));

    std::cout << "Conectado al servidor\n";

    
    std::cout << "\n[Enviando JSON valido #1]\n";
    send_fragmented(sock, build_frame(R"({"tipo":"saludo","mensaje":"Hola servidor"})"));

    sleep(1);

   
    std::cout << "\n[Enviando JSON valido #2]\n";
    send_fragmented(sock, build_frame(R"({"tipo":"datos","valores":[1,2,3],"activo":true})"));

    sleep(1);

    //enviar BASURA 
    std::cout << "\n[Enviando BASURA - sin protocolo correcto]\n";
    std::string garbage = "XXXXXXXXXBASURAXXXXXXXXXXX\n";
    send(sock, garbage.c_str(), garbage.size(), 0);

    sleep(1);


    std::cout << "\n----------------------------------------\n";
    std::cout << "  PID del cliente: " << getpid() << "\n";
    std::cout << "  Desde la terminal 3 ejecuta:\n";
    std::cout << "    kill -9 " << getpid() << "\n";
    std::cout << "  Tienes 60 segundos.\n";
    std::cout << "----------------------------------------\n\n";

    sleep(60);

    // Desconexión inesperada 
    std::cout << "Cerrando cliente abruptamente...\n";
    close(sock);

    return 0;
}
