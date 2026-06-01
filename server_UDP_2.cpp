// g++ server.cpp -o server -pthread

#include <iostream>
#include <thread>
#include <mutex>
#include <map>
#include <vector>
#include <cstring>
#include <algorithm>
#include <arpa/inet.h>
#include <unistd.h>

using namespace std;

constexpr int PORT          = 9000;
constexpr int DATAGRAM_SIZE = 500;
constexpr int FLAG_SIZE     = 2;
constexpr int SEQ_SIZE      = 4;
constexpr int CSUM_SIZE     = 5;
constexpr int HEADER_SIZE   = FLAG_SIZE + SEQ_SIZE + CSUM_SIZE;
constexpr int PAYLOAD_SIZE  = DATAGRAM_SIZE - HEADER_SIZE;

struct ClientInfo { sockaddr_in address; };

struct FragmentData {
    int sequence;
    string payload;
};

mutex clientsMutex;
mutex fragmentsMutex;

map<string, ClientInfo> clients;
map<string, vector<FragmentData>> fragmentBuffer;

uint32_t calcChecksum(const string &data) {
    uint32_t sum = 0;
    for (unsigned char c : data) sum += c;
    return sum % 100000;
}

string padNumber(int number, int width) {
    string s = to_string(number);
    while ((int)s.size() < width) s = "0" + s;
    return s;
}

string trimRight(const string &s, char c) {
    size_t end = s.find_last_not_of(c);
    if (end == string::npos) return "";
    return s.substr(0, end + 1);
}

string buildClientKey(sockaddr_in addr) {
    return string(inet_ntoa(addr.sin_addr))
           + ":" + to_string(ntohs(addr.sin_port));
}

void printConnectedClients() {
    lock_guard<mutex> lock(clientsMutex);
    cout << "\n CONNECTED CLIENTS \n";
    if (clients.empty()) {
        cout << "No connected clients.\n";
    } else {
        int count = 1;
        for (const auto &client : clients) {
            sockaddr_in addr = client.second.address;
            cout << count++ << ". Nickname: " << client.first
                 << " | IP: "   << inet_ntoa(addr.sin_addr)
                 << " | Port: " << ntohs(addr.sin_port) << endl;
        }
    }
    
}

void sendDatagram(
    int sockfd, sockaddr_in addr,
    const string &flag, int sequence,
    const string &payload
) {
    char datagram[DATAGRAM_SIZE];
    memset(datagram, 0, DATAGRAM_SIZE);

    memcpy(datagram, flag.c_str(), FLAG_SIZE);

    string seq = padNumber(sequence, SEQ_SIZE);
    memcpy(datagram + FLAG_SIZE, seq.c_str(), SEQ_SIZE);

    uint32_t csum = calcChecksum(payload);
    string csumStr = padNumber((int)csum, CSUM_SIZE);
    memcpy(datagram + FLAG_SIZE + SEQ_SIZE, csumStr.c_str(), CSUM_SIZE);

    memcpy(datagram + HEADER_SIZE, payload.c_str(), payload.size());

    cout << "WRITE:>>>" << datagram << "<<<" << endl;

    sendto(sockfd, datagram, DATAGRAM_SIZE, 0,
           (sockaddr *)&addr, sizeof(addr));
}

vector<string> splitPayload(const string &data) {
    vector<string> chunks;
    int offset = 0;
    while (offset < (int)data.size()) {
        int chunkSize = min(PAYLOAD_SIZE, (int)data.size() - offset);
        chunks.push_back(data.substr(offset, chunkSize));
        offset += chunkSize;
    }
    return chunks;
}

void sendMessage(int sockfd, sockaddr_in addr, const string &data) {
    if ((int)data.size() <= PAYLOAD_SIZE) {
        string payload = data;
        payload.append(PAYLOAD_SIZE - payload.size(), '#');
        sendDatagram(sockfd, addr, "11", 0, payload);
        return;
    }
    vector<string> chunks = splitPayload(data);
    for (int i = 0; i < (int)chunks.size(); i++) {
        string flag = "00";
        if (i == 0) flag = "01";
        else if (i == (int)chunks.size() - 1) flag = "11";
        string payload = chunks[i];
        if (i == (int)chunks.size() - 1 && (int)payload.size() < PAYLOAD_SIZE)
            payload.append(PAYLOAD_SIZE - payload.size(), '@');
        sendDatagram(sockfd, addr, flag, i, payload);
    }
}

string buildProtocol(
    char action,
    const string &nickname,
    const string &destination,
    const string &message,
    const string &filename,
    const string &fileData
) {
    string protocol;
    protocol += action;
    protocol += padNumber(nickname.size(), 3);    protocol += nickname;
    protocol += padNumber(destination.size(), 3); protocol += destination;
    protocol += padNumber(message.size(), 5);     protocol += message;
    protocol += padNumber(filename.size(), 11);   protocol += filename;
    protocol += padNumber(fileData.size(), 20);   protocol += fileData;
    return protocol;
}

void parseProtocol(
    const string &data,
    char &action, string &nickname, string &destination,
    string &message, string &filename, string &fileData
) {
    int idx = 0;
    action = data[idx++];
    int nickSize = stoi(data.substr(idx, 3)); idx += 3;
    nickname = data.substr(idx, nickSize);    idx += nickSize;
    int destSize = stoi(data.substr(idx, 3)); idx += 3;
    destination = data.substr(idx, destSize); idx += destSize;
    int msgSize = stoi(data.substr(idx, 5));  idx += 5;
    message = data.substr(idx, msgSize);      idx += msgSize;
    int fileNameSize = stoi(data.substr(idx, 11)); idx += 11;
    filename = data.substr(idx, fileNameSize); idx += fileNameSize;
    int fileSize = stoi(data.substr(idx, 20)); idx += 20;
    fileData = data.substr(idx, fileSize);
}

string buildClientList() {
    string list;
    bool first = true;
    for (auto &c : clients) {
        if (!first) list += ",";
        list += c.first;
        first = false;
    }
    return list;
}

void processMessage(int sockfd, const string &fullData, sockaddr_in senderAddr) {
    char action;
    string nickname, destination, message, filename, fileData;
    parseProtocol(fullData, action, nickname, destination,
                  message, filename, fileData);

    cout << "\n MESSAGE \n";
    cout << "FROM: " << nickname << " | ACTION: " << action << endl;

    if (action == 'L') {
        clientsMutex.lock();
        bool exists = clients.count(nickname) > 0;
        if (!exists) clients[nickname] = {senderAddr};
        clientsMutex.unlock();

        if (exists) {
            sendMessage(sockfd, senderAddr,
                buildProtocol('E', "server", nickname, "Nickname ya en uso", "", ""));
            cout << "LOGIN REJECTED: " << nickname << "\n";
        } else {
            sendMessage(sockfd, senderAddr,
                buildProtocol('K', "server", nickname, "OK", "", ""));
            cout << "LOGIN OK: " << nickname << "\n";
            printConnectedClients();
        }
        return;
    }

    if (action == 'O') {
        clientsMutex.lock();
        clients.erase(nickname);
        clientsMutex.unlock();

        sendMessage(sockfd, senderAddr,
            buildProtocol('K', "server", nickname, "OK", "", ""));
        cout << "LOGOUT: " << nickname << "\n";
        printConnectedClients();
        
        return;
    }

    if (action == 'T') {
        clientsMutex.lock();
        string list = buildClientList();
        clientsMutex.unlock();

        sendMessage(sockfd, senderAddr,
            buildProtocol('t', "server", nickname, list, "", ""));
        cout << "LIST sent to: " << nickname << "\n";

        return;
    }

    clientsMutex.lock();
    clients[nickname] = {senderAddr};
    clientsMutex.unlock();

    printConnectedClients();

    if (action == 'M' || action == 'B') cout << "TEXT: " << message << endl;
    if (action == 'F') {
        cout << "FILE: " << filename << endl;
        cout << "FILE SIZE: " << fileData.size() << endl;
    }
    

    if (action == 'B') {
        vector<pair<string, sockaddr_in>> targets;
        clientsMutex.lock();
        for (auto &c : clients)
            if (c.first != nickname)
                targets.push_back({c.first, c.second.address});
        clientsMutex.unlock();

        for (auto &t : targets)
            sendMessage(sockfd, t.second, fullData);

    } else if (action == 'M' || action == 'F') {
        clientsMutex.lock();
        bool found = clients.count(destination) > 0;
        sockaddr_in destAddr;
        if (found) destAddr = clients[destination].address;
        clientsMutex.unlock();

        if (found) {
            sendMessage(sockfd, destAddr, fullData);
        } else {
            sendMessage(sockfd, senderAddr,
                buildProtocol('E', "server", nickname,
                              "Destino " + destination + " no encontrado", "", ""));
        }
    }
}

void receiveThread(int sockfd, string fullData, sockaddr_in clientAddr) {
    processMessage(sockfd, fullData, clientAddr);
}

int main() {
    int sockfd = socket(AF_INET, SOCK_DGRAM, 0);

    sockaddr_in serverAddr{};
    serverAddr.sin_family      = AF_INET;
    serverAddr.sin_port        = htons(PORT);
    serverAddr.sin_addr.s_addr = INADDR_ANY;
    bind(sockfd, (sockaddr *)&serverAddr, sizeof(serverAddr));

    cout << "UDP CHAT SERVER RUNNING on port " << PORT << "\n";

    while (true) {
        char buffer[DATAGRAM_SIZE];
        sockaddr_in clientAddr{};
        socklen_t len = sizeof(clientAddr);

        int received = recvfrom(sockfd, buffer, DATAGRAM_SIZE, 0,
                                (sockaddr *)&clientAddr, &len);
        if (received <= 0) continue;

        cout << "READ:>>>" << buffer << "<<<" << endl;

        string flag(buffer, FLAG_SIZE);
        string seqStr(buffer + FLAG_SIZE, SEQ_SIZE);
        int sequence = stoi(seqStr);

        string csumStr(buffer + FLAG_SIZE + SEQ_SIZE, CSUM_SIZE);
        uint32_t recvCsum = (uint32_t)stoi(csumStr);

        string payload(buffer + HEADER_SIZE, PAYLOAD_SIZE);
        uint32_t calcCsum = calcChecksum(payload);

        if (recvCsum != calcCsum) {
            cout << "CHECKSUM ERROR: recv=" << recvCsum
                 << " calc=" << calcCsum << " — datagram descartado\n";
            continue;
        }

        string clientKey = buildClientKey(clientAddr);

        if (flag == "11" && sequence == 0) {
            payload = trimRight(payload, '#');
            thread t(receiveThread, sockfd, payload, clientAddr);
            t.detach();
            continue;
        }

        lock_guard<mutex> lock(fragmentsMutex);

        if (flag == "01") {
            fragmentBuffer[clientKey].clear();
            fragmentBuffer[clientKey].push_back({sequence, payload});
        } else if (flag == "00") {
            fragmentBuffer[clientKey].push_back({sequence, payload});
        } else if (flag == "11") {
            payload = trimRight(payload, '@');
            fragmentBuffer[clientKey].push_back({sequence, payload});

            auto &fragments = fragmentBuffer[clientKey];
            sort(fragments.begin(), fragments.end(),
                 [](const FragmentData &a, const FragmentData &b) {
                     return a.sequence < b.sequence;
                 });

            string fullData;
            for (auto &f : fragments) fullData += f.payload;

            thread t(receiveThread, sockfd, fullData, clientAddr);
            t.detach();
            fragmentBuffer[clientKey].clear();
        }
    }

    close(sockfd);
    return 0;
}