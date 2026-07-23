"""
生成 Clipmap 内存层级与数据打包示意图
"""
import matplotlib.pyplot as plt
import matplotlib.patches as mpatches
import numpy as np

fig, axes = plt.subplots(1, 2, figsize=(14, 6))

# --- 左图: Clipmap Mip 层级 ---
ax = axes[0]
ax.set_xlim(-6, 6)
ax.set_ylim(-6, 6)
ax.set_aspect('equal')
ax.set_title('Clipmap Mip Hierarchy\n(Camera-centered, rolling update)', fontsize=11, fontweight='bold')

colors = ['#2ecc71', '#3498db', '#9b59b6', '#e74c3c']
labels = ['Mip0 (1m/px)', 'Mip1 (2m/px)', 'Mip2 (4m/px)', 'Mip3 (8m/px)']
sizes = [1.5, 3, 5, 6]

for i in reversed(range(4)):
    rect = mpatches.FancyBboxPatch((-sizes[i], -sizes[i]), sizes[i]*2, sizes[i]*2,
                                    boxstyle="round,pad=0.1",
                                    facecolor=colors[i], alpha=0.3, edgecolor=colors[i], linewidth=2)
    ax.add_patch(rect)

ax.plot(0, 0, 'k^', markersize=12, label='Camera')
ax.legend(loc='upper right', fontsize=9)

for i, (s, l) in enumerate(zip(sizes, labels)):
    ax.annotate(l, xy=(s-0.1, s-0.3), fontsize=8, color=colors[i], fontweight='bold')

ax.set_xlabel('World X')
ax.set_ylabel('World Y')
ax.grid(True, alpha=0.2)

# --- 右图: Clipmap Channel 打包 ---
ax2 = axes[1]
ax2.axis('off')
ax2.set_title('Clipmap RGBA Channel Packing', fontsize=11, fontweight='bold')

table_data = [
    ['Channel', 'Vegetation', 'Water', 'Wetness'],
    ['R', 'Tree Health\n(0.1-0.85: Season\n0.9-1: Burnt)', 'Velocity U\n(Flow Map)', '-'],
    ['G', '-', 'Velocity V\n(Flow Map)', '-'],
    ['B', 'Grass Health\n(0.1-0.85: Season\n0.9-1: Burnt)', 'Absorption\n(Water Color)', '-'],
    ['A', '-', 'Foam\n(0.6-1.0)', 'Wetness\n(0.1-0.5)'],
]

colors_table = [['#ecf0f1']*4]
row_colors = ['#e8f8f5', '#ebf5fb', '#fef9e7', '#fdedec']
for c in row_colors:
    colors_table.append([c]*4)

table = ax2.table(cellText=table_data, cellColours=colors_table,
                  loc='center', cellLoc='center')
table.auto_set_font_size(False)
table.set_fontsize(9)
table.scale(1.2, 2.0)

ax2.text(0.5, 0.02, '100MB (10K texture) -> 2.5MB (Clipmap)\nSingle texture serves: Vegetation + Water + Wetness + VFX',
         ha='center', fontsize=9, style='italic', transform=ax2.transAxes)

plt.tight_layout()
import os
out_dir = os.path.dirname(os.path.abspath(__file__))
plt.savefig(os.path.join(out_dir, 'fig_clipmap.png'), dpi=150, bbox_inches='tight')
plt.close()
print("fig_clipmap.png saved.")
