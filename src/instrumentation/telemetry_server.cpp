#include "instrumentation/telemetry_server.hpp"
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>
#include <iostream>
#include <vector>
#include <sstream>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
using socket_t = SOCKET;
#define CLOSE_SOCKET closesocket
#define INVALID_SOCKET_VAL INVALID_SOCKET
#define SOCKET_ERROR_VAL SOCKET_ERROR
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
using socket_t = int;
#define CLOSE_SOCKET close;
#define INVALID_SOCKET_VAL -1
#define SOCKET_ERROR_VAL -1
#endif

TelemetryServer::TelemetryServer(EventBus& event_bus, int port) 
    : event_bus_(event_bus), port_(port) {
#ifdef _WIN32
    // Initialize Winsock
    WSADATA wsaData;
    int res = WSAStartup(MAKEWORD(2, 2), &wsaData);
    if (res != 0) {
        spdlog::error("WSAStartup failed with error: {}", res);
    }
#endif
}

TelemetryServer::~TelemetryServer() {
    stop();
#ifdef _WIN32
    WSACleanup();
#endif
}

bool TelemetryServer::start() {
    if (running_) return true;
    
    running_ = true;
    server_thread_ = std::thread(&TelemetryServer::listen_loop, this);
    return true;
}

void TelemetryServer::stop() {
    running_ = false;
    
    // Close the server socket to unblock accept()
    if (server_fd_ != -1) {
#ifdef _WIN32
        closesocket(static_cast<socket_t>(server_fd_));
#else
        close(static_cast<int>(server_fd_));
#endif
        server_fd_ = -1;
    }

    if (server_thread_.joinable()) {
        server_thread_.join();
    }
}

void TelemetryServer::listen_loop() {
    socket_t server_sock = socket(AF_INET, SOCK_STREAM, 0);
    if (server_sock == INVALID_SOCKET_VAL) {
        spdlog::error("Failed to create socket.");
        running_ = false;
        return;
    }
    
    // Set socket options (SO_REUSEADDR)
    int opt = 1;
#ifdef _WIN32
    setsockopt(server_sock, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&opt), sizeof(opt));
#else
    setsockopt(server_sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
#endif

    server_fd_ = server_sock;

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY; // Listen on all interfaces
    address.sin_port = htons(port_);

    if (bind(server_sock, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == SOCKET_ERROR_VAL) {
        spdlog::error("Bind failed on port {}.", port_);
#ifdef _WIN32
        closesocket(server_sock);
#else
        close(server_sock);
#endif
        running_ = false;
        return;
    }

    if (listen(server_sock, 3) == SOCKET_ERROR_VAL) {
        spdlog::error("Listen failed.");
#ifdef _WIN32
        closesocket(server_sock);
#else
        close(server_sock);
#endif
        running_ = false;
        return;
    }

    spdlog::info("Telemetry server listening on port {}", port_);

    while (running_) {
        sockaddr_in client_addr{};
        int addrlen = sizeof(client_addr);
        
        socket_t client_sock = accept(server_sock, reinterpret_cast<sockaddr*>(&client_addr), &addrlen);
        if (client_sock == INVALID_SOCKET_VAL) {
            if (running_) {
                spdlog::warn("Accept error on socket.");
            }
            break;
        }

        client_connected_ = true;
        spdlog::info("Client connected to LLMScope telemetry server.");
        
        handle_client(static_cast<int64_t>(client_sock));
        
        client_connected_ = false;
        spdlog::info("Client disconnected from telemetry server.");
    }
    
#ifdef _WIN32
    closesocket(server_sock);
#endif
    running_ = false;
}

void TelemetryServer::handle_client(int64_t client_socket) {
    socket_t sock = static_cast<socket_t>(client_socket);
    std::vector<char> buffer(65536);
    std::string accumulated_data;

    while (running_) {
        int bytes_received = recv(sock, buffer.data(), static_cast<int>(buffer.size()), 0);
        if (bytes_received <= 0) {
            break; // Connection closed or error
        }

        accumulated_data.append(buffer.data(), bytes_received);

        // Process line-oriented JSON packets
        size_t newline_pos;
        while ((newline_pos = accumulated_data.find('\n')) != std::string::npos) {
            std::string line = accumulated_data.substr(0, newline_pos);
            accumulated_data.erase(0, newline_pos + 1);

            // Trim carriage returns if sent by client
            if (!line.empty() && line.back() == '\r') {
                line.pop_back();
            }

            if (line.empty()) continue;

            try {
                nlohmann::json j = nlohmann::json::parse(line);
                TelemetryEvent event = j.get<TelemetryEvent>();
                event_bus_.publish(event);
            } catch (const std::exception& e) {
                spdlog::error("Error parsing telemetry JSON packet: {}. Line size: {}", e.what(), line.size());
            }
        }
    }

#ifdef _WIN32
    closesocket(sock);
#else
    close(sock);
#endif
}
