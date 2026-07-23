"""
生成性能数据对比图 (Delta Force vs Weight Map)
"""
import matplotlib.pyplot as plt
import numpy as np
import os

fig, axes = plt.subplots(2, 2, figsize=(12, 8))
fig.suptitle('Performance: Delta Force vs Weight-per-layer (Snapdragon 855)', fontsize=12, fontweight='bold')

# 数据
categories = ['Still-High', 'Worst-High', 'Still-Low', 'Worst-Low']
x = np.arange(len(categories))
width = 0.35

# Frame Rate
ax = axes[0, 0]
df_fps = [175, 155, 200, 180]
wm_fps = [97, 95, 140, 130]
bars1 = ax.bar(x - width/2, df_fps, width, label='Delta Force', color='#2ecc71', alpha=0.8)
bars2 = ax.bar(x + width/2, wm_fps, width, label='Weight Map', color='#e74c3c', alpha=0.8)
ax.set_ylabel('FPS')
ax.set_title('Frame Rate (higher=better)')
ax.set_xticks(x)
ax.set_xticklabels(categories, fontsize=8)
ax.legend()
ax.set_ylim(0, 230)
for bar in bars1:
    ax.text(bar.get_x() + bar.get_width()/2., bar.get_height() + 3, f'{int(bar.get_height())}', ha='center', fontsize=8)
for bar in bars2:
    ax.text(bar.get_x() + bar.get_width()/2., bar.get_height() + 3, f'{int(bar.get_height())}', ha='center', fontsize=8)

# Power Consumption
ax = axes[0, 1]
df_pow = [1.15, 1.75, 1.12, 1.45]
wm_pow = [1.9, 2.05, 1.57, 1.65]
bars1 = ax.bar(x - width/2, df_pow, width, label='Delta Force', color='#2ecc71', alpha=0.8)
bars2 = ax.bar(x + width/2, wm_pow, width, label='Weight Map', color='#e74c3c', alpha=0.8)
ax.set_ylabel('Power (mW)')
ax.set_title('Power Consumption (lower=better)')
ax.set_xticks(x)
ax.set_xticklabels(categories, fontsize=8)
ax.legend()

# Bandwidth
ax = axes[1, 0]
df_bw = [2.45, 3.40, 1.87, 2.39]
wm_bw = [5.46, 6.49, 4.26, 6.65]
bars1 = ax.bar(x - width/2, df_bw, width, label='Delta Force', color='#2ecc71', alpha=0.8)
bars2 = ax.bar(x + width/2, wm_bw, width, label='Weight Map', color='#e74c3c', alpha=0.8)
ax.set_ylabel('GB/sec')
ax.set_title('Bandwidth (lower=better)')
ax.set_xticks(x)
ax.set_xticklabels(categories, fontsize=8)
ax.legend()

# GPU Time
ax = axes[1, 1]
categories_gpu = ['Still-High', 'Still-Low']
x_gpu = np.arange(len(categories_gpu))
df_gpu = [5.23, 8.93]
wm_gpu = [4.55, 8.32]
bars1 = ax.bar(x_gpu - width/2, df_gpu, width, label='Delta Force (32 layers)', color='#2ecc71', alpha=0.8)
bars2 = ax.bar(x_gpu + width/2, wm_gpu, width, label='Weight Map (4 layers)', color='#e74c3c', alpha=0.8)
ax.set_ylabel('ms')
ax.set_title('GPU Time (lower=better)\n*DF supports 32 layers + decals vs WM 4 layers')
ax.set_xticks(x_gpu)
ax.set_xticklabels(categories_gpu, fontsize=8)
ax.legend()
ax.set_ylim(0, 12)

plt.tight_layout()
out_dir = os.path.dirname(os.path.abspath(__file__))
plt.savefig(os.path.join(out_dir, 'fig_performance.png'), dpi=150, bbox_inches='tight')
plt.close()
print("fig_performance.png saved.")
