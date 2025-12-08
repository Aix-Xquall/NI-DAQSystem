import socket
import json
import threading
import queue
import time
import sys
import numpy as np
import matplotlib.pyplot as plt
from matplotlib.widgets import Button
from collections import deque

# ================= 設定區 =================
CONFIG_FILE = "DAQ_Settings.json"
UDP_IP = "0.0.0.0"       # 監聽所有網卡
UDP_PORT = 5005
BUFFER_SIZE = 65536      # 64KB UDP Buffer
MAX_FPS = 30             # UI 更新率上限
PLOT_DISPLAY_LIMIT = 50000 # 繪圖優化：時域模式下最多顯示點數
MAX_BUFFER_SEC = 20.0    # 時域模式下記憶體保留秒數
# ==========================================

class SystemMapper:
    """
    負責解析 JSON 設定檔:
    1. 建立 Device Name -> Slot Index 的映射
    2. 計算每個 Slot 的「有效頻率 (Effective Rate)」
    3. 判斷每個 Slot 的顯示模式 (Time vs FFT)
    """
    def __init__(self, config_path):
        self.slot_titles = [] 
        # device_map: { "DeviceName": SlotIndex }
        self.device_map = {} 
        # slot_rates: { SlotIndex: EffectiveRate(Hz) }
        self.slot_rates = {}
        # slot_modes: { SlotIndex: "FFT" or "TIME" }
        self.slot_modes = {}
        
        self.load_config(config_path)

    def load_config(self, path):
        try:
            with open(path, 'r', encoding='utf-8') as f:
                config = json.load(f)
            
            print(f"[System] Loading Config: {config.get('system_name')}")
            print("-" * 80)
            print(f"{'Slot':<6} {'Device':<12} {'TaskRate':<10} {'Mode':<6} {'Eff. Rate':<10}")
            print("-" * 80)
            
            # 建立 Slot 列表
            for task in config.get('tasks', []):
                if not task.get('active', False):
                    continue
                
                task_rate = float(task.get('sample_rate', 1000.0))
                
                for ch in task.get('channels', []):
                    if not ch.get('active', True):
                        continue
                    
                    dev_name = ch.get('device_name')
                    model = ch.get('model_info', dev_name)
                    
                    # 1. 計算降頻後的有效頻率 (Moving Average)
                    avg_cfg = ch.get('moving_avg', {})
                    window_size = 1
                    if avg_cfg.get('active', False):
                        window_size = int(avg_cfg.get('window_size', 1))
                    
                    eff_rate = task_rate / window_size

                    # 2. 判斷顯示模式 (FFT active?)
                    fft_cfg = ch.get('fft', {})
                    is_fft = fft_cfg.get('active', False)
                    mode_str = "FFT" if is_fft else "TIME"

                    # 3. 註冊或更新 Slot 資訊
                    if dev_name not in self.device_map:
                        self.slot_titles.append(f"Slot {len(self.slot_titles)+1}: {model}")
                        current_slot_idx = len(self.slot_titles) - 1
                        self.device_map[dev_name] = current_slot_idx
                        self.slot_rates[current_slot_idx] = eff_rate
                        self.slot_modes[current_slot_idx] = mode_str
                    else:
                        # 若 Slot 已存在，通常取較高的頻率為主，但模式應一致
                        current_slot_idx = self.device_map[dev_name]
                        if eff_rate > self.slot_rates[current_slot_idx]:
                            self.slot_rates[current_slot_idx] = eff_rate
                        if is_fft: 
                            self.slot_modes[current_slot_idx] = "FFT"

                    # 列印資訊
                    print(f"{current_slot_idx+1:<6} {dev_name:<12} {task_rate:<10.1f} {mode_str:<6} {eff_rate:<10.2f} Hz")

            print("-" * 80)
            print(f"[System] Configured {len(self.slot_titles)} slots.")
            
        except Exception as e:
            print(f"[Error] Config load failed: {e}")
            sys.exit(1)

class RealTimePlotter:
    def __init__(self, mapper):
        self.mapper = mapper
        self.running = True
        
        self.packet_queue = queue.Queue()
        
        # UI 狀態 - 預設 100ms (僅對 Time Mode 有效)
        self.time_window = 0.1 
        
        # 資料儲存結構: buffers[slot_idx][ch_index]
        self.buffers = [{} for _ in range(len(self.mapper.slot_titles))]
        
        # FFT 模式下儲存 X 軸資訊
        self.fft_axis_info = [None] * len(self.mapper.slot_titles)
        
        # 計算 Time Mode 的 Buffer 長度限制
        self.slot_max_lens = {}
        for slot_idx, rate in self.mapper.slot_rates.items():
            self.slot_max_lens[slot_idx] = int(rate * MAX_BUFFER_SEC) + 100

        # 繪圖物件快取
        self.lines = [{} for _ in range(len(self.mapper.slot_titles))]
        
        # 按鈕物件快取
        self.btns = []
        self.btn_axes = []
        
        self.init_plot()
        
        self.udp_thread = threading.Thread(target=self.udp_worker, daemon=True)
        self.udp_thread.start()

    def init_plot(self):
        plt.ion() 
        self.fig, self.axes = plt.subplots(4, 2, figsize=(16, 10))
        self.axes = self.axes.flatten()
        
        self.fig.canvas.manager.set_window_title('NI cDAQ-9189 Monitor (Time & FFT Mixed)')
        plt.subplots_adjust(left=0.06, bottom=0.05, right=0.98, top=0.92, wspace=0.15, hspace=0.45)

        # 設定 Slot 標題與軸
        for i, ax in enumerate(self.axes):
            if i < len(self.mapper.slot_titles):
                title = self.mapper.slot_titles[i]
                rate = self.mapper.slot_rates.get(i, 0)
                mode = self.mapper.slot_modes.get(i, "TIME")
                
                # 依據模式設定標題與標籤
                if mode == "FFT":
                    ax.set_title(f"{title} [FFT Spectrum]", fontsize=9, fontweight='bold', color='darkblue', pad=3)
                    ax.set_xlabel("Frequency (Hz)", fontsize=7)
                    ax.set_ylabel("Magnitude (V)", fontsize=7)
                else:
                    ax.set_title(f"{title} (Rate: {rate:.1f}Hz)", fontsize=9, fontweight='bold', pad=3)
                    ax.set_xlabel("Time (s)", fontsize=7)
                    ax.set_xlim(-self.time_window, 0)
                
                ax.grid(True, linestyle=':', alpha=0.7)
                ax.tick_params(labelsize=7)
            else:
                ax.set_visible(False)

        # --- 時間選擇按鈕 ---
        labels = ['10ms', '100ms', '500ms', '1000ms', '5S', '10S']
        n_btns = len(labels)
        btn_w = 0.08
        btn_h = 0.03
        gap = 0.01
        
        total_w = n_btns * btn_w + (n_btns - 1) * gap
        start_x = 0.5 - (total_w / 2)
        y_pos = 0.968
        
        self.btns = []
        self.btn_axes = []

        for i, label in enumerate(labels):
            x = start_x + i * (btn_w + gap)
            ax_btn = plt.axes([x, y_pos, btn_w, btn_h])
            btn = Button(ax_btn, label, color='0.9', hovercolor='0.8')
            
            if label == '100ms':
                btn.color = 'orange'
                ax_btn.set_facecolor('orange')
            
            btn.on_clicked(lambda event, l=label, b=btn: self.change_window(l, b))
            self.btns.append(btn)
            self.btn_axes.append(ax_btn)

        self.fig.canvas.mpl_connect('key_press_event', self.on_key)
        self.fig.canvas.mpl_connect('close_event', self.on_close)

    def change_window(self, label, clicked_btn):
        label_lower = label.lower()
        val = 0.1
        if 'ms' in label_lower:
            val = float(label_lower.replace('ms', '')) / 1000.0
        elif 's' in label_lower:
            val = float(label_lower.replace('s', ''))
        
        self.time_window = val
        print(f"[UI] Time Window set to: {self.time_window}s (Time-Mode slots only)")
        
        for btn in self.btns:
            if btn == clicked_btn:
                btn.color = 'orange'
                btn.ax.set_facecolor('orange')
            else:
                btn.color = '0.9'
                btn.ax.set_facecolor('0.9')
        
        for i, ax in enumerate(self.axes):
            if ax.get_visible() and self.mapper.slot_modes.get(i) == "TIME":
                ax.set_xlim(-self.time_window, 0)
        
        self.fig.canvas.draw_idle()

    def udp_worker(self):
        sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        sock.bind((UDP_IP, UDP_PORT))
        sock.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF, 4 * 1024 * 1024)
        print(f"[UDP] Listening on port {UDP_PORT}...")

        while self.running:
            try:
                data, _ = sock.recvfrom(BUFFER_SIZE)
                self.packet_queue.put(data)
            except OSError:
                break
        sock.close()

    def process_packet(self, raw_data):
        try:
            msg = raw_data.decode('utf-8').strip()
            parts = msg.split(',')
            
            if len(parts) < 6: return
            header_name = parts[0]
            
            target_slot = -1
            for dev_name, slot_idx in self.mapper.device_map.items():
                if header_name.startswith(dev_name):
                    target_slot = slot_idx
                    break
            
            if target_slot == -1: return 

            mode = self.mapper.slot_modes.get(target_slot, "TIME")
            
            # [Case A] FFT 封包
            if "_FFT" in header_name and mode == "FFT":
                ch_count = int(parts[3])
                bin_count = int(parts[4])
                start_freq = float(parts[5])
                delta_freq = float(parts[6])
                
                self.fft_axis_info[target_slot] = (start_freq, delta_freq, bin_count)
                
                data_start_idx = 7
                all_data = [float(x) for x in parts[data_start_idx:]]
                
                if len(all_data) < ch_count * bin_count: return

                for i in range(ch_count):
                    start = i * bin_count
                    end = start + bin_count
                    spectrum = all_data[start:end]
                    self.buffers[target_slot][i] = spectrum

            # [Case B] Time Domain 封包
            elif mode == "TIME":
                ch_count = int(parts[3])
                data_values = [float(x) for x in parts[5:]]
                maxlen = self.slot_max_lens.get(target_slot, 5000)

                for i in range(ch_count):
                    if i >= len(data_values): break
                    val = data_values[i]
                    if i not in self.buffers[target_slot]:
                        self.buffers[target_slot][i] = deque(maxlen=maxlen)
                    self.buffers[target_slot][i].append(val)

        except Exception:
            pass

    def update_plot(self):
        while self.running:
            start_time = time.time()

            while not self.packet_queue.empty():
                self.process_packet(self.packet_queue.get())

            for slot_idx, ax in enumerate(self.axes):
                if slot_idx >= len(self.buffers): break
                
                slot_data = self.buffers[slot_idx]
                if not slot_data: continue

                mode = self.mapper.slot_modes.get(slot_idx, "TIME")
                has_update = False

                # === Mode: FFT (頻域) ===
                if mode == "FFT":
                    axis_info = self.fft_axis_info[slot_idx]
                    if not axis_info: continue
                    
                    start_freq, delta_freq, bin_count = axis_info
                    x_data = np.linspace(start_freq, start_freq + (bin_count - 1) * delta_freq, bin_count)
                    
                    # [新增] 用來追蹤當前 Slot 所有通道的最大值，以便設定 Y 軸
                    current_slot_max_y = 0.0

                    for ch_idx, spectrum_data in slot_data.items():
                        if len(spectrum_data) != bin_count: continue
                        
                        label = f"Ch{ch_idx}"
                        has_update = True
                        
                        # 更新最大值 (排除極小值避免 log scale 問題，雖然這裡是 linear)
                        ch_max = np.max(spectrum_data)
                        if ch_max > current_slot_max_y:
                            current_slot_max_y = ch_max
                        
                        if ch_idx not in self.lines[slot_idx]:
                            line, = ax.plot([], [], label=label, linewidth=1.0)
                            self.lines[slot_idx][ch_idx] = line
                            if ch_idx == 0: ax.legend(loc='upper right', fontsize=6)
                        
                        self.lines[slot_idx][ch_idx].set_data(x_data, spectrum_data)
                    
                    if has_update:
                        ax.set_xlim(0, x_data[-1])
                        
                        # [修改] 強制設定 Y 軸範圍隨最大值變化 (Dynamic Scaling)
                        # 如果最大值極小 (例如沒訊號)，設一個預設值避免 crash
                        top_limit = current_slot_max_y * 1.1 if current_slot_max_y > 1e-6 else 1.0
                        ax.set_ylim(0, top_limit)
                        
                        # 移除 autoscale_view，改用手動控制
                        # ax.relim() 
                        # ax.autoscale_view(scalex=False, scaley=True)

                # === Mode: Time (時域) ===
                else:
                    eff_rate = self.mapper.slot_rates.get(slot_idx, 10.0)
                    points_needed = int(eff_rate * self.time_window)
                    if points_needed < 10: points_needed = 10

                    for ch_idx, data_deque in slot_data.items():
                        if len(data_deque) < 2: continue
                        has_update = True
                        
                        full_data = list(data_deque)
                        if len(full_data) > points_needed:
                            display_data = full_data[-points_needed:]
                        else:
                            display_data = full_data
                        
                        num_points = len(display_data)
                        step = 1
                        if num_points > PLOT_DISPLAY_LIMIT:
                            step = num_points // PLOT_DISPLAY_LIMIT + 1
                            display_data = display_data[::step]
                            num_points = len(display_data)

                        actual_duration = (num_points * step) / eff_rate
                        x_data = np.linspace(-actual_duration, 0, num_points)
                        
                        label = f"Ch{ch_idx}"
                        if ch_idx not in self.lines[slot_idx]:
                            line, = ax.plot([], [], label=label, linewidth=1.2 if eff_rate < 100 else 1.0)
                            self.lines[slot_idx][ch_idx] = line
                            if ch_idx == 0: ax.legend(loc='upper left', fontsize=6, framealpha=0.5)

                        self.lines[slot_idx][ch_idx].set_data(x_data, display_data)

                    if has_update:
                        ax.set_xlim(-self.time_window, 0)
                        ax.relim()
                        ax.autoscale_view(scalex=False, scaley=True)

            self.fig.canvas.flush_events()
            
            elapsed = time.time() - start_time
            sleep_time = (1.0 / MAX_FPS) - elapsed
            if sleep_time > 0:
                time.sleep(sleep_time)

    def on_key(self, event):
        if event.key == 'enter':
            self.close()

    def on_close(self, event):
        self.close()

    def close(self):
        self.running = False
        plt.close(self.fig)
        sys.exit(0)

if __name__ == "__main__":
    print("=== UDP Plotter (Mixed Time/FFT Modes) ===")
    mapper = SystemMapper(CONFIG_FILE)
    plotter = RealTimePlotter(mapper)
    
    print("\nRunning...")
    try:
        plotter.update_plot()
    except KeyboardInterrupt:
        plotter.close()