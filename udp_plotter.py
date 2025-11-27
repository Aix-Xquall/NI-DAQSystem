import socket
import matplotlib.pyplot as plt
from collections import deque

# 設定
UDP_IP = "127.0.0.1"
UDP_PORT = 5005
BUFFER_SIZE = 1024  # UDP buffer

# 建立 Socket
sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
sock.bind((UDP_IP, UDP_PORT))
print(f"Listening on {UDP_IP}:{UDP_PORT}...")

# 繪圖資料緩衝 (顯示最近 100 點)
data_buffer = deque(maxlen=100)
plt.ion() # 開啟互動模式
fig, ax = plt.subplots()
line, = ax.plot([], [])
ax.set_ylim(-11, 11) # 假設電壓範圍 +/- 10V
ax.set_xlim(0, 100)
ax.set_title("Real-time DAQ Data (UDP Stream)")
ax.grid(True)

try:
    while True:
        data, addr = sock.recvfrom(BUFFER_SIZE)
        raw_str = data.decode('utf-8')
        
        # 解析 CSV
        # 格式: DeviceName, Timestamp, SampleRate, Count, Data1, Data2...
        parts = raw_str.split(',')
        
        if len(parts) > 4:
            device_name = parts[0]
            # 簡單起見，我們只取這包數據的第一個數值來繪圖
            # parts[4] 是第一個數據點
            try:
                val = float(parts[4])
                data_buffer.append(val)
                
                # 更新圖表
                line.set_ydata(list(data_buffer))
                line.set_xdata(range(len(data_buffer)))
                
                # 為了效能，每接收 10 次才重繪一次，或者依賴 flush_events
                fig.canvas.draw()
                fig.canvas.flush_events()
                
                print(f"[{device_name}] Value: {val:.4f}")
                
            except ValueError:
                pass

except KeyboardInterrupt:
    print("Stopped by user")
finally:
    sock.close()