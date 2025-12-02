#pragma once
#include <vector>
#include <optional>

namespace DSP
{

    /**
     * @brief 區塊平均降頻器 (Block Averaging Downsampler)
     * 負責將高頻訊號透過平均法降至低頻。
     * 例如: WindowSize = 10，則每接收 10 點輸入，計算一次平均值並輸出 1 點。
     */
    class Downsampler
    {
    public:
        explicit Downsampler(int windowSize)
            : m_windowSize(windowSize), m_counter(0), m_accumulator(0.0)
        {
            if (m_windowSize < 1)
                m_windowSize = 1;
        }

        /**
         * @brief 輸入一個原始採樣點
         * @param value 原始數值
         * @return std::optional<double> 若累積滿 WindowSize 則回傳平均值，否則回傳 std::nullopt
         */
        std::optional<double> push(double value)
        {
            // 若 WindowSize 為 1，直接透傳 (Pass-through)
            if (m_windowSize == 1)
                return value;

            m_accumulator += value;
            m_counter++;

            if (m_counter >= m_windowSize)
            {
                double avg = m_accumulator / m_windowSize;
                // 重置計數器與累加器
                m_accumulator = 0.0;
                m_counter = 0;
                return avg;
            }

            return std::nullopt; // 資料不足，尚未產生輸出
        }

        // 重置狀態
        void reset()
        {
            m_counter = 0;
            m_accumulator = 0.0;
        }

    private:
        int m_windowSize;
        int m_counter;
        double m_accumulator;
    };
}