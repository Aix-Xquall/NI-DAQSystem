#include "comm/UdpSender.hpp"
#include <iostream>
#include <cstring> // for memset

namespace Comm {

    UdpSender::UdpSender(const std::string& targetIp, int targetPort)
        : m_targetIp(targetIp), m_targetPort(targetPort), m_sockfd(INVALID_SOCKET), m_isInitialized(false) {
    }

    UdpSender::~UdpSender() {
        closeSocket();
        #ifdef _WIN32
            WSACleanup();
        #endif
    }

    bool UdpSender::initialize() {
        #ifdef _WIN32
            // Windows 需先初始化 Winsock
            WSADATA wsaData;
            if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
                std::cerr << "[UDP] WSAStartup failed" << std::endl;
                return false;
            }
        #endif

        // 建立 UDP Socket
        if ((m_sockfd = socket(AF_INET, SOCK_DGRAM, 0)) == INVALID_SOCKET) {
            std::cerr << "[UDP] Could not create socket" << std::endl;
            return false;
        }

        // 設定目標位址結構
        memset(&m_serverAddr, 0, sizeof(m_serverAddr));
        m_serverAddr.sin_family = AF_INET;
        m_serverAddr.sin_port = htons(m_targetPort);
        
        // 轉換 IP 字串
        #ifdef _WIN32
            if (inet_pton(AF_INET, m_targetIp.c_str(), &m_serverAddr.sin_addr) <= 0) {
                std::cerr << "[UDP] Invalid address: " << m_targetIp << std::endl;
                return false;
            }
        #else
            if (inet_aton(m_targetIp.c_str(), &m_serverAddr.sin_addr) == 0) {
                std::cerr << "[UDP] Invalid address: " << m_targetIp << std::endl;
                return false;
            }
        #endif

        m_isInitialized = true;
        std::cout << "[UDP] Initialized. Target: " << m_targetIp << ":" << m_targetPort << std::endl;
        return true;
    }

    bool UdpSender::send(const std::string& data) {
        if (!m_isInitialized) return false;

        // UDP 發送
        int sentBytes = sendto(m_sockfd, data.c_str(), data.length(), 0, 
                               (struct sockaddr *)&m_serverAddr, sizeof(m_serverAddr));

        if (sentBytes == SOCKET_ERROR) {
            std::cerr << "[UDP] Send failed" << std::endl;
            return false;
        }
        return true;
    }

    void UdpSender::closeSocket() {
        if (m_sockfd != INVALID_SOCKET) {
            #ifdef _WIN32
                closesocket(m_sockfd);
            #else
                close(m_sockfd);
            #endif
            m_sockfd = INVALID_SOCKET;
        }
    }

}