#pragma once

#include <string>
#include <cstdint>

#ifdef _WIN32
    #include <winsock2.h>
    #include <ws2tcpip.h>
#else
    #include <sys/socket.h>
    #include <arpa/inet.h>
    #include <unistd.h>
    #define INVALID_SOCKET -1
    #define SOCKET_ERROR -1
    typedef int SOCKET;
#endif

namespace Comm {

    class UdpSender {
    public:
        // 建構子：指定目標 IP 和 Port
        UdpSender(const std::string& targetIp, int targetPort);
        ~UdpSender();

        // 初始化 Socket
        bool initialize();

        // 發送字串數據
        bool send(const std::string& data);

    private:
        std::string m_targetIp;
        int m_targetPort;
        SOCKET m_sockfd;
        struct sockaddr_in m_serverAddr;
        bool m_isInitialized;

        void closeSocket();
    };

}