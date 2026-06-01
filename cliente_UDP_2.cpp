// g++ client.cpp -o client -pthread

#include <iostream>
#include <fstream>
#include <thread>
#include <mutex>
#include <map>
#include <vector>
#include <algorithm>
#include <cstring>
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

struct FragmentData {
    int sequence;
    string payload;
};

mutex fragmentsMutex;
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

string readFile(const string &filename) {
    ifstream file(filename, ios::binary);
    if (!file) return "";
    return string(
        (istreambuf_iterator<char>(file)),
        istreambuf_iterator<char>()
    );
}

void sendDatagram(
    int sockfd, sockaddr_in serverAddr,
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

    sendto(sockfd, datagram, DATAGRAM_SIZE, 0,
           (sockaddr *)&serverAddr, sizeof(serverAddr));

    cout << "WRITE:>>>" << datagram << "<<<" << endl;
}

vector<string> splitPayload(const string &data) {
    vector<string> chunks;
    int offset = 0;
    while (offset < (int)data.size()) {
        int size = min(PAYLOAD_SIZE, (int)data.size() - offset);
        chunks.push_back(data.substr(offset, size));
        offset += size;
    }
    return chunks;
}

void sendMessage(int sockfd, sockaddr_in serverAddr, const string &data) {
    if ((int)data.size() <= PAYLOAD_SIZE) {
        string payload = data;
        payload.append(PAYLOAD_SIZE - payload.size(), '#');
        sendDatagram(sockfd, serverAddr, "11", 0, payload);
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
        sendDatagram(sockfd, serverAddr, flag, i, payload);
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

void parseProtocol(const string &data) {
    int idx = 0;
    char action = data[idx++];

    int nickSize = stoi(data.substr(idx, 3)); idx += 3;
    string nickname = data.substr(idx, nickSize); idx += nickSize;

    int destSize = stoi(data.substr(idx, 3)); idx += 3;
    string destination = data.substr(idx, destSize); idx += destSize;

    int msgSize = stoi(data.substr(idx, 5)); idx += 5;
    string message = data.substr(idx, msgSize); idx += msgSize;

    int fileNameSize = stoi(data.substr(idx, 11)); idx += 11;
    string filename = data.substr(idx, fileNameSize); idx += fileNameSize;

    int fileSize = stoi(data.substr(idx, 20)); idx += 20;
    string fileData = data.substr(idx, fileSize);

    cout << "\n=================================\n";

    if (action == 'K') {
        cout << "[OK] " << message << "\n";
        cout << "=================================\n";
        return;
    }
    if (action == 'E') {
        cout << "[ERROR] " << message << "\n";
        cout << "=================================\n";
        return;
    }
    if (action == 't') {
        cout << "--- Clientes conectados ---\n";
        string list = message;
        size_t start = 0;
        while (start < list.size()) {
            size_t comma = list.find(',', start);
            string entry = list.substr(start,
                comma == string::npos ? string::npos : comma - start);
            if (!entry.empty()) cout << "  - " << entry << "\n";
            if (comma == string::npos) break;
            start = comma + 1;
        }
        cout << "---------------------------\n";
        cout << "=================================\n";
        return;
    }

    cout << "FROM: " << nickname << endl;
    cout << "ACTION: " << action << endl;

    if (action == 'M' || action == 'B')
        cout << "MESSAGE: " << message << endl;

    if (action == 'F') {
        cout << "FILE RECEIVED: " << filename << endl;
        cout << "FILE SIZE: " << fileData.size() << " bytes" << endl;
        ofstream outFile("received_" + filename, ios::binary);
        outFile.write(fileData.c_str(), fileData.size());
        outFile.close();
        cout << "FILE SAVED AS: received_" << filename << endl;
    }

    cout << "=================================\n";
}

void receiveMessages(int sockfd) {
    while (true) {
        char buffer[DATAGRAM_SIZE];
        sockaddr_in senderAddr{};
        socklen_t len = sizeof(senderAddr);

        int received = recvfrom(sockfd, buffer, DATAGRAM_SIZE, 0,
                                (sockaddr *)&senderAddr, &len);
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

        string senderKey = string(inet_ntoa(senderAddr.sin_addr))
                           + ":" + to_string(ntohs(senderAddr.sin_port));

        if (flag == "11" && sequence == 0) {
            payload = trimRight(payload, '#');
            parseProtocol(payload);
            continue;
        }

        lock_guard<mutex> lock(fragmentsMutex);

        if (flag == "01") {
            fragmentBuffer[senderKey].clear();
            fragmentBuffer[senderKey].push_back({sequence, payload});
        } else if (flag == "00") {
            fragmentBuffer[senderKey].push_back({sequence, payload});
        } else if (flag == "11") {
            payload = trimRight(payload, '@');
            fragmentBuffer[senderKey].push_back({sequence, payload});

            auto &fragments = fragmentBuffer[senderKey];
            sort(fragments.begin(), fragments.end(),
                 [](const FragmentData &a, const FragmentData &b) {
                     return a.sequence < b.sequence;
                 });

            string fullData;
            for (auto &f : fragments) fullData += f.payload;

            parseProtocol(fullData);
            fragmentBuffer[senderKey].clear();
        }
    }
}

int main() {
    int sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd < 0) { cerr << "Socket creation failed.\n"; return 1; }

    sockaddr_in clientAddr{};
    clientAddr.sin_family      = AF_INET;
    clientAddr.sin_addr.s_addr = INADDR_ANY;
    clientAddr.sin_port        = htons(0);
    if (bind(sockfd, (sockaddr *)&clientAddr, sizeof(clientAddr)) < 0) {
        cerr << "Bind failed.\n"; return 1;
    }

    sockaddr_in serverAddr{};
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port   = htons(PORT);
    inet_pton(AF_INET, "172.31.215.158", &serverAddr.sin_addr);

    thread receiverThread(receiveMessages, sockfd);
    receiverThread.detach();

    string nickname;
    cout << "Enter nickname: ";
    getline(cin, nickname);

    sendMessage(sockfd, serverAddr,
                buildProtocol('L', nickname, "", "", "", ""));
    usleep(200000);

    while (true) {
        cout << "\n=========================\n";
        cout << "[M] Private Message\n";
        cout << "[B] Broadcast\n";
        cout << "[F] Send File\n";
        cout << "[T] List clients\n";
        cout << "[O] Logout\n";
        cout << "Choice: ";

        char action;
        cin >> action;
        cin.ignore();

        string destination, message, filename, fileData;

        if (action == 'O') {
            sendMessage(sockfd, serverAddr,
                        buildProtocol('O', nickname, "", "", "", ""));
            usleep(100000);
            break;
        }
        if (action == 'T') {
            sendMessage(sockfd, serverAddr,
                        buildProtocol('T', nickname, "", "", "", ""));
            continue;
        }
        if (action == 'M' || action == 'F') {
            cout << "Destination: ";
            getline(cin, destination);
        }
        if (action == 'M' || action == 'B') {
            cout << "Message: ";
            getline(cin, message);
        }
        if (action == 'F') {
            cout << "Filename (image or text): ";
            getline(cin, filename);
            fileData = readFile(filename);
            if (fileData.empty()) { cout << "Cannot read file.\n"; continue; }
            cout << "File size: " << fileData.size() << " bytes\n";
        }

        sendMessage(sockfd, serverAddr,
                    buildProtocol(action, nickname, destination,
                                  message, filename, fileData));
        cout << "Data sent successfully.\n";
    }

    close(sockfd);
    return 0;
}