#pragma once

namespace DAQ
{
    // 抽象介面：定義所有 DAQ 裝置(不論真實或虛擬)都必須具備的功能
    class IDaqDevice
    {
    public:
        virtual ~IDaqDevice() = default;

        // 初始化裝置/任務
        virtual bool initialize() = 0;

        // 開始擷取
        virtual bool start() = 0;

        // 停止擷取
        virtual bool stop() = 0;
    };
}