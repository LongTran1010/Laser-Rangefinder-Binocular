import pandas as pd
import numpy as np
import matplotlib.pyplot as plt

# Load dữ liệu của bạn
df = pd.read_csv('350m.csv')

# Tham số bộ lọc
alpha = 0.25
gate_threshold = 10.0  # Mét
max_reject = 5

# Biến trạng thái
ema_filter = df['distance_m'].iloc[0]
reject_count = 0
filtered_data = []

# Mô phỏng luồng dữ liệu thời gian thực
for raw_val in df['distance_m']:
    if np.isnan(raw_val):
        filtered_data.append(ema_filter)
        continue

    # --- GATING LOGIC ---
    delta = abs(raw_val - ema_filter)

    if delta > gate_threshold:
        reject_count += 1
        if reject_count < max_reject:
            # Bỏ qua mẫu nhiễu, giữ giá trị cũ
            filtered_data.append(ema_filter)
            continue
        else:
            # Chấp nhận giá trị mới (reset)
            ema_filter = raw_val
            reject_count = 0
    else:
        reject_count = 0

    # --- EMA LOGIC ---
    ema_filter = alpha * raw_val + (1 - alpha) * ema_filter
    filtered_data.append(ema_filter)

# Vẽ đồ thị so sánh
plt.figure(figsize=(12, 6))
plt.plot(df['distance_m'], label='Dữ liệu thô (Có nhiễu)', alpha=0.5, color='gray')
plt.plot(df['distance_filt_m'], label='Bộ lọc cũ (Bị kéo xuống)', color='orange', linestyle='--')
plt.plot(filtered_data, label='Bộ lọc mới (Gating)', color='green', linewidth=2)
plt.title('So sánh hiệu quả bộ lọc tại khoảng cách 350m')
plt.ylabel('Khoảng cách (m)')
plt.legend()
plt.grid(True)
plt.show()