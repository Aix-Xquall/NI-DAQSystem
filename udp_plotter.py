import socket
import matplotlib.pyplot as plt
import numpy as np # 需要 numpy 來處理矩陣
from collections import deque

# 設定
UDP_IP = "127.0.0.1"
UDP_PORT = 5005
BUFFER_SIZE = 65535 # 增加 Buffer 大小以應對多通道數據

sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
sock.bind((UDP_IP, UDP_PORT))
print(f"Listening on {UDP_IP}:{UDP_PORT}...")

# 繪圖緩衝區字典 (key: device_name, value: data_queues)
device_buffers = {} 
colors = ['r', 'g', 'b', 'c', 'm', 'y', 'k'] # 通道顏色池

plt.ion()
fig, ax = plt.subplots()
ax.set_title("Real-time Distributed DAQ (Auto-Config)")
ax.set_ylabel("Voltage (V)")
ax.grid(True)

# 儲存繪圖物件
lines = {} # key: device_name_channel_index

try:
    while True:
        data, addr = sock.recvfrom(BUFFER_SIZE)
        raw_str = data.decode('utf-8')
        parts = raw_str.split(',')
        
        # 格式: DeviceName(0), Timestamp(1), Rate(2), ChannelCount(3), TotalPoints(4), Data(5...N)
        if len(parts) > 5:
            dev_name = parts[0]
            try:
                ch_count = int(parts[3])
                data_vals = [float(x) for x in parts[5:]]
                
                # 初始化該裝置的繪圖線條 (如果第一次收到)
                if dev_name not in device_buffers:
                    device_buffers[dev_name] = [deque(maxlen=100) for _ in range(ch_count)]
                    for i in range(ch_count):
                        line_id = f"{dev_name}_ch{i}"
                        line, = ax.plot([], [], label=line_id, color=colors[i % len(colors)])
                        lines[line_id] = line
                    ax.legend(loc='upper right')

                # 解析交錯數據 (De-interleave)
                # data_vals 排列為: ch0, ch1, ch2, ch0, ch1, ch2...
                for i in range(len(data_vals)):
                    ch_idx = i % ch_count # 計算這是第幾個通道
                    val = data_vals[i]
                    device_buffers[dev_name][ch_idx].append(val)

                # 更新繪圖 (只更新這個裝置的線條)
                for i in range(ch_count):
                    line_id = f"{dev_name}_ch{i}"
                    if line_id in lines:
                        y_data = list(device_buffers[dev_name][i])
                        lines[line_id].set_ydata(y_data)
                        lines[line_id].set_xdata(range(len(y_data)))
                
                # 自動調整 Y 軸範圍 (可選)
                ax.relim()
                ax.autoscale_view(scalex=False, scaley=True)
                
                fig.canvas.flush_events()

            except ValueError as e:
                print(f"Parse error: {e}")

except KeyboardInterrupt:
    print("Stopped")
finally:
    sock.close()