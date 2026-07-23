"""
生成 Splat Map ID Fixing 与三角插值示意图
"""
import matplotlib.pyplot as plt
import matplotlib.patches as mpatches
import matplotlib.tri as tri
import numpy as np
import os

fig, axes = plt.subplots(1, 3, figsize=(16, 5))

# --- 左图: 传统方法对比 ---
ax = axes[0]
ax.set_title('Splat Map Methods Comparison', fontsize=10, fontweight='bold')
ax.axis('off')

methods = ['Weight-per-layer\n(UE Default)', 'ID Map\n(Far Cry)', 'Delta Force\n(Ours)']
samples = ['18 samples', '12 samples', '9 samples']
features = ['Weight Blend: YES\nMax Layers: 4-8', 'Weight Blend: NO\nMax Layers: 32+', 'Weight Blend: YES\nMax Layers: 32+']
colors = ['#e74c3c', '#f39c12', '#2ecc71']

for i, (m, s, f, c) in enumerate(zip(methods, samples, features, colors)):
    y = 0.75 - i * 0.3
    rect = mpatches.FancyBboxPatch((0.05, y-0.08), 0.9, 0.2,
                                    boxstyle="round,pad=0.02",
                                    facecolor=c, alpha=0.2, edgecolor=c, linewidth=2,
                                    transform=ax.transAxes)
    ax.add_patch(rect)
    ax.text(0.15, y+0.03, m, fontsize=9, fontweight='bold', transform=ax.transAxes, va='center')
    ax.text(0.5, y+0.03, s, fontsize=9, transform=ax.transAxes, va='center')
    ax.text(0.7, y+0.03, f, fontsize=8, transform=ax.transAxes, va='center')

# --- 中图: 三角插值 vs 四边形插值 ---
ax2 = axes[1]
ax2.set_title('Triangle Interpolation\n(6 IDs -> 3 distinct)', fontsize=10, fontweight='bold')
ax2.set_xlim(-0.5, 3.5)
ax2.set_ylim(-0.5, 3.5)
ax2.set_aspect('equal')

# 画网格
for i in range(4):
    ax2.axhline(i, color='gray', alpha=0.3, linewidth=0.5)
    ax2.axvline(i, color='gray', alpha=0.3, linewidth=0.5)

# 画三角形
triangle_x = [1, 2, 2]
triangle_y = [1, 1, 2]
ax2.fill(triangle_x, triangle_y, alpha=0.3, color='#3498db')
ax2.plot(triangle_x + [triangle_x[0]], triangle_y + [triangle_y[0]], 'b-', linewidth=2)

# 标注顶点
labels_tri = ['P0\n(Bot0, Top0)', 'P1\n(Bot1, Top1)', 'P2\n(Bot2, Top2)']
for x, y, l in zip(triangle_x, triangle_y, labels_tri):
    ax2.plot(x, y, 'ko', markersize=8)
    ax2.annotate(l, (x, y), textcoords="offset points", xytext=(5, 8), fontsize=7)

# 像素点
ax2.plot(1.5, 1.3, 'r*', markersize=15)
ax2.annotate('Pixel\n(blend 3 layers)', (1.5, 1.3), textcoords="offset points", xytext=(10, -15), fontsize=8, color='red')

ax2.set_xlabel('Terrain Grid')
ax2.set_ylabel('Terrain Grid')

# --- 右图: ID Fixing 流程 ---
ax3 = axes[2]
ax3.set_title('ID Fixing Process', fontsize=10, fontweight='bold')
ax3.axis('off')

steps = [
    '1. Get 3 bottom + 3 top IDs\n   from triangle vertices',
    '2. Create empty set (size=3)',
    '3. Add 3 bottom IDs to set',
    '4. Add top IDs (by weight)\n   until set is full',
    '5. Remove extra top IDs\n   (set top = bottom)',
    '6. Result: only 3 distinct\n   layers to sample!'
]

for i, step in enumerate(steps):
    y = 0.9 - i * 0.15
    color = '#2ecc71' if i == 5 else '#ecf0f1'
    rect = mpatches.FancyBboxPatch((0.02, y-0.05), 0.96, 0.12,
                                    boxstyle="round,pad=0.01",
                                    facecolor=color, alpha=0.4, edgecolor='#34495e', linewidth=1,
                                    transform=ax3.transAxes)
    ax3.add_patch(rect)
    ax3.text(0.06, y+0.01, step, fontsize=8, transform=ax3.transAxes, va='center', family='monospace')
    if i < 5:
        ax3.annotate('', xy=(0.5, y-0.05), xytext=(0.5, y-0.02),
                     xycoords=ax3.transAxes, textcoords=ax3.transAxes,
                     arrowprops=dict(arrowstyle='->', color='#34495e'))

plt.tight_layout()
out_dir = os.path.dirname(os.path.abspath(__file__))
plt.savefig(os.path.join(out_dir, 'fig_splatmap.png'), dpi=150, bbox_inches='tight')
plt.close()
print("fig_splatmap.png saved.")
