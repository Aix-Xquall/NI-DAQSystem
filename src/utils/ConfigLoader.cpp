#include "utils/ConfigLoader.hpp"
#include "nlohmann/json.hpp"
#include <fstream>
#include <stdexcept>
#include <iostream>

namespace Utils
{

    SystemConfig ConfigLoader::load(const std::string &filePath)
    {
        // 嘗試開啟檔案 (相容 build 目錄與專案根目錄)
        std::ifstream file(filePath);
        if (!file.is_open())
        {
            file.open("../" + filePath);
            if (!file.is_open())
            {
                throw std::runtime_error("[Config] Cannot open config file: " + filePath);
            }
        }

        try
        {
            nlohmann::json j;
            file >> j;

            SystemConfig sysConfig;
            sysConfig.systemName = j.value("system_name", "DefaultSystem");
            sysConfig.udpIp = j.value("udp_target_ip", "127.0.0.1");
            sysConfig.udpPort = j.value("udp_target_port", 5005);

            if (j.contains("tasks"))
            {
                for (const auto &tItem : j["tasks"])
                {
                    TaskConfig task;
                    task.taskName = tItem.value("task_name", "Unnamed");
                    task.active = tItem.value("active", false);
                    task.sampleRate = tItem.value("sample_rate", 1000.0);

                    if (!task.active)
                        continue;

                    if (tItem.contains("channels"))
                    {
                        for (const auto &cItem : tItem["channels"])
                        {
                            ChannelConfig ch;
                            ch.deviceName = cItem.value("device_name", "");
                            ch.modelInfo = cItem.value("model_info", "");
                            ch.channelRange = cItem.value("channel_range", "ai0");
                            ch.channelType = cItem.value("channel_type", "Voltage");
                            ch.active = cItem.value("active", true);
                            ch.minVal = cItem.value("min_val", -10.0);
                            ch.maxVal = cItem.value("max_val", 10.0);

                            // --- IEPE ---
                            if (cItem.contains("iepe_config"))
                            {
                                auto iItem = cItem["iepe_config"];
                                ch.iepeSettings.current = iItem.value("current", 0.004);
                                ch.iepeSettings.coupling = iItem.value("coupling", "AC");
                            }
                            else
                            {
                                ch.iepeSettings = {0.0, "DC"};
                            }

                            // --- Strain ---
                            if (cItem.contains("strain_config"))
                            {
                                auto sItem = cItem["strain_config"];
                                ch.strainSettings.gageFactor = sItem.value("gage_factor", 2.0);
                                ch.strainSettings.resistance = sItem.value("resistance", 120.0);
                                ch.strainSettings.poissonRatio = sItem.value("poisson_ratio", 0.3);
                                ch.strainSettings.excitationVal = sItem.value("excitation_val", 2.5);
                            }
                            else
                            {
                                ch.strainSettings = {2.0, 120.0, 0.3, 2.5};
                            }

                            // --- [新增] Moving Average ---
                            if (cItem.contains("moving_avg"))
                            {
                                auto avgItem = cItem["moving_avg"];
                                ch.avgSettings.active = avgItem.value("active", false);
                                ch.avgSettings.windowSize = avgItem.value("window_size", 1);
                                if (ch.avgSettings.windowSize < 1)
                                    ch.avgSettings.windowSize = 1;
                            }
                            else
                            {
                                ch.avgSettings = {false, 1};
                            }

                            // --- [新增] FFT ---
                            if (cItem.contains("fft"))
                            {
                                auto fftItem = cItem["fft"];
                                ch.fftSettings.active = fftItem.value("active", false);
                                ch.fftSettings.windowType = fftItem.value("window_type", "Hann");
                                ch.fftSettings.points = fftItem.value("points", 1024);
                            }
                            else
                            {
                                ch.fftSettings = {false, "Hann", 1024};
                            }

                            if (!ch.deviceName.empty())
                            {
                                task.channels.push_back(ch);
                            }
                        }
                    }

                    // 檢查是否有啟用的通道
                    bool hasActiveChannel = false;
                    for (const auto &ch : task.channels)
                    {
                        if (ch.active)
                        {
                            hasActiveChannel = true;
                            break;
                        }
                    }

                    if (hasActiveChannel)
                    {
                        sysConfig.taskConfigs.push_back(task);
                    }
                }
            }
            return sysConfig;
        }
        catch (const nlohmann::json::exception &e)
        {
            throw std::runtime_error("[Config] JSON Parse Error: " + std::string(e.what()));
        }
    }
}