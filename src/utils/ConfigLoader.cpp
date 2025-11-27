#include "utils/ConfigLoader.hpp"
#include "nlohmann/json.hpp" // 只在這裡引入 JSON 庫
#include <fstream>
#include <stdexcept>
#include <iostream>

namespace Utils
{

    SystemConfig ConfigLoader::load(const std::string &filePath)
    {
        SystemConfig sysConfig;
        std::ifstream file(filePath);

        if (!file.is_open())
        {
            throw std::runtime_error("[Config] Cannot open config file: " + filePath);
        }

        try
        {
            // 使用 nlohmann::json 進行解析
            nlohmann::json j;
            file >> j;

            // 1. 讀取系統參數 (使用 .value() 提供預設值)
            sysConfig.systemName = j.value("system_name", "DefaultSystem");
            sysConfig.udpIp = j.value("udp_target_ip", "127.0.0.1");
            sysConfig.udpPort = j.value("udp_target_port", 5005);

            // 2. 讀取 DAQ 陣列
            if (j.contains("daqs") && j["daqs"].is_array())
            {
                for (const auto &item : j["daqs"])
                {
                    DaqConfig daq;

                    daq.deviceName = item.value("device_name", "");
                    daq.active = item.value("active", false);
                    daq.sampleRate = item.value("sample_rate", 1000.0);
                    daq.channelRange = item.value("channel_range", "ai0");
                    daq.channelCount = item.value("channel_count", 1);
                    daq.minVoltage = item.value("min_voltage", -10.0);
                    daq.maxVoltage = item.value("max_voltage", 10.0);

                    // 只加入啟用且名稱不為空的 DAQ
                    if (daq.active && !daq.deviceName.empty())
                    {
                        sysConfig.daqConfigs.push_back(daq);
                    }
                }
            }
        }
        catch (const nlohmann::json::exception &e)
        {
            // 捕捉 JSON 格式錯誤
            throw std::runtime_error("[Config] JSON Parse Error: " + std::string(e.what()));
        }

        return sysConfig;
    }

}