#pragma once

#include <queue>
#include <mutex>
#include <condition_variable>
#include <optional> // C++17

namespace Data
{

    template <typename T>
    class SafeQueue
    {
    public:
        SafeQueue() = default;
        ~SafeQueue() = default;

        // 禁止複製
        SafeQueue(const SafeQueue &) = delete;
        SafeQueue &operator=(const SafeQueue &) = delete;

        // 推入資料 (生產者用)
        void push(const T &value)
        {
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                m_queue.push(value);
            }
            m_cond.notify_one(); // 通知等待中的消費者
        }

        // 移動語意推入 (效能較佳)
        void push(T &&value)
        {
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                m_queue.push(std::move(value));
            }
            m_cond.notify_one();
        }

        // 嘗試彈出資料 (非阻塞)
        bool try_pop(T &value)
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            if (m_queue.empty())
            {
                return false;
            }
            value = std::move(m_queue.front());
            m_queue.pop();
            return true;
        }

        // 等待並彈出資料 (阻塞直到有資料)
        // 這是消費者執行緒最常用的方法，可以避免 CPU 空轉 (Busy Waiting)
        void wait_and_pop(T &value)
        {
            std::unique_lock<std::mutex> lock(m_mutex);
            m_cond.wait(lock, [this]
                        { return !m_queue.empty(); });
            value = std::move(m_queue.front());
            m_queue.pop();
        }

        // 檢查是否為空
        bool empty() const
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            return m_queue.empty();
        }

    private:
        std::queue<T> m_queue;
        mutable std::mutex m_mutex;
        std::condition_variable m_cond;
    };

}