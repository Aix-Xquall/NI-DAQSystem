import socket
import matplotlib.pyplot as plt
import numpy as np
from collections import deque
import time

# --- 設定 ---
UDP_IP = "127.0.0.1"
UDP_PORT = 5005
BUFFER_SIZE = 65535  # 加大 Buffer 以防封包變大

# --- 初始化 ---
sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
sock.bind((UDP_IP, UDP_PORT))
sock.setblocking(False) # 設定為非阻塞模式

print(f"Listening on {UDP_IP}:{UDP_PORT}...")

plt.ion()
fig, ax = plt.subplots()
ax.set_title("Real-time Distributed DAQ")
ax.set_xlabel("Samples")
ax.set_ylabel("Voltage (V)")
ax.set_ylim(-11, 11)
ax.grid(True)

# 儲存繪圖物件的字典: { "DeviceName_ChIndex": LineObject }
lines = {}
# 儲存數據的字典: { "DeviceName_ChIndex": Deque }
data_buffers = {}
# 顏色庫
colors = ['r', 'g', 'b', 'c', 'm', 'y', 'k']

print("Press Ctrl+C to stop.")

try:
    while True:
        try:
            data, addr = sock.recvfrom(BUFFER_SIZE)
            raw_str = data.decode('utf-8')
            parts = raw_str.split(',')
            
            # 解析 CSV (新格式)
            # 0: Name, 1: Time, 2: Rate, 3: ChCount, 4: TotalPoints, 5...: Data
            if len(parts) > 5:
                dev_name = parts[0]
                ch_count = int(parts[3])
                
                # 解析數據部分
                # 從 index 5 開始是數據
                raw_values = [float(x) for x in parts[5:]]
                
                # 處理多通道數據 (Interleaved: ch0, ch1, ch2, ch0, ch1...)
                for ch_idx in range(ch_count):
                    # 產生唯一識別碼 (Key)
                    key = f"{dev_name}_ch{ch_idx}"
                    
                    # 取出該通道的數據 (Slicing)
                    # raw_values[ch_idx::ch_count] 代表從 ch_idx 開始，每隔 ch_count 取一個值
                    ch_data = raw_values[ch_idx::ch_count]
                    
                    if not ch_data:
                        continue

                    # 初始化線條與 Buffer (如果是第一次收到這個通道)
                    if key not in lines:
                        # 分配顏色 (簡單雜湊)
                        color = colors[len(lines) % len(colors)]
                        line, = ax.plot([], [], label=key, color=color, alpha=0.8)
                        lines[key] = line
                        data_buffers[key] = deque(maxlen=100) # 每個通道保留 100 點
                        ax.legend(loc='upper right', fontsize='small')
                    
                    # 更新數據
                    # 為了 UI 流暢，我們只取這包數據的第一點 (Downsampling)
                    # 實際應用可根據需求全畫
                    data_buffers[key].append(ch_data[0])
                    
                    # 更新圖表物件
                    lines[key].set_ydata(list(data_buffers[key]))
                    lines[key].set_xdata(range(len(data_buffers[key])))

            # 繪圖更新 (控制更新率以免 UI 卡死)
            fig.canvas.draw()
            fig.canvas.flush_events()
            
        except BlockingIOError:
            # 沒有數據時休息一下
            time.sleep(0.01)
        except Exception as e:
            print(f"Error: {e}")

except KeyboardInterrupt:
    print("\nStopped.")
finally:
    sock.close()