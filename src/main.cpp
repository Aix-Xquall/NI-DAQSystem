#include <iostream>
#include <thread>
#include <chrono>
#include <memory>
#include <sstream>
#include <iomanip>
#include <vector>
#include <atomic>

#include "daq/DaqDevice.hpp"
#include "data/SafeQueue.hpp"
#include "data/DataTypes.hpp"
#include "comm/UdpSender.hpp"

// CSV 轉換 helper 函式
// 為了避免 UDP 封包過大，我們增加一個 maxPoints 參數
// 對於 UI 繪圖，我們可能只需要看趨勢，不需要 50kHz 的每一個點
std::string convertToCsv(const Data::RawDataChunk& chunk, size_t maxPoints = 50) {
    std::stringstream ss;
    
    // 格式: DeviceName,Timestamp(ms),SampleRate,NumPoints,Data1,Data2...
    auto now = std::chrono::time_point_cast<std::chrono::milliseconds>(chunk.timestamp);
    auto epoch = now.time_since_epoch().count();

    ss << chunk.deviceName << "," 
       << epoch << "," 
       << chunk.sampleRate << "," 
       << chunk.data.size();

    // 限制輸出的數據點數量，避免 UDP 封包爆掉
    size_t pointsToSend = std::min(chunk.data.size(), maxPoints);
    
    // 設定小數點精度
    ss << std::fixed << std::setprecision(4);

    for (size_t i = 0; i < pointsToSend; ++i) {
        ss << "," << chunk.data[i];
    }
    
    return ss.str();
}

// 消費者執行緒：負責處理數據並發送 UDP
void dataProcessingTask(std::shared_ptr<Data::SafeQueue<Data::RawDataChunk>> queue, 
                        std::atomic<bool>& running) {
    
    // 建立 UDP Sender (Target: 本機 127.0.0.1, Port 5005)
    Comm::UdpSender udpSender("127.0.0.1", 5005);
    
    if (!udpSender.initialize()) {
        std::cerr << "[Consumer] UDP Initialization failed!" << std::endl;
        return;
    }

    std::cout << "[Consumer] Processing thread started. Sending UDP to 127.0.0.1:5005" << std::endl;
    
    Data::RawDataChunk chunk;
    while (running) {
        if (queue->try_pop(chunk)) {
            // 1. 轉成 CSV 格式 (限制最多傳送 20 點供 UI 繪圖)
            std::string csvPacket = convertToCsv(chunk, 20);

            // 2. 透過 UDP 發送
            udpSender.send(csvPacket);

            // (Debug 訊息，可註解掉)
            std::cout << "[UDP Sent] " << chunk.deviceName << " Size: " << csvPacket.size() << " bytes" << std::endl;
        } else {
            // 避免空轉，稍微休眠
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }
    std::cout << "[Consumer] Processing thread stopped." << std::endl;
}

int main()
{
    std::cout << "=== Distributed DAQ System (Multi-threaded) ===" << std::endl;

    // 1. 建立共用的資料佇列 (Thread-Safe)
    auto sharedQueue = std::make_shared<Data::SafeQueue<Data::RawDataChunk>>();

    // 2. 建立 DAQ 物件 (傳入 Queue)
    std::string device1_name = "cDAQ1Mod1"; // NI-9232 (50kHz)
    std::string device2_name = "cDAQ1Mod2"; // NI-9230 (10Hz)

    // NI-9232: 2 channels, 50kHz (每 0.1秒產生 50點 * 2 = 100數據)
    DAQ::DaqDevice daq9232(device1_name, 50.0, 2, sharedQueue);

    // NI-9230: 3 channels, 10Hz (每 0.1秒產生 1點 * 3 = 30數據)
    DAQ::DaqDevice daq9230(device2_name, 10.0, 3, sharedQueue);

    // 3. 初始化
    daq9232.initialize();
    daq9230.initialize();

    // 4. 啟動消費者執行緒
    std::atomic<bool> isSystemRunning(true);
    std::thread consumerThread(dataProcessingTask, sharedQueue, std::ref(isSystemRunning));

    // 5. 啟動 DAQ (生產者)
    daq9232.start();
    daq9230.start();

    // 讓系統跑 5 秒鐘
    std::cout << "\nSystem running for 5 seconds..." << std::endl;
    std::this_thread::sleep_for(std::chrono::seconds(50));

    // 6. 停止系統
    std::cout << "\n--- Stopping System ---" << std::endl;
    daq9232.stop();
    daq9230.stop();

    isSystemRunning = false;
    if (consumerThread.joinable())
    {
        consumerThread.join();
    }

    std::cout << "=== End ===" << std::endl;
    std::cin.get();
    return 0;
}