// server.cpp
#include <iostream>
#include <string>
#include <map>
#include <vector>
#include <sstream>
#include <fstream>
#include <algorithm>
#include <cstring>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "Ws2_32.lib")
using socket_t = SOCKET;
#define CLOSESOCK closesocket
#define GET_LAST_ERR WSAGetLastError()
#else
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <unistd.h>
using socket_t = int;
#define INVALID_SOCKET (-1)
#define SOCKET_ERROR   (-1)
#define CLOSESOCK close
#define GET_LAST_ERR errno
#endif

// ---------- Persistent names ----------
static std::map<std::string, std::string> savedNames;   // key: "IP:Port" -> name

static void loadNamesFromFile(const std::string& path = "clients.txt") {
    std::ifstream in(path);
    if (!in) return;
    std::string key, name;
    while (in >> key >> name) savedNames[key] = name;
}

static void saveNamesToFile(const std::string& path = "clients.txt") {
    std::ofstream out(path, std::ios::trunc);
    for (auto& kv : savedNames) out << kv.first << ' ' << kv.second << '\n';
}

// ---------- Broadcast helper ----------
static void broadcast(const std::string& msg,
    const std::map<socket_t, std::string>& liveNames,
    socket_t except = INVALID_SOCKET)
{
    for (auto& kv : liveNames) {
        if (kv.first == except) continue;
#ifdef _WIN32
        send(kv.first, msg.c_str(), (int)msg.size(), 0);
#else
        send(kv.first, msg.c_str(), msg.size(), 0);
#endif
    }
}

// ---------- Main ----------
int main() {
#ifdef _WIN32
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        std::cerr << "WSAStartup failed\n";
        return 1;
    }
#endif

    loadNamesFromFile();

    socket_t server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd == INVALID_SOCKET) {
        std::cerr << "socket() failed: " << GET_LAST_ERR << "\n";
        return 1;
    }

    // Reuse address (best-effort)
    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, (char*)&opt, sizeof(opt));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(0); // 0 => OS chooses a free port

    if (bind(server_fd, (sockaddr*)&addr, sizeof(addr)) == SOCKET_ERROR) {
        std::cerr << "bind() failed: " << GET_LAST_ERR << "\n";
        CLOSESOCK(server_fd);
#ifdef _WIN32
        WSACleanup();
#endif
        return 1;
    }

    // Find assigned port
    socklen_t alen = sizeof(addr);
    if (getsockname(server_fd, (sockaddr*)&addr, &alen) == SOCKET_ERROR) {
        std::cerr << "getsockname() failed: " << GET_LAST_ERR << "\n";
        CLOSESOCK(server_fd);
#ifdef _WIN32
        WSACleanup();
#endif
        return 1;
    }
    int port = ntohs(addr.sin_port);
    std::cout << "Server listening on port: " << port << "\n";

    // Write port to file in working directory
    {
        std::ofstream("C:\\temp\\port.txt") << port;
        std::cout << "Port written to port.txt\n";
    }

    if (listen(server_fd, SOMAXCONN) == SOCKET_ERROR) {
        std::cerr << "listen() failed: " << GET_LAST_ERR << "\n";
        CLOSESOCK(server_fd);
#ifdef _WIN32
        WSACleanup();
#endif
        return 1;
    }

    // Active clients: socket -> name
    std::map<socket_t, std::string> clientNames;

    fd_set master_set, read_set;
    FD_ZERO(&master_set);
    FD_SET(server_fd, &master_set);
    socket_t fdmax = server_fd;

    std::cout << "Waiting for clients...\n";

    while (true) {
        read_set = master_set;
        int ready = select((int)fdmax + 1, &read_set, nullptr, nullptr, nullptr);
        if (ready <= 0) {
            std::cerr << "select() error: " << GET_LAST_ERR << "\n";
            break;
        }

        for (socket_t fd = 0; fd <= fdmax; ++fd) {
            if (!FD_ISSET(fd, &read_set)) continue;

            if (fd == server_fd) {
                // New connection
                sockaddr_in caddr{};
                socklen_t clen = sizeof(caddr);
                socket_t newfd = accept(server_fd, (sockaddr*)&caddr, &clen);
                if (newfd == INVALID_SOCKET) continue;

                FD_SET(newfd, &master_set);
                if (newfd > fdmax) fdmax = newfd;

                // Build client key "IP:Port"
                char ip[INET_ADDRSTRLEN]{};
                inet_ntop(AF_INET, &caddr.sin_addr, ip, sizeof(ip));
                int cport = ntohs(caddr.sin_port);
                std::string key = std::string(ip) + ":" + std::to_string(cport);

                // If known from file, set name immediately and notify others
                auto itSaved = savedNames.find(key);
                if (itSaved != savedNames.end()) {
                    clientNames[newfd] = itSaved->second;
                    std::string join = "[System] " + itSaved->second + " reconnected.";
                    std::cout << join << "\n";
                    broadcast(join, clientNames, newfd);
#ifdef _WIN32
                    send(newfd, join.c_str(), (int)join.size(), 0);
#else
                    send(newfd, join.c_str(), join.size(), 0);
#endif
                }
                else {
                    // Not known yet — we’ll treat the first message as their chosen name
                    clientNames[newfd] = ""; // placeholder until first message
                    std::cout << "New client connected, awaiting name...\n";
#ifdef _WIN32
                    const char* prompt = "[System] Enter your name (press Enter for auto-assign): ";
                    send(newfd, prompt, (int)strlen(prompt), 0);
#else
                    const char* prompt = "[System] Enter your name (press Enter for auto-assign): ";
                    send(newfd, prompt, strlen(prompt), 0);
#endif
                }
            }
            else {
                // From a client
                char buf[1024];
#ifdef _WIN32
                int n = recv(fd, buf, (int)sizeof(buf) - 1, 0);
#else
                int n = recv(fd, buf, sizeof(buf) - 1, 0);
#endif
                if (n <= 0) {
                    // Disconnect
                    std::string who = clientNames[fd].empty() ? "Unknown" : clientNames[fd];
                    std::cout << "[System] " << who << " disconnected.\n";
                    FD_CLR(fd, &master_set);
                    CLOSESOCK(fd);
                    clientNames.erase(fd);
                    continue;
                }
                buf[n] = '\0';
                std::string text(buf);

                // Trim CR/LF
                while (!text.empty() && (text.back() == '\r' || text.back() == '\n')) text.pop_back();

                if (clientNames[fd].empty()) {
                    // First message = desired name (or auto-assign)
                    // Need the client's IP:Port to persist it
                    sockaddr_in caddr{};
                    socklen_t clen = sizeof(caddr);
                    getpeername(fd, (sockaddr*)&caddr, &clen);
                    char ip[INET_ADDRSTRLEN]{};
                    inet_ntop(AF_INET, &caddr.sin_addr, ip, sizeof(ip));
                    int cport = ntohs(caddr.sin_port);
                    std::string key = std::string(ip) + ":" + std::to_string(cport);

                    std::string name = text;
                    if (name.empty()) {
                        name = "User" + std::to_string((int)fd);
                    }

                    clientNames[fd] = name;
                    savedNames[key] = name;
                    saveNamesToFile();

                    std::string join = "[System] " + name + " joined the chat.";
                    std::cout << join << "\n";
                    broadcast(join, clientNames, fd);
#ifdef _WIN32
                    send(fd, join.c_str(), (int)join.size(), 0);
#else
                    send(fd, join.c_str(), join.size(), 0);
#endif
                }
                else {
                    // Normal chat message -> broadcast to others
                    const std::string full = clientNames[fd] + ": " + text;
                    std::cout << full << "\n";
                    broadcast(full, clientNames, fd);
                }
            }
        }
    }

    // Cleanup
    for (auto& kv : clientNames) CLOSESOCK(kv.first);
    CLOSESOCK(server_fd);
#ifdef _WIN32
    WSACleanup();
#endif
    return 0;
}
