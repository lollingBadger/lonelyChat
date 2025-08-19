// client.cpp
#include <iostream>
#include <string>
#include <fstream>
#include <thread>
#include <atomic>
#include <cstring>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "Ws2_32.lib")
using socket_t = SOCKET;
#define CLOSESOCK closesocket
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
#endif

std::atomic<bool> running{ true };

void receiver(socket_t sock) {
    char buf[1024];
    while (running) {
#ifdef _WIN32
        int n = recv(sock, buf, (int)sizeof(buf) - 1, 0);
#else
        int n = recv(sock, buf, sizeof(buf) - 1, 0);
#endif
        if (n <= 0) {
            std::cout << "\n[System] Disconnected.\n";
            running = false;
            break;
        }
        buf[n] = '\0';
        std::string msg(buf);
        std::cout << "\n" << msg << "\n> " << std::flush;
    }
}

int main() {
#ifdef _WIN32
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        std::cerr << "WSAStartup failed\n";
        return 1;
    }
#endif

    // Read port selected by server
    int port = 0;
    {
        std::ifstream in("C:\\temp\\port.txt");
        if (!(in >> port) || port <= 0) {
            std::cerr << "Could not read port from port.txt\n";
#ifdef _WIN32
            WSACleanup();
#endif
            return 1;
        }
    }

    socket_t sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock == INVALID_SOCKET) {
        std::cerr << "socket() failed\n";
#ifdef _WIN32
        WSACleanup();
#endif
        return 1;
    }

    sockaddr_in serv{};
    serv.sin_family = AF_INET;
    serv.sin_port = htons(port);
    // Localhost; change to server IP for LAN/WAN
    if (inet_pton(AF_INET, "127.0.0.1", &serv.sin_addr) <= 0) {
        std::cerr << "inet_pton failed\n";
        CLOSESOCK(sock);
#ifdef _WIN32
        WSACleanup();
#endif
        return 1;
    }

    if (connect(sock, (sockaddr*)&serv, sizeof(serv)) == SOCKET_ERROR) {
        std::cerr << "Connection failed\n";
        CLOSESOCK(sock);
#ifdef _WIN32
        WSACleanup();
#endif
        return 1;
    }

    std::cout << "[System] Connected on port " << port << "\n";
    std::cout << "> " << std::flush;

    std::thread t(receiver, sock);

    // Send name first (empty => server auto-assigns)
    std::string name;
    std::getline(std::cin, name);
#ifdef _WIN32
    send(sock, name.c_str(), (int)name.size(), 0);
#else
    send(sock, name.c_str(), name.size(), 0);
#endif

    // Chat loop
    std::string line;
    while (running) {
        std::cout << "> " << std::flush;
        if (!std::getline(std::cin, line)) break;
        if (!running) break;
#ifdef _WIN32
        send(sock, line.c_str(), (int)line.size(), 0);
#else
        send(sock, line.c_str(), line.size(), 0);
#endif
    }

    running = false;
    CLOSESOCK(sock);
    t.join();
#ifdef _WIN32
    WSACleanup();
#endif
    return 0;
}
