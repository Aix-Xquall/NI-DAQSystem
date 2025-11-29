#include "utils/ConfigLoader.hpp"
#include "nlohmann/json.hpp" // 請確保專案中有此標頭檔
#include <fstream>
#include <stdexcept>
#include <iostream>

namespace Utils {

    SystemConfig ConfigLoader::load(const std::string& filePath) {
        SystemConfig sysConfig;
        std::ifstream file("../"+filePath);
        
        if (!file.is_open()) {
            throw std::runtime_error("[Config] Cannot open config file: " + filePath);
        }

        try {
            nlohmann::json j;
            file >> j;

            sysConfig.systemName = j.value("system_name", "DefaultSystem");
            sysConfig.udpIp = j.value("udp_target_ip", "127.0.0.1");
            sysConfig.udpPort = j.value("udp_target_port", 5005);

            if (j.contains("tasks")) {
                for (const auto& tItem : j["tasks"]) {
                    TaskConfig task;
                    task.taskName = tItem.value("task_name", "Unnamed");
                    task.active = tItem.value("active", false);
                    task.sampleRate = tItem.value("sample_rate", 1000.0);

                    // 若 Task 不啟用，直接跳過 (效能優化)
                    if (!task.active) continue;

                    if (tItem.contains("channels")) {
                        for (const auto& cItem : tItem["channels"]) {
                            ChannelConfig ch;
                            ch.deviceName = cItem.value("device_name", "");
                            ch.modelInfo = cItem.value("model_info", "");
                            ch.channelRange = cItem.value("channel_range", "ai0");
                            ch.channelType = cItem.value("channel_type", "Voltage");
                            
                            // [新增] 讀取 Active 欄位，預設為 true
                            ch.active = cItem.value("active", true);

                            ch.minVal = cItem.value("min_val", -10.0);
                            ch.maxVal = cItem.value("max_val", 10.0);

                            // 讀取 IEPE 設定
                            if (cItem.contains("iepe_config")) {
                                auto iItem = cItem["iepe_config"];
                                ch.iepeSettings.current = iItem.value("current", 0.004);
                                ch.iepeSettings.coupling = iItem.value("coupling", "AC");
                            } else {
                                ch.iepeSettings = { 0.0, "DC" }; // 預設無 IEPE
                            }

                            // 讀取 Strain 設定
                            if (cItem.contains("strain_config")) {
                                auto sItem = cItem["strain_config"];
                                ch.strainSettings.gageFactor = sItem.value("gage_factor", 2.0);
                                ch.strainSettings.resistance = sItem.value("resistance", 120.0);
                                ch.strainSettings.poissonRatio = sItem.value("poisson_ratio", 0.3);
                                ch.strainSettings.excitationVal = sItem.value("excitation_val", 2.5);
                            } else {
                                ch.strainSettings = { 2.0, 120.0, 0.3, 2.5 };
                            }

                            if (!ch.deviceName.empty()) {
                                task.channels.push_back(ch);
                            }
                        }
                    }
                    
                    // 只有當 Task 至少有一個 Active 通道時才加入
                    bool hasActiveChannel = false;
                    for(const auto& ch : task.channels) {
                        if(ch.active) { hasActiveChannel = true; break; }
                    }

                    if (hasActiveChannel) {
                        sysConfig.taskConfigs.push_back(task);
                    }
                }
            }
        }
        catch (const nlohmann::json::exception& e) {
            throw std::runtime_error("[Config] JSON Parse Error: " + std::string(e.what()));
        }
        return sysConfig;
    }
}