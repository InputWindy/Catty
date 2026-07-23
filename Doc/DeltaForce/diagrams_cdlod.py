"""
生成 CDLOD 地形几何与跨平台架构流程图
"""
import matplotlib.pyplot as plt
import matplotlib.patches as mpatches
import numpy as np
import os

fig, axes = plt.subplots(1, 2, figsize=(15, 6))

# --- 左图: CDLOD LOD 层级示意 ---
ax = axes[0]
ax.set_title('CDLOD Terrain Mesh\n(1-2 Draw Calls for entire terrain)', fontsize=10, fontweight='bold')
ax.set_xlim(0, 16)
ax.set_ylim(0, 16)
ax.set_aspect('equal')

# LOD3 - 最远 最大块
for x in range(0, 16, 8):
    for y in range(0, 16, 8):
        if x == 0 and y == 0:
            continue
        rect = mpatches.Rectangle((x, y), 8, 8, fill=False, edgecolor='#e74c3c', linewidth=2)
        ax.add_patch(rect)
        ax.text(x+4, y+4, 'LOD3', ha='center', va='center', fontsize=8, color='#e74c3c')

# LOD2
for x in range(0, 8, 4):
    for y in range(0, 8, 4):
        if x < 4 and y < 4:
            continue
        rect = mpatches.Rectangle((x, y), 4, 4, fill=False, edgecolor='#f39c12', linewidth=1.5)
        ax.add_patch(rect)
        ax.text(x+2, y+2, 'LOD2', ha='center', va='center', fontsize=7, color='#f39c12')

# LOD1
for x in range(0, 4, 2):
    for y in range(0, 4, 2):
        if x < 2 and y < 2:
            continue
        rect = mpatches.Rectangle((x, y), 2, 2, fill=False, edgecolor='#3498db', linewidth=1)
        ax.add_patch(rect)
        ax.text(x+1, y+1, 'LOD1', ha='center', va='center', fontsize=6, color='#3498db')

# LOD0 - 最近 最密
rect = mpatches.Rectangle((0, 0), 2, 2, fill=True, facecolor='#2ecc71', alpha=0.3, edgecolor='#2ecc71', linewidth=2)
ax.add_patch(rect)
ax.text(1, 1, 'LOD0\n(densest)', ha='center', va='center', fontsize=7, color='#27ae60', fontweight='bold')

# Camera
ax.plot(1, 1, 'k^', markersize=14)
ax.annotate('Camera', (1, 1), textcoords="offset points", xytext=(15, 10), fontsize=9, fontweight='bold')

ax.set_xlabel('World X (meters)')
ax.set_ylabel('World Y (meters)')
ax.grid(True, alpha=0.1)

# --- 右图: 跨平台架构流 ---
ax2 = axes[1]
ax2.set_title('Cross-Platform Pipeline Overview', fontsize=10, fontweight='bold')
ax2.axis('off')
ax2.set_xlim(0, 10)
ax2.set_ylim(0, 10)

# 绘制流程框
boxes = [
    (5, 9.2, 'Houdini PCG\n(Offline Generation)', '#3498db'),
    (5, 7.5, 'Biome Preset Layers\n(Terrain + Veg + Decal + VFX)', '#9b59b6'),
    (5, 5.8, 'Clipmap (2.5MB)\n(RGBA packed biome data)', '#2ecc71'),
    (2.5, 4.0, 'PC Pipeline\n- Impostor LOD\n- Full VT + Decals\n- HW Tess / Soft Tess\n- SSR + IBL', '#e74c3c'),
    (7.5, 4.0, 'Mobile Pipeline\n- Billboard LOD\n- VT + ASTC Compress\n- CDLOD only\n- Fake Shadow + AO', '#f39c12'),
    (5, 1.5, 'Shared: CDLOD Mesh\n+ Virtual Texture\n+ Dynamic Texture Array', '#1abc9c'),
]

for (x, y, text, color) in boxes:
    w, h = 3.8, 1.2
    if 'PC Pipeline' in text or 'Mobile Pipeline' in text:
        h = 1.8
    rect = mpatches.FancyBboxPatch((x-w/2, y-h/2), w, h,
                                    boxstyle="round,pad=0.1",
                                    facecolor=color, alpha=0.2, edgecolor=color, linewidth=2)
    ax2.add_patch(rect)
    ax2.text(x, y, text, ha='center', va='center', fontsize=7.5, fontweight='bold')

# 箭头
arrows = [(5, 8.6, 5, 8.1), (5, 6.9, 5, 6.4),
          (3.5, 5.2, 2.5, 4.9), (6.5, 5.2, 7.5, 4.9),
          (2.5, 3.1, 4, 2.1), (7.5, 3.1, 6, 2.1)]
for (x1, y1, x2, y2) in arrows:
    ax2.annotate('', xy=(x2, y2), xytext=(x1, y1),
                 arrowprops=dict(arrowstyle='->', color='#34495e', lw=1.5))

plt.tight_layout()
out_dir = os.path.dirname(os.path.abspath(__file__))
plt.savefig(os.path.join(out_dir, 'fig_cdlod_pipeline.png'), dpi=150, bbox_inches='tight')
plt.close()
print("fig_cdlod_pipeline.png saved.")
