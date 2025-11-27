#pragma once

#include <string>
#include <vector>

namespace Utils
{

    // 定義單一 DAQ 設定結構
    struct DaqConfig
    {
        std::string deviceName;   // 裝置名稱 e.g., "cDAQ1Mod1"
        bool active;              // 是否啟用
        double sampleRate;        // 取樣率 e.g., 50000.0
        std::string channelRange; // 通道範圍 e.g., "ai0:1"
        int channelCount;         // 通道數量 e.g., 2
        double minVoltage;        // 電壓下限 e.g., -10.0
        double maxVoltage;        // 電壓上限 e.g., 10.0
    };

    // 定義系統整體設定結構
    struct SystemConfig
    {
        std::string systemName;
        std::string udpIp;
        int udpPort;
        std::vector<DaqConfig> daqConfigs;
    };

    class ConfigLoader
    {
    public:
        // 靜態方法：讀取 JSON 並回傳 SystemConfig
        // 若讀取失敗或格式錯誤，會拋出 std::runtime_error
        static SystemConfig load(const std::string &filePath);
    };

}