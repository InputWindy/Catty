# -*- coding: utf-8 -*-
"""高质量地形 —— 全部知识点用图表展示。运行生成 6 张 PNG。"""
import numpy as np
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
from matplotlib.patches import FancyBboxPatch, FancyArrowPatch, Rectangle

plt.rcParams["font.family"] = "Microsoft YaHei"
plt.rcParams["axes.unicode_minus"] = False

# ---- 调色板 (dataviz) ----
SURFACE="#1a1a19"; INK="#f0efe9"; INK2="#c3c2b7"; MUTED="#898781"
GRID="#2c2c2a"; BASE="#383835"
BLUE="#3987e5"; AQUA="#199e70"; YEL="#c98500"; GREEN="#33a833"
VIOLET="#9085e9"; RED="#e05a5a"; ORANGE="#d95926"
GOOD="#0ca30c"; WARN="#fab219"; CRIT="#e0574f"

def box(ax, x, y, w, h, fc, ec, txt, tc=INK, fs=10, bold=False, align="center", pad=0.02):
    p = FancyBboxPatch((x, y), w, h, boxstyle=f"round,pad={pad},rounding_size=0.02",
                       fc=fc, ec=ec, lw=1.6, zorder=2)
    ax.add_patch(p)
    ha = {"center":"center","left":"left"}[align]
    tx = x+w/2 if align=="center" else x+0.02
    ax.text(tx, y+h/2, txt, ha=ha, va="center", color=tc, fontsize=fs,
            fontweight="bold" if bold else "normal", zorder=3, wrap=True)

def arrow(ax, x1,y1,x2,y2, color=MUTED, lw=2):
    ax.add_patch(FancyArrowPatch((x1,y1),(x2,y2), arrowstyle="-|>",
                 mutation_scale=16, color=color, lw=lw, zorder=1))

# =====================================================================
# 图1: 术语思维导图
# =====================================================================
def fig_glossary():
    cats = [
        ("几何 Geometry", BLUE, [
            ("Heightmap 高度图", "灰度图存每点地表高度"),
            ("LOD 细节层级", "近密远疏,省顶点"),
            ("Popping 跳变", "LOD 切换时形状突变"),
            ("Morph 形变过渡", "插值顶点,消 popping"),
            ("CDLOD 连续LOD", "四叉树+GPU morph 无缝"),
            ("Tessellation 曲面细分", "GPU 拆三角,加近处细节"),
            ("Draw Call 提交批次", "越多 CPU 越累"),
        ]),
        ("纹理/材质 Texturing", AQUA, [
            ("Splat/Weight 权重图", "每点各材质占比"),
            ("Height Blend 高度混合", "按高度过渡,非线性alpha"),
            ("Tri-planar 三向投影", "陡坡免拉伸,采样×3"),
            ("Tiling 平铺重复", "大面积可见重复花纹"),
            ("Stochastic 随机混合", "随机采样打破 tiling"),
            ("Virtual Texture 虚拟纹理", "超大纹理按页调入显存"),
            ("Clipmap 裁剪图", "以相机为心的分层缓存"),
            ("POM 视差遮蔽", "高度图伪造凹凸自遮挡"),
        ]),
        ("光照 Shading", YEL, [
            ("PBR 物理渲染", "能量守恒,金属/粗糙度"),
            ("CSM 级联阴影", "太阳阴影主流做法"),
            ("AO 环境光遮蔽", "缝隙凹陷变暗"),
            ("IBL 环境光照", "用环境贴图照明/反射"),
            ("SSR 屏幕空间反射", "水面常用"),
            ("GI 全局光照", "间接多次反弹光"),
        ]),
        ("植被/生产 Content", GREEN, [
            ("Biome 生态群系", "统一风格的区域"),
            ("Impostor 卡片", "多角度图顶替远处模型"),
            ("Billboard 公告板", "朝向相机的片,最廉价"),
            ("PCG 程序化生成", "规则自动铺植被石头"),
            ("Houdini / HDA", "程序化工具+可复用资产"),
            ("Permutation 排列", "按开关选最省 shader 变体"),
        ]),
    ]
    fig, ax = plt.subplots(figsize=(15, 9.2), dpi=140)
    fig.patch.set_facecolor(SURFACE); ax.set_facecolor(SURFACE)
    ax.set_xlim(0, 4); ax.set_ylim(0, 9); ax.axis("off")

    colw = 0.92
    for ci, (title, col, terms) in enumerate(cats):
        x = ci + 0.04
        box(ax, x, 8.35, colw, 0.5, col, col, title, tc="#ffffff", fs=13, bold=True)
        y = 8.05
        for term, desc in terms:
            h = 0.62
            box(ax, x, y-h, colw, h, "#242422", col, "", pad=0.005)
            ax.text(x+0.05, y-0.20, term, ha="left", va="center", color=INK,
                    fontsize=10.2, fontweight="bold")
            ax.text(x+0.05, y-0.46, desc, ha="left", va="center", color=INK2, fontsize=8.8)
            y -= h + 0.12
    ax.text(0.04, 8.92, "地形渲染术语总览", fontsize=17, fontweight="bold", color=INK)
    fig.tight_layout()
    fig.savefig("图1_术语思维导图.png", facecolor=SURFACE, bbox_inches="tight")
    plt.close(fig)

# =====================================================================
# 图2: PC 高质量地形标准管线
# =====================================================================
def fig_pipeline():
    stages = [
        ("① 几何", BLUE, ["高度图 → 四叉树 LOD", "morph 消 popping", "近处硬件细分 + 位移"]),
        ("② 表面材质", AQUA, ["Splat 多层混合", "Height Blend 高度混合", "VT 烘焙摊销采样"]),
        ("③ 细节增强", YEL, ["悬崖 Tri-planar", "Stochastic 去 tiling", "POM + 宏观/微观贴图"]),
        ("④ 场景集成", GREEN, ["PCG 植被(近模型/远Impostor)", "道路 河流 湖泊", "贴花 Decal"]),
        ("⑤ 光照", ORANGE, ["CSM 阴影 + 接触阴影", "GI / lightmap", "AO + 水体反射"]),
    ]
    fig, ax = plt.subplots(figsize=(15, 5.4), dpi=140)
    fig.patch.set_facecolor(SURFACE); ax.set_facecolor(SURFACE)
    ax.set_xlim(0, 15); ax.set_ylim(0, 5); ax.axis("off")

    bw, gap = 2.6, 0.35
    x = 0.15
    for name, col, items in stages:
        box(ax, x, 3.55, bw, 0.72, col, col, name, tc="#ffffff", fs=13, bold=True)
        box(ax, x, 0.7, bw, 2.6, "#242422", col, "", pad=0.01)
        for i, it in enumerate(items):
            ax.text(x+0.12, 2.85-i*0.62, "• "+it, ha="left", va="center",
                    color=INK2, fontsize=9.6)
        if x > 0.2:
            arrow(ax, x-gap+0.02, 3.9, x-0.02, 3.9, color=MUTED, lw=2.2)
        x += bw + gap
    ax.text(0.15, 4.6, "PC 高质量地形标准管线", fontsize=17, fontweight="bold", color=INK)
    ax.text(0.15, 0.28, "思路:分而治之地堆质量,不太担心内存/带宽", fontsize=10, color=MUTED)
    fig.tight_layout()
    fig.savefig("图2_PC地形管线.png", facecolor=SURFACE, bbox_inches="tight")
    plt.close(fig)

# =====================================================================
# 图3: 高质量地形的质量维度 (雷达)
# =====================================================================
def fig_radar():
    dims = ["宏观几何","微观几何","无缝LOD","材质丰富度","抗重复",
            "特殊地表","生态融合","光照交互"]
    ordinary = [2.5, 1.5, 2, 2, 1.5, 1.5, 1.5, 2]
    hq       = [4.5, 4, 5, 4.5, 4, 4.5, 4, 4.5]
    N = len(dims)
    ang = np.linspace(0, 2*np.pi, N, endpoint=False).tolist()
    ang += ang[:1]
    def close(v): return v + v[:1]

    fig, ax = plt.subplots(figsize=(9, 8.2), dpi=140, subplot_kw=dict(polar=True))
    fig.patch.set_facecolor(SURFACE); ax.set_facecolor(SURFACE)
    ax.set_theta_offset(np.pi/2); ax.set_theta_direction(-1)
    ax.set_ylim(0, 5)
    ax.set_xticks(ang[:-1]); ax.set_xticklabels(dims, fontsize=12, color=INK)
    ax.set_yticks([1,2,3,4,5]); ax.set_yticklabels(["1","2","3","4","5"], color=MUTED, fontsize=9)
    ax.grid(color=GRID, lw=1); ax.spines["polar"].set_color(BASE)

    ax.plot(ang, close(ordinary), color=MUTED, lw=2, label="普通地形")
    ax.fill(ang, close(ordinary), color=MUTED, alpha=0.12)
    ax.plot(ang, close(hq), color=BLUE, lw=2.4, label="高质量地形")
    ax.fill(ang, close(hq), color=BLUE, alpha=0.18)

    ax.set_title("高质量地形的 8 个画质维度", fontsize=16, fontweight="bold",
                 color=INK, pad=28)
    ax.legend(loc="upper right", bbox_to_anchor=(1.16, 1.12), frameon=False,
              fontsize=12, labelcolor=INK2)
    ax.set_position([0.10, 0.16, 0.80, 0.74])
    fig.text(0.5, 0.045, "另有工程维度:⑨ 流送与内存    ⑩ 性能/带宽/功耗    —— 决定能否真的做出来并跑起来",
             ha="center", fontsize=10.5, color=INK2)
    fig.savefig("图3_质量维度雷达.png", facecolor=SURFACE, bbox_inches="tight")
    plt.close(fig)

# =====================================================================
# 图4: LOD 分级 与 popping/morph
# =====================================================================
def fig_lod():
    fig, (a1, a2) = plt.subplots(1, 2, figsize=(14, 6), dpi=140,
                                 gridspec_kw={"width_ratios":[1.1,1]})
    for a in (a1,a2):
        a.set_facecolor(SURFACE)
    fig.patch.set_facecolor(SURFACE)

    # --- 左: 以相机为中心的 LOD 环 (近密远疏) ---
    a1.set_xlim(0,10); a1.set_ylim(0,10); a1.axis("off")
    a1.set_title("LOD 分级:以相机为中心,近密远疏", fontsize=14, fontweight="bold", color=INK, loc="left")
    cx, cy = 5,5
    # 从外到内绘制:外圈疏(远)、内圈密(近相机)。内圈后画覆盖中心 → 中心最密
    rings = [(4.5, YEL,  1.5, "LOD2 远·疏", 0.55),
             (3.2, AQUA, 0.8, "LOD1 中",   0.65),
             (2.0, BLUE, 0.4, "LOD0 近·密", 0.75)]
    for r, col, step, lab, al in rings:
        gv = np.arange(cx-r, cx+r+0.001, step)
        for gx in gv:
            a1.plot([gx,gx],[cy-r,cy+r], color=col, lw=0.6, alpha=al, zorder=1)
        gh = np.arange(cy-r, cy+r+0.001, step)
        for gy in gh:
            a1.plot([cx-r,cx+r],[gy,gy], color=col, lw=0.6, alpha=al, zorder=1)
        a1.add_patch(Rectangle((cx-r, cy-r), 2*r, 2*r, fill=False, ec=col, lw=2.2, zorder=2))
    a1.plot(cx, cy, marker="o", ms=13, color=CRIT, zorder=5)
    a1.text(cx, cy-0.55, "相机", ha="center", color=CRIT, fontsize=10, fontweight="bold")
    a1.text(cx-4.4, cy+4.6, "LOD2 远·疏", color=YEL, fontsize=10.5, fontweight="bold")
    a1.text(cx-3.1, cy+3.3, "LOD1 中", color=AQUA, fontsize=10.5, fontweight="bold")
    a1.text(cx-1.9, cy+2.1, "LOD0 近·密", color=BLUE, fontsize=10.5, fontweight="bold")

    # --- 右: popping vs morph ---
    a2.set_xlim(0,10); a2.set_ylim(0,10); a2.axis("off")
    a2.set_title("过渡:硬切=跳变  vs  morph=平滑", fontsize=14, fontweight="bold", color=INK, loc="left")
    x = np.linspace(0, 10, 200)
    base = 6.8 + 0.7*np.sin(x)
    # 硬切:阶梯
    step = np.round(base*1.2)/1.2
    a2.plot(x, step, color=CRIT, lw=2.4)
    a2.text(0.2, 8.7, "无 morph:LOD 切换处高度突跳 (popping)", color=CRIT, fontsize=10.5, fontweight="bold")
    for xv in [3.3, 6.6]:
        a2.axvline(xv, color=MUTED, ls=":", lw=1)
    # morph:平滑
    a2.plot(x, base-3.4, color=GREEN, lw=2.4)
    a2.text(0.2, 5.15, "有 morph:顶点插值,过渡无感", color=GREEN, fontsize=10.5, fontweight="bold")
    a2.annotate("", xy=(3.3,7.9), xytext=(3.3,7.0),
                arrowprops=dict(arrowstyle="<->", color=CRIT, lw=1.6))
    a2.text(3.45,7.5,"跳变",color=CRIT,fontsize=9)
    fig.tight_layout()
    fig.savefig("图4_LOD与morph.png", facecolor=SURFACE, bbox_inches="tight")
    plt.close(fig)

# =====================================================================
# 图5: 层混合 —— 线性alpha vs 高度混合
# =====================================================================
def fig_blend():
    fig, (a1,a2) = plt.subplots(2,1, figsize=(12,5.6), dpi=140)
    fig.patch.set_facecolor(SURFACE)
    x = np.linspace(0,1,512)
    # 两种材质颜色
    c_rock = np.array([0.55,0.52,0.48])
    c_soil = np.array([0.42,0.28,0.16])

    # 线性 alpha
    a = np.clip((x-0.35)/0.3,0,1)
    img1 = (c_rock[None,:]*(1-a[:,None]) + c_soil[None,:]*a[:,None])
    img1 = np.repeat(img1[None,:,:], 60, axis=0)
    a1.imshow(img1, aspect="auto", extent=[0,1,0,1])
    a1.set_title("线性 alpha 混合:过渡糊、发灰、无细节", fontsize=13, fontweight="bold", color=INK, loc="left")

    # 高度混合: 用两层高度图比较 + sharpness
    rng = np.random.default_rng(3)
    h_rock = 0.5 + 0.25*np.sin(x*40) + 0.15*rng.standard_normal(512).cumsum()/40
    h_soil = 0.5 + 0.25*np.cos(x*33)
    weight = x  # 从左到右偏向 soil
    sharp = 12
    bf = np.clip(((h_soil - h_rock) + (weight-0.5))*sharp + 0.5, 0, 1)
    img2 = (c_rock[None,:]*(1-bf[:,None]) + c_soil[None,:]*bf[:,None])
    img2 = np.repeat(img2[None,:,:], 60, axis=0)
    a2.imshow(img2, aspect="auto", extent=[0,1,0,1])
    a2.set_title("高度混合:石缝露土,过渡犬牙交错更自然", fontsize=13, fontweight="bold", color=INK, loc="left")

    for a in (a1,a2):
        a.set_yticks([]); a.set_xticks([])
        a.text(0.02,0.5,"石",color="#ffffff",fontsize=12,fontweight="bold",va="center")
        a.text(0.96,0.5,"土",color="#ffffff",fontsize=12,fontweight="bold",va="center")
    fig.tight_layout()
    fig.savefig("图5_层混合对比.png", facecolor=SURFACE, bbox_inches="tight")
    plt.close(fig)

# =====================================================================
# 图6: 虚拟纹理 + Clipmap
# =====================================================================
def fig_vt():
    fig, (a1,a2) = plt.subplots(1,2, figsize=(14,6), dpi=140)
    fig.patch.set_facecolor(SURFACE)
    for a in (a1,a2): a.set_facecolor(SURFACE); a.axis("off")

    # 左: VT 大纹理 -> 可见页 -> 物理池
    a1.set_xlim(0,12); a1.set_ylim(0,10)
    a1.set_title("虚拟纹理:超大纹理,只把可见页调进显存", fontsize=13.5, fontweight="bold", color=INK, loc="left")
    # 大虚拟纹理 8x8
    n=8; s=0.62; ox,oy=0.3,1.6
    vis = {(3,4),(4,4),(4,3),(3,3),(5,4),(4,5)}
    for i in range(n):
        for j in range(n):
            fc = BLUE if (i,j) in vis else "#16283f"
            a1.add_patch(Rectangle((ox+i*s, oy+j*s), s*0.94, s*0.94, fc=fc, ec=BASE, lw=0.6))
    a1.text(ox, oy+n*s+0.25, "逻辑虚拟纹理 (如 16K×16K)", color=INK2, fontsize=10)
    # 物理池
    px,py=8.3,3.5
    for k,(i,j) in enumerate(sorted(vis)):
        r,c = k//3, k%3
        a1.add_patch(Rectangle((px+c*0.8, py+r*0.8), 0.72,0.72, fc=BLUE, ec=BASE, lw=0.6))
    a1.text(px, py+2.5+0.2, "物理页池 (小,只存可见页)", color=INK2, fontsize=10)
    arrow(a1, ox+n*s+0.1, oy+n*s/2, px-0.3, py+1.0, color=MUTED, lw=2.2)
    a1.text(ox+n*s+0.15, oy+n*s/2+0.4, "按需\n调入", color=GREEN, fontsize=9.5, fontweight="bold")

    # 右: Clipmap 同心层
    a2.set_xlim(0,10); a2.set_ylim(0,10)
    a2.set_title("Clipmap:以相机为心,近处高精、远处低精", fontsize=13.5, fontweight="bold", color=INK, loc="left")
    cx,cy=5,4.8
    layers=[(3.6,"#16324f","L2 低精 大范围"),(2.4,"#3a6ea5","L1 中精"),(1.2,BLUE,"L0 高精 近相机")]
    for r,col,lab in layers:
        a2.add_patch(Rectangle((cx-r,cy-r),2*r,2*r, fc=col, ec="#242422", lw=2, zorder=1))
    a2.plot(cx,cy, marker="o", ms=12, color=CRIT, zorder=5)
    a2.text(cx,cy-0.5,"相机",ha="center",color="#ffffff",fontsize=9,fontweight="bold")
    for r,col,lab in layers:
        a2.text(cx, cy+r+0.15, lab, ha="center", color=INK2, fontsize=9.2)
    a2.text(0.3,0.4,"内存:全场景高精 100MB → clipmap 仅 ~2.5MB (三角洲数据)",
            color=GREEN, fontsize=9.6, fontweight="bold")
    fig.tight_layout()
    fig.savefig("图6_虚拟纹理与clipmap.png", facecolor=SURFACE, bbox_inches="tight")
    plt.close(fig)

# =====================================================================
# 图7: Mipmap vs Clipmap vs Virtual Texture 对照
# =====================================================================
def fig_texcompare():
    fig, (a1,a2,a3) = plt.subplots(1,3, figsize=(16,6.4), dpi=140)
    fig.patch.set_facecolor(SURFACE)
    for a in (a1,a2,a3):
        a.set_facecolor(SURFACE); a.set_xlim(0,10); a.set_ylim(0,10); a.axis("off")

    # 金字塔层参数 (bottom=最细=最大)
    hw   = [4.0, 3.0, 2.1, 1.3, 0.7]     # 半宽
    y0   = [0.8, 2.15, 3.5, 4.85, 6.2]   # 各层底
    hh   = 1.15
    labs = ["L0 8K","L1 4K","L2 2K","L3 1K","L4 512"]
    cx   = 5.0

    # ---- A: Mipmap 完整金字塔,全存 ----
    a1.set_title("Mipmap  完整金字塔", fontsize=14, fontweight="bold", color=INK, loc="center")
    for h, y, lab in zip(hw, y0, labs):
        a1.add_patch(Rectangle((cx-h, y), 2*h, hh, fc="#1c3a26", ec=GREEN, lw=1.8))
        a1.text(cx, y+hh/2, lab, ha="center", va="center", fontsize=9.5, color=INK)
    a1.text(cx, 0.15, "每级完整存储 → 大图整张放不下", ha="center", fontsize=10.5, color=CRIT, fontweight="bold")

    # ---- B: Clipmap 大层裁成固定窗口 ----
    a2.set_title("Clipmap  裁成固定窗口", fontsize=14, fontweight="bold", color=INK, loc="center")
    bw = 1.5   # clip 窗口半宽
    for h, y, lab in zip(hw, y0, labs):
        # 完整层轮廓(灰,示意)
        a2.add_patch(Rectangle((cx-h, y), 2*h, hh, fc="none", ec=BASE, lw=1, ls="--"))
        # 实际保留 = 与窗口交集(宽层被裁到窗口 → 同尺寸)
        keep = min(h, bw)
        a2.add_patch(Rectangle((cx-keep, y), 2*keep, hh, fc="#1c3350", ec=BLUE, lw=1.9))
        a2.text(cx, y+hh/2, lab, ha="center", va="center", fontsize=9, color=INK)
    a2.plot([cx-bw,cx-bw],[0.6,y0[-1]+hh+0.1], color=ORANGE, lw=1.4, ls=":")
    a2.plot([cx+bw,cx+bw],[0.6,y0[-1]+hh+0.1], color=ORANGE, lw=1.4, ls=":")
    a2.text(cx+bw+0.15, 4.6, "固定\n窗口", color=ORANGE, fontsize=9.5, fontweight="bold")
    a2.plot(cx, 0.55, marker="o", ms=11, color=CRIT, zorder=5)
    a2.text(cx, 0.15, "宽层裁到同尺寸 → N 张一样大 · 相机居中", ha="center", fontsize=10.2, color=BLUE, fontweight="bold")

    # ---- C: Virtual Texture 切页按需驻留 ----
    a3.set_title("Virtual Texture  切页按需驻留", fontsize=14, fontweight="bold", color=INK, loc="center")
    n=7; s=0.72; ox,oy=0.6,2.6
    vis={(2,3),(3,3),(3,2),(2,2),(4,3),(3,4)}
    for i in range(n):
        for j in range(n):
            fc = BLUE if (i,j) in vis else "#16283f"
            a3.add_patch(Rectangle((ox+i*s, oy+j*s), s*0.94, s*0.94, fc=fc, ec=BASE, lw=0.5))
    a3.text(ox, oy+n*s+0.2, "逻辑巨图(1 张,切成页)", fontsize=10, color=INK2)
    # 页表 -> 物理池
    px,py=7.0,3.4
    for k,ij in enumerate(sorted(vis)):
        r,c=k//3,k%3
        a3.add_patch(Rectangle((px+c*0.62, py+r*0.62), 0.55,0.55, fc=BLUE, ec=BASE, lw=0.5))
    a3.text(px, py+1.5, "物理页池", fontsize=10, color=INK2)
    arrow(a3, ox+n*s+0.05, oy+n*s/2, px-0.2, py+0.6, color=MUTED, lw=2)
    a3.text(ox+n*s-0.1, oy+n*s/2+0.35, "页表\n映射", color=GREEN, fontsize=9, fontweight="bold")
    a3.text(5, 0.5, "只驻留可见页 · 页表间接寻址 · 内存随可见内容变", ha="center", fontsize=10.2, color=GREEN, fontweight="bold")

    fig.suptitle("\"纹理太大装不下\" 的三条路 —— 同尺寸(Clipmap) ≠ 切页(Virtual Texture)",
                 fontsize=16, fontweight="bold", color=INK, y=0.99)
    fig.tight_layout(rect=[0,0,1,0.95])
    fig.savefig("图7_Mipmap_Clipmap_VT对比.png", facecolor=SURFACE, bbox_inches="tight")
    plt.close(fig)

# =====================================================================
# 图8: Clipmap 环形更新 (toroidal update)
# =====================================================================
def fig_toroidal():
    fig, (a1,a2,a3) = plt.subplots(1,3, figsize=(16.5,6.2), dpi=140)
    fig.patch.set_facecolor(SURFACE)
    for a in (a1,a2,a3):
        a.set_facecolor(SURFACE); a.set_xlim(0,10); a.set_ylim(0,10); a.axis("off")

    # ---- ① 世界空间:窗口随相机滑动 ----
    a1.set_title("① 相机移动 → 窗口滑动(世界空间)", fontsize=13.5, fontweight="bold", color=INK)
    ox0,oy0,W = 1.4,1.4,5.2          # 旧窗口
    dx,dy = 1.4,1.4                  # 位移
    # 复用区(交集)
    a1.add_patch(Rectangle((ox0+dx, oy0+dy), W-dx, W-dy, fc="#16283f", ec="none", zorder=1))
    # 新进入 L 形(new - old):右条 + 上条
    a1.add_patch(Rectangle((ox0+W, oy0+dy, ), dx, W-dy, fc="#1c3a26", ec=GREEN, lw=1.4, zorder=2))
    a1.add_patch(Rectangle((ox0+dx, oy0+W), W, dx, fc="#1c3a26", ec=GREEN, lw=1.4, zorder=2))
    # 离开 L 形(old - new):左条 + 下条
    a1.add_patch(Rectangle((ox0, oy0), dx, W, fc="none", ec=MUTED, lw=1.2, hatch="///", zorder=2))
    a1.add_patch(Rectangle((ox0+dx, oy0), W-dx, dy, fc="none", ec=MUTED, lw=1.2, hatch="///", zorder=2))
    # 窗口轮廓
    a1.add_patch(Rectangle((ox0,oy0), W, W, fc="none", ec=BASE, lw=1.8, ls="--", zorder=3))
    a1.add_patch(Rectangle((ox0+dx,oy0+dy), W, W, fc="none", ec=BLUE, lw=2.2, zorder=3))
    # 相机
    a1.plot(ox0+W/2, oy0+W/2, marker="o", ms=10, mfc="none", mec=CRIT, mew=1.8, zorder=5)
    a1.plot(ox0+dx+W/2, oy0+dy+W/2, marker="o", ms=11, color=CRIT, zorder=5)
    arrow(a1, ox0+W/2, oy0+W/2, ox0+dx+W/2-0.15, oy0+dy+W/2-0.15, color=CRIT, lw=1.8)
    a1.text(0.3,0.5,"绿=新进入   斜纹=离开   浅蓝=复用(不动)", fontsize=9.6, color=INK2)
    a1.text(ox0+dx+W-0.2, oy0+dy+W+0.15, "新窗口", color=BLUE, fontsize=9.5, fontweight="bold")

    # ---- ② 只上传 L 形边条 ----
    a2.set_title("② 只上传 L 形新边条", fontsize=13.5, fontweight="bold", color=INK)
    n=6; s=1.25; gx,gy=1.5,1.6
    for i in range(n):
        for j in range(n):
            new = (i==n-1) or (j==n-1)   # 右列 或 上行
            fc = "#1c3a26" if new else "#16283f"
            ec = GREEN if new else BASE
            a2.add_patch(Rectangle((gx+i*s, gy+j*s), s*0.95, s*0.95, fc=fc, ec=ec, lw=1.2))
    a2.text(gx, gy+n*s+0.2, "相机右上移 → 仅需右边一列 + 上边一行", fontsize=10, color=GREEN, fontweight="bold")
    a2.text(gx, 0.5, "其余 ~97% texel 完全不动", fontsize=10, color=INK2)

    # ---- ③ 环形写入 + 取模寻址 ----
    a3.set_title("③ 环形写入:新数据覆盖最旧(物理贴图)", fontsize=13.5, fontweight="bold", color=INK)
    n=6; s=1.2; hx,hy=1.6,1.6
    for i in range(n):
        for j in range(n):
            wrap = (i==0) or (j==0)       # 物理最左列/最下行 = 最旧,被覆盖
            fc = BLUE if wrap else "#16283f"
            a3.add_patch(Rectangle((hx+i*s, hy+j*s), s*0.95, s*0.95,
                                   fc=fc, ec=BASE, lw=0.9))
    # wrap 箭头:右边缘 → 左, 上边缘 → 下
    a3.annotate("", xy=(hx-0.15, hy+2.5*s), xytext=(hx+n*s+0.15, hy+2.5*s),
                arrowprops=dict(arrowstyle="->", color=ORANGE, lw=1.8,
                                connectionstyle="arc3,rad=-0.35"))
    a3.annotate("", xy=(hx+2.5*s, hy-0.15), xytext=(hx+2.5*s, hy+n*s+0.15),
                arrowprops=dict(arrowstyle="->", color=ORANGE, lw=1.8,
                                connectionstyle="arc3,rad=0.35"))
    a3.text(hx+n*s+0.2, hy+2.5*s, "右缘\nwrap→左", color=ORANGE, fontsize=8.8, fontweight="bold", va="center")
    a3.text(hx+2.5*s, hy+n*s+0.35, "上缘 wrap→下", color=ORANGE, fontsize=8.8, fontweight="bold", ha="center")
    a3.text(hx, hy+n*s+0.9, "蓝=刚写入(覆盖最旧数据)", fontsize=10, color=BLUE, fontweight="bold")
    a3.text(hx, 0.5, "采样: (逻辑坐标 + origin) % 尺寸", fontsize=10.5, color=INK, fontweight="bold")

    fig.suptitle("Clipmap 环形更新 (toroidal) —— 不搬数据,只写新边条 + 取模寻址",
                 fontsize=16, fontweight="bold", color=INK, y=0.99)
    fig.tight_layout(rect=[0,0,1,0.94])
    fig.savefig("图8_clipmap环形更新.png", facecolor=SURFACE, bbox_inches="tight")
    plt.close(fig)

# =====================================================================
# 图9: 两层解耦 + 数据驻留决策树
# =====================================================================
def fig_layers():
    fig,(a1,a2)=plt.subplots(1,2,figsize=(16,6.8),dpi=140)
    fig.patch.set_facecolor(SURFACE)
    for a in (a1,a2): a.set_facecolor(SURFACE); a.set_xlim(0,10); a.set_ylim(0,10); a.axis("off")

    # ---- 左: 两层正交 ----
    a1.set_title("两层解耦:几何 LOD 与 数据驻留(正交)", fontsize=14, fontweight="bold", color=INK, loc="left")
    a1.add_patch(FancyBboxPatch((0.4,6.0),9.2,2.7,boxstyle="round,pad=0.02,rounding_size=0.05",fc="#16283f",ec=BLUE,lw=1.8))
    a1.text(0.75,8.25,"几何 LOD 层",fontsize=13,fontweight="bold",color=BLUE)
    a1.text(0.75,7.45,"CDLOD = CPU 四叉树选块  +  VS 逐顶点 morph",fontsize=11,color=INK)
    a1.text(0.75,6.7,"决定:顶点/三角形怎么随距离减少 + 无缝（必需）",fontsize=9.6,color=INK2)
    a1.add_patch(FancyBboxPatch((0.4,1.2),9.2,3.3,boxstyle="round,pad=0.02,rounding_size=0.05",fc="#152a1c",ec=GREEN,lw=1.8))
    a1.text(0.75,4.1,"数据驻留层",fontsize=13,fontweight="bold",color=GREEN)
    a1.text(0.75,3.45,"决定:数据放哪、装不下怎么办（可选）",fontsize=9.6,color=INK2)
    for i,o in enumerate(["全量驻留","Clipmap","VT","Component流送"]):
        box(a1,0.7+i*2.25,1.55,2.05,0.95,"#242422",GREEN,o,tc=INK,fs=10.5,bold=True)
    a1.annotate("",xy=(5,4.5),xytext=(5,6.0),arrowprops=dict(arrowstyle="<->",color=MUTED,lw=1.8))
    a1.text(5.2,5.25,"正交\n各选各的",fontsize=9.5,color=MUTED,fontweight="bold")

    # ---- 右: 决策树 ----
    a2.set_title("数据驻留怎么选", fontsize=14, fontweight="bold", color=INK, loc="left")
    box(a2,2.6,8.2,4.8,1.2,"#242422",INK,"整张(带mip)heightmap\n装得进显存?",tc=INK,fs=11,bold=True)
    arrow(a2,3.6,8.2,1.9,7.0); a2.text(2.1,7.55,"是",color=GOOD,fontsize=11,fontweight="bold")
    box(a2,0.2,5.6,3.7,1.4,"#152a1c",GOOD,"全量驻留\n硬件 mip 直接采样\n(不要 clipmap)",tc=INK,fs=9.6)
    arrow(a2,6.4,8.2,8.0,7.0); a2.text(7.6,7.55,"否",color=CRIT,fontsize=11,fontweight="bold")
    box(a2,5.9,5.6,3.9,1.4,"#242422",INK,"相机居中 / 范围规则?",tc=INK,fs=10.5,bold=True)
    arrow(a2,6.6,5.6,5.0,4.0); a2.text(5.1,4.7,"是",color=BLUE,fontsize=10,fontweight="bold")
    box(a2,2.5,2.4,3.7,1.5,"#16283f",BLUE,"Clipmap\n确定性·无feedback\n简单·整圈",tc=INK,fs=9.6)
    arrow(a2,8.4,5.6,8.6,4.0); a2.text(8.7,4.7,"否",color=VIOLET,fontsize=10,fontweight="bold")
    box(a2,6.4,2.4,3.5,1.5,"#231d34",VIOLET,"VT\n按需+feedback\n最省·复杂",tc=INK,fs=9.6)

    fig.suptitle("CDLOD(几何) 与 数据驻留(clipmap/VT) 是两层,正交解耦",
                 fontsize=15.5, fontweight="bold", color=INK, y=0.99)
    fig.tight_layout(rect=[0,0,1,0.94])
    fig.savefig("图9_两层解耦与决策.png", facecolor=SURFACE, bbox_inches="tight")
    plt.close(fig)

# =====================================================================
# 图10: CDLOD 机制 —— 四叉树划分 + 同一网格缩放 + 省instance
# =====================================================================
def fig_cdlod_mech():
    fig,(a1,a2)=plt.subplots(1,2,figsize=(15.5,7),dpi=140,gridspec_kw={"width_ratios":[1.1,1]})
    fig.patch.set_facecolor(SURFACE)
    for a in (a1,a2): a.set_facecolor(SURFACE); a.axis("off")

    # ---- 左: 世界空间四叉树划分(近小远大, 无重叠无空隙), 每块同一网格 ----
    a1.set_xlim(0,8); a1.set_ylim(0,8)
    a1.set_title("四叉树划分:近小远大,无重叠无空隙", fontsize=13.5, fontweight="bold", color=INK, loc="left")
    def cell(x,y,s,col):
        a1.add_patch(Rectangle((x,y),s,s,fc="none",ec=col,lw=2,zorder=3))
        n=4
        for k in range(1,n):
            a1.plot([x+k*s/n,x+k*s/n],[y,y+s],color=col,lw=0.5,alpha=0.5,zorder=2)
            a1.plot([x,x+s],[y+k*s/n,y+k*s/n],color=col,lw=0.5,alpha=0.5,zorder=2)
    # 远处大块(浅色,少)
    cell(0,4,4,YEL); cell(4,4,4,YEL); cell(4,0,4,YEL)
    # 近处(左下)细分为中块
    cell(0,2,2,AQUA); cell(2,2,2,AQUA); cell(2,0,2,AQUA)
    # 最近(相机处)再细分为小块
    cell(0,0,1,BLUE); cell(1,0,1,BLUE); cell(0,1,1,BLUE); cell(1,1,1,BLUE)
    a1.plot(0.5,0.5,marker="o",ms=12,color=CRIT,zorder=6)
    a1.text(0.5,0.5+0.35,"相机",ha="center",color=CRIT,fontsize=9,fontweight="bold",zorder=6)
    a1.text(6,7.3,"大块=远(instance少)",color=YEL,fontsize=10,fontweight="bold")
    a1.text(2.1,3.2,"中块",color=AQUA,fontsize=9.5,fontweight="bold")
    a1.text(1.15,1.15,"小块=近(instance多)",color=BLUE,fontsize=9.5,fontweight="bold")
    a1.text(0,-0.5,"每个块都是同一张4×4网格,只是缩放不同 → 大块格子大(疏)、小块格子小(密)",fontsize=9.4,color=INK2)

    # ---- 右: 同一单位网格缩放 + 省instance ----
    a2.set_xlim(0,10); a2.set_ylim(0,10)
    a2.set_title("同一张单位网格,缩放复用", fontsize=13.5, fontweight="bold", color=INK, loc="left")
    # 单位网格
    gx,gy,gs=0.5,6.4,2.4; n=4
    a2.add_patch(Rectangle((gx,gy),gs,gs,fc="#16283f",ec=INK,lw=1.8))
    for k in range(1,n):
        a2.plot([gx+k*gs/n,gx+k*gs/n],[gy,gy+gs],color=MUTED,lw=0.6)
        a2.plot([gx,gx+gs],[gy+k*gs/n,gy+k*gs/n],color=MUTED,lw=0.6)
    a2.text(gx,gy+gs+0.2,"单位网格(顶点数固定,如33×33)",fontsize=10,color=INK,fontweight="bold")
    # 近块(小)
    arrow(a2,gx+gs+0.1,gy+gs*0.6,4.6,8.0)
    a2.add_patch(Rectangle((4.7,7.3),1.0,1.0,fc="none",ec=BLUE,lw=1.8))
    for k in range(1,n):
        a2.plot([4.7+k*1.0/n,4.7+k*1.0/n],[7.3,8.3],color=BLUE,lw=0.4)
        a2.plot([4.7,5.7],[7.3+k*1.0/n,7.3+k*1.0/n],color=BLUE,lw=0.4)
    a2.text(5.9,7.8,"×小 → 近块(覆盖64m,密)",color=BLUE,fontsize=9.5,va="center")
    # 远块(大)
    arrow(a2,gx+gs+0.1,gy+gs*0.35,4.6,5.4)
    a2.add_patch(Rectangle((4.7,4.0),2.6,2.6,fc="none",ec=YEL,lw=1.8))
    for k in range(1,n):
        a2.plot([4.7+k*2.6/n,4.7+k*2.6/n],[4.0,6.6],color=YEL,lw=0.4)
        a2.plot([4.7,7.3],[4.0+k*2.6/n,4.0+k*2.6/n],color=YEL,lw=0.4)
    a2.text(7.5,5.3,"×大 → 远块(覆盖2km,疏)",color="#f0c04a",fontsize=9.5,va="center")
    # punchline
    box(a2,0.5,1.4,9.0,1.9,"#152a1c",GREEN,"",pad=0.01)
    a2.text(5.0,2.75,"顶点数一样(1089),变的只是覆盖范围",ha="center",fontsize=11,color=INK,fontweight="bold")
    a2.text(5.0,1.95,"同样一片 2km²:近处≈1000个instance,远处1个 → CDLOD 省的是 instance 数量",
            ha="center",fontsize=10,color=GREEN,fontweight="bold")

    fig.suptitle("CDLOD:同一网格缩放 + 四叉树划分,远处靠“块少”省顶点",
                 fontsize=15.5, fontweight="bold", color=INK, y=0.99)
    fig.tight_layout(rect=[0,0,1,0.93])
    fig.savefig("图10_CDLOD机制.png", facecolor=SURFACE, bbox_inches="tight")
    plt.close(fig)

# =====================================================================
# 图11: per-node LOD级 vs per-vertex morph
# =====================================================================
def fig_morph_gran():
    fig,ax=plt.subplots(figsize=(14,6),dpi=140)
    fig.patch.set_facecolor(SURFACE); ax.set_facecolor(SURFACE)
    ax.set_xlim(0,12); ax.set_ylim(0,8); ax.axis("off")
    ax.set_title("LOD 级别 = per-node   |   morphK = per-vertex", fontsize=15, fontweight="bold", color=INK, loc="left")

    # 相机
    ax.plot(0.4,4,marker="o",ms=13,color=CRIT,zorder=6); ax.text(0.4,3.3,"相机",ha="center",color=CRIT,fontsize=9.5,fontweight="bold")

    # 两个相邻节点 A(近) B(远), 共享中间边. morphK 随距离(左→右)渐变
    grad=np.linspace(0,1,256)[None,:]
    ax.imshow(grad, extent=[2,6.9,2,6], aspect="auto", cmap="Blues", vmin=-0.3, vmax=1.2, zorder=1)
    ax.imshow(np.linspace(0.35,1,256)[None,:], extent=[7.1,11.5,2,6], aspect="auto", cmap="Blues", vmin=-0.3, vmax=1.2, zorder=1)
    for x0,x1 in [(2,6.9),(7.1,11.5)]:
        ax.add_patch(Rectangle((x0,2),x1-x0,4,fc="none",ec=INK,lw=2,zorder=3))
        for k in range(1,6):
            ax.plot([x0+k*(x1-x0)/6]*2,[2,6],color="#ffffff",lw=0.5,alpha=0.6,zorder=2)
    ax.text(4.45,6.25,"节点 A (级别 L1)",ha="center",fontsize=11,fontweight="bold",color=INK)
    ax.text(9.3,6.25,"节点 B (级别 L2)",ha="center",fontsize=11,fontweight="bold",color=INK)

    # per-node 注释
    ax.annotate("LOD 级别:整块一个值\n(决定网格密度/在哪两级 blend)\n由 CPU 四叉树定",
                xy=(4.45,5.5),xytext=(3.0,7.0),fontsize=9.5,color=INK2,
                arrowprops=dict(arrowstyle="->",color=MUTED,lw=1.2))
    # per-vertex 注释
    ax.text(2,1.4,"morphK: 0 (近·全细)",color=BLUE,fontsize=10,fontweight="bold")
    ax.text(9.8,1.4,"→ 1 (远·全morph成粗)",color="#9ec5f4",fontsize=10,fontweight="bold")
    ax.annotate("每个顶点各算各的\n(用自身到相机距离)",xy=(5.5,3),xytext=(5.0,0.4),
                fontsize=9.5,color=BLUE,ha="center",arrowprops=dict(arrowstyle="->",color=BLUE,lw=1.2))

    # 共享边对齐
    ax.plot([7.0,7.0],[2,6],color=GREEN,lw=3,zorder=5)
    ax.annotate("共享边:两侧顶点距离相同\n→ morphK 相同 → 对齐,无裂缝",
                xy=(7.0,4),xytext=(7.6,7.0),fontsize=9.6,color=GREEN,fontweight="bold",
                arrowprops=dict(arrowstyle="->",color=GREEN,lw=1.4))

    fig.tight_layout()
    fig.savefig("图11_morph粒度.png", facecolor=SURFACE, bbox_inches="tight")
    plt.close(fig)

# =====================================================================
# 图12: 后备存储层级 + 换出语义 + clipmap vs VT
# =====================================================================
def fig_backing():
    fig,(a1,a2)=plt.subplots(1,2,figsize=(15.5,6.6),dpi=140)
    fig.patch.set_facecolor(SURFACE)
    for a in (a1,a2): a.set_facecolor(SURFACE); a.set_xlim(0,10); a.set_ylim(0,10); a.axis("off")

    # ---- 左: 显存外 <-> 显存内 ----
    a1.set_title("后备存储在显存外;换出=覆盖丢弃", fontsize=13.5, fontweight="bold", color=INK, loc="left")
    a1.add_patch(FancyBboxPatch((0.4,6.3),9.2,2.4,boxstyle="round,pad=0.02,rounding_size=0.05",fc="#26251f",ec=MUTED,lw=1.8))
    a1.text(0.7,8.35,"显存外(后备/权威,可以很大)",fontsize=11.5,fontweight="bold",color=INK)
    for i,o in enumerate(["SSD 资产文件","系统内存 RAM","程序化生成"]):
        box(a1,0.75+i*3.0,6.6,2.8,0.95,"#242422",MUTED,o,tc=INK,fs=10,bold=True)
    a1.add_patch(FancyBboxPatch((0.4,1.4),9.2,2.4,boxstyle="round,pad=0.02,rounding_size=0.05",fc="#16283f",ec=BLUE,lw=1.8))
    a1.text(0.7,3.45,"显存内(驻留,有限)",fontsize=11.5,fontweight="bold",color=BLUE)
    for i,o in enumerate(["Clipmap 纹理","VT 物理页池","普通贴图"]):
        box(a1,0.75+i*3.0,1.7,2.8,0.95,"#242422",BLUE,o,tc=INK,fs=10,bold=True)
    arrow(a1,2.6,6.3,2.6,3.8,color=GREEN,lw=2.4)
    a1.text(0.4,5.0,"换入\n读盘/读RAM/现算\n→上传",color=GREEN,fontsize=9.5,fontweight="bold")
    a1.annotate("换出 = 原地覆盖丢弃\n(只读数据,不写回)",xy=(7.4,3.9),xytext=(5.6,4.9),
                fontsize=9.6,color=CRIT,fontweight="bold",
                arrowprops=dict(arrowstyle="->",color=CRIT,lw=1.4))

    # ---- 右: clipmap vs VT ----
    a2.set_title("Clipmap vs VT", fontsize=13.5, fontweight="bold", color=INK, loc="left")
    rows=[("驻留决策","相机位置 → 直接算","GPU feedback → 按需"),
          ("要 feedback","不要","要"),
          ("驻留精度","整圈(可能含没采到)","只留真正采到的页"),
          ("复杂度","简单","复杂"),
          ("适合","相机居中·范围规则","精确按可见性")]
    box(a2,0.3,8.3,3.0,1.0,"#16283f",BLUE,"Clipmap",tc=BLUE,fs=12,bold=True)
    box(a2,6.6,8.3,3.0,1.0,"#231d34",VIOLET,"VT",tc=VIOLET,fs=12,bold=True)
    y=7.4
    for name,cv,vv in rows:
        a2.text(0.3,y,name,fontsize=10,fontweight="bold",color=INK)
        a2.text(3.4,y,cv,fontsize=9.5,color=BLUE)
        a2.text(3.4,y-0.42,vv,fontsize=9.5,color=VIOLET)
        a2.plot([0.3,9.6],[y-0.72,y-0.72],color=GRID,lw=0.8)
        y-=1.35

    fig.suptitle("数据驻留:后备在显存外,只读→换出即丢弃;clipmap 确定性、VT 按需",
                 fontsize=15, fontweight="bold", color=INK, y=0.99)
    fig.tight_layout(rect=[0,0,1,0.93])
    fig.savefig("图12_后备存储与驻留对比.png", facecolor=SURFACE, bbox_inches="tight")
    plt.close(fig)

# =====================================================================
# 图13: Biome 组织
# =====================================================================
def fig_biome_org():
    fig,ax=plt.subplots(figsize=(14,7.6),dpi=140); fig.patch.set_facecolor(SURFACE); ax.set_facecolor(SURFACE)
    ax.set_xlim(0,14); ax.set_ylim(0,8.4); ax.axis("off")
    ax.set_title("Biome 组织:单一数据源 → 多消费者", fontsize=16, fontweight="bold", color=INK, loc="left")
    box(ax,4.5,6.6,5,1.15,"#26251f",INK,"Biome Mask\n单一权威源 · 低精常驻",tc=INK,fs=12,bold=True)
    arrow(ax,6.2,6.6,3.4,5.5); arrow(ax,7.8,6.6,10.6,5.5)
    box(ax,0.6,4.2,5,1.3,"#152a1c",GREEN,"PCG(离线)\n放置 植被/石头/道路",tc=INK,fs=11.5,bold=True)
    box(ax,8.4,4.2,5,1.3,"#16283f",BLUE,"材质(FS · opt-in)\ntint / 选图层 / 湿度",tc=INK,fs=11.5,bold=True)
    arrow(ax,3.1,4.2,6.2,2.9); arrow(ax,10.9,4.2,7.8,2.9)
    box(ax,3.8,1.7,6.4,1.15,"#242422",INK,"画面一致:地表 + 植被 + 色调 同步",tc=INK,fs=12.5,bold=True)
    box(ax,0.6,0.25,12.8,0.95,"#2e2611",WARN,"Clipmap 通道打包:RGBA + 空间互斥复用(水/陆共通道)→ 100MB→2.5MB   |   过渡 = 软权重 blend",tc=INK,fs=10.2,bold=True)
    fig.tight_layout(); fig.savefig("图13_Biome组织.png", facecolor=SURFACE, bbox_inches="tight"); plt.close(fig)

# =====================================================================
# 图14: 纹理即空间数据容器
# =====================================================================
def fig_texture_container():
    fig,(a1,a2)=plt.subplots(1,2,figsize=(16,7.4),dpi=140,gridspec_kw={"width_ratios":[1,1.05]})
    fig.patch.set_facecolor(SURFACE)
    for a in (a1,a2): a.set_facecolor(SURFACE); a.set_xlim(0,10); a.set_ylim(0,10); a.axis("off")
    a1.set_title("同一个东西,按内容命名", fontsize=14, fontweight="bold", color=INK, loc="left")
    fam=[("biome mask","环境语义","选层/tint/玩法"),("splat map","材质ID+权重","混地表层"),
         ("heightmap","高度","定顶点/位移"),("tint map","颜色","乘 albedo"),
         ("normal map","法线","光照"),("flow map","方向","水流/毛发"),
         ("SDF","到表面距离","碰撞/软阴影"),("interaction map","动态影响","脚印/压草"),
         ("lightmap","烘焙光照","加光")]
    a1.text(0.2,9.3,"名字",fontsize=10,fontweight="bold",color=MUTED)
    a1.text(3.6,9.3,"存什么",fontsize=10,fontweight="bold",color=MUTED)
    a1.text(6.9,9.3,"干嘛",fontsize=10,fontweight="bold",color=MUTED)
    y=8.6
    for n,s,u in fam:
        a1.text(0.2,y,n,fontsize=10,fontweight="bold",color=BLUE)
        a1.text(3.6,y,s,fontsize=9.5,color=INK)
        a1.text(6.9,y,u,fontsize=9.5,color=INK2)
        a1.plot([0.2,9.7],[y-0.32,y-0.32],color=GRID,lw=0.7); y-=0.9
    a1.text(0.2,0.2,"本质:把空间场编码进纹理,shader 采样",fontsize=10.5,color=GREEN,fontweight="bold")

    a2.set_title("两档 + 4 条设计轴", fontsize=14, fontweight="bold", color=INK, loc="left")
    box(a2,0.3,7.2,9.4,1.9,"#152a1c",GREEN,"",pad=0.01)
    a2.text(0.6,8.7,"静态语义",fontsize=12,fontweight="bold",color=GREEN)
    a2.text(0.6,8.0,"biome / splat / height —— 世界俯视,常驻 / clipmap / VT",fontsize=10,color=INK)
    a2.text(0.6,7.45,"(mask 有人读才生效;不改就不动你的资产)",fontsize=9,color=INK2)
    box(a2,0.3,4.9,9.4,1.9,"#16283f",BLUE,"",pad=0.01)
    a2.text(0.6,6.4,"动态交互",fontsize=12,fontweight="bold",color=BLUE)
    a2.text(0.6,5.7,"脚印 / 压草 / 水坑 / 动态biome —— 玩家中心 RT",fontsize=10,color=INK)
    a2.text(0.6,5.15,"= clipmap(toroidal):画进去 + 采样,和脚印同一套",fontsize=9.5,color=BLUE,fontweight="bold")
    a2.text(0.3,3.9,"4 条设计轴(机制同,选项不同):",fontsize=11,fontweight="bold",color=INK)
    axes=[("坐标空间","世界俯视 / UV / 屏幕"),("驻留","全量 / clipmap / VT"),
          ("读写","静态烘焙 / 每帧渲进去"),("消费者","地表/植被/玩法/音频")]
    y=3.1
    for k,v in axes:
        a2.text(0.6,y,f"• {k}:",fontsize=10,fontweight="bold",color=INK2)
        a2.text(3.0,y,v,fontsize=10,color=INK); y-=0.72
    fig.suptitle("纹理 = GPU 的通用空间数据容器(存什么自定义,名字由内容决定)",
                 fontsize=15.5,fontweight="bold",color=INK,y=0.99)
    fig.tight_layout(rect=[0,0,1,0.94]); fig.savefig("图14_纹理即空间数据容器.png", facecolor=SURFACE, bbox_inches="tight"); plt.close(fig)

# =====================================================================
# 图15: Splat + ID Fixing 全流程
# =====================================================================
def fig_splat_flow():
    fig,ax=plt.subplots(figsize=(15,8),dpi=140); fig.patch.set_facecolor(SURFACE); ax.set_facecolor(SURFACE)
    ax.set_xlim(0,15); ax.set_ylim(0,9); ax.axis("off")
    ax.set_title("Splat Map + ID Fixing:32 层却固定 9 采样", fontsize=16, fontweight="bold", color=INK, loc="left")
    # 传统 vs 三角洲 头对比
    box(ax,0.4,7.4,6.9,1.1,"#331a1a",CRIT,"传统:per-pixel 权重,混 N 层 → 最多 18 采样,层数受限",tc=INK,fs=10.5,bold=True)
    box(ax,7.7,7.4,6.9,1.1,"#152a1c",GOOD,"三角洲:索引 ID + ID Fixing → 固定 9 采样,支持 32 层",tc=INK,fs=10.5,bold=True)
    # 流程 5 步
    steps=[("① 每格点存","Bottom ID(5) + Top ID(5)\n+ Weight(3) = 13bit\n【存纹理 splat ID map】",BLUE),
           ("② 一个格三角形","3 角 × 2 ID = 最多 6 个 ID",AQUA),
           ("③ ID Fixing(离线)","把每三角形 6 ID\n约束到 ≤3 层",YEL),
           ("④ 运行时","point 采 3 角(3, 便宜)\n+ 采 3 层材质(9, 贵)",ORANGE),
           ("⑤ 高度混合","犬牙交错,石缝露土",GREEN)]
    x=0.4; w=2.75
    for i,(t,d,c) in enumerate(steps):
        box(ax,x,4.2,w,2.4,"#242422",c,"",pad=0.01)
        ax.text(x+w/2,6.15,t,ha="center",fontsize=11,fontweight="bold",color=c if c!=YEL else "#f0c04a")
        ax.text(x+w/2,5.2,d,ha="center",fontsize=8.8,color=INK)
        if i>0: arrow(ax,x-0.15,5.4,x+0.02,5.4,color=MUTED,lw=2)
        x+=w+0.17
    # 底部对比表
    box(ax,0.4,0.4,14.2,3.2,"#0d0d0d",BASE,"",pad=0.01)
    ax.text(0.7,3.2,"对比",fontsize=12,fontweight="bold",color=INK)
    rows=[("支持层数","4(堆权重图)","32"),("每像素材质采样","可到 18","固定 9"),
          ("splat 存储","大权重图","格点 13bit(+clipmap)"),("高度混合","常没有","有(公式)")]
    ax.text(3.2,3.2,"传统",fontsize=11,fontweight="bold",color=CRIT)
    ax.text(9.0,3.2,"三角洲",fontsize=11,fontweight="bold",color=GOOD)
    y=2.55
    for n,a,b in rows:
        ax.text(0.7,y,n,fontsize=10.5,fontweight="bold",color=INK)
        ax.text(3.2,y,a,fontsize=10,color=CRIT); ax.text(9.0,y,b,fontsize=10,color=GOOD)
        ax.plot([0.7,14.3],[y-0.28,y-0.28],color=GRID,lw=0.7); y-=0.62
    fig.tight_layout(); fig.savefig("图15_Splat_IDFixing全流程.png", facecolor=SURFACE, bbox_inches="tight"); plt.close(fig)

# =====================================================================
# 图16: ID Fixing 6→3 + PS 重建三角形 + 每层权重
# =====================================================================
def fig_idfix_bary():
    fig,(a1,a2)=plt.subplots(1,2,figsize=(15.5,7),dpi=140); fig.patch.set_facecolor(SURFACE)
    for a in (a1,a2): a.set_facecolor(SURFACE); a.set_xlim(0,10); a.set_ylim(0,10); a.axis("off")
    # 左: PS 重建三角形 + ID Fixing
    a1.set_title("PS 从 splat 格网重建三角形 + ID Fixing", fontsize=13.5, fontweight="bold", color=INK, loc="left")
    # quad 4 texel 中心
    P={"A":(2.5,6.5),"B":(6.5,6.5),"C":(2.5,2.5),"D":(6.5,2.5)}
    for k,(x,y) in P.items():
        a1.add_patch(Rectangle((x-0.28,y-0.28),0.56,0.56,fc=BLUE,ec="#fff",lw=1.2,zorder=4))
    # quad + 对角线
    a1.add_patch(Rectangle((2.5,2.5),4,4,fc="none",ec=BASE,lw=1.4,ls="--"))
    a1.plot([2.5,6.5],[6.5,2.5],color=ORANGE,lw=1.6,ls=":")
    a1.text(6.7,4.5,"对角切\n2 个三角形",color=ORANGE,fontsize=9,fontweight="bold")
    # 像素
    a1.plot(4.0,5.2,marker="*",ms=16,color=CRIT,zorder=5); a1.text(4.15,5.5,"像素",color=CRIT,fontsize=9)
    a1.text(2.2,7.1,"texel中心=格点(each 2 ID)",fontsize=9.5,color=BLUE,fontweight="bold")
    a1.text(0.3,1.6,"frac(worldXZ×scale) 判断在哪个三角形",fontsize=9.6,color=INK2)
    a1.text(0.3,1.0,"point-sample 3 角(离散ID不能bilinear)→ 6 ID",fontsize=9.6,color=INK2)
    a1.text(0.3,0.4,"ID Fixing 离线保证:6 ID 并集 ≤ 3 层",fontsize=10,color=GREEN,fontweight="bold")

    # 右: barycentric 每层权重
    a2.set_title("每层权重 = 3 角展开 + 重心加权", fontsize=13.5, fontweight="bold", color=INK, loc="left")
    a2.text(0.3,9.0,"唯一层 {草=0, 土=1, 石=2}",fontsize=10.5,fontweight="bold",color=INK)
    lines=[("角A {底草,顶土,w0.3}","→ 草0.7  土0.3  石0"),
           ("角B {底土,顶石,w0.6}","→ 草0    土0.4  石0.6"),
           ("角C {底草,顶石,w0.5}","→ 草0.5  土0    石0.5")]
    y=8.1
    for a,b in lines:
        a2.text(0.3,y,a,fontsize=9.6,color=INK); a2.text(5.1,y,b,fontsize=9.6,color=BLUE); y-=0.7
    a2.text(0.3,5.7,"像素重心 (0.2, 0.3, 0.5)  ← 逐像素变",fontsize=10,fontweight="bold",color=CRIT)
    box(a2,0.3,3.0,9.4,2.2,"#152a1c",GREEN,"",pad=0.01)
    a2.text(0.6,4.7,"草 = .2×.7 + .3×0  + .5×.5 = 0.39",fontsize=10.5,color=INK)
    a2.text(0.6,4.1,"土 = .2×.3 + .3×.4 + .5×0  = 0.18",fontsize=10.5,color=INK)
    a2.text(0.6,3.5,"石 = .2×0  + .3×.6 + .5×.5 = 0.43   (和=1)",fontsize=10.5,color=INK)
    a2.text(0.3,2.2,"→ 3 层权重逐像素不同(重心插值)",fontsize=10.5,color=GREEN,fontweight="bold")
    a2.text(0.3,1.5,"→ 喂进高度混合公式,得最终色",fontsize=10,color=INK2)
    fig.suptitle("Splat 运行时:重建格三角形 → point 采 3 角(ID Fixing≤3层)→ 重心算每层权重",
                 fontsize=14.5,fontweight="bold",color=INK,y=0.99)
    fig.tight_layout(rect=[0,0,1,0.93]); fig.savefig("图16_IDFixing与重心权重.png", facecolor=SURFACE, bbox_inches="tight"); plt.close(fig)

# =====================================================================
# 图17: 为什么不干脆提高 splat 分辨率
# =====================================================================
def fig_why_not_highres():
    fig,ax=plt.subplots(figsize=(14,8),dpi=140); fig.patch.set_facecolor(SURFACE); ax.set_facecolor(SURFACE)
    ax.set_xlim(0,14); ax.set_ylim(0,9); ax.axis("off")
    ax.set_title("为什么不提高 splat 分辨率、每像素只采 1 次?", fontsize=15.5, fontweight="bold", color=INK, loc="left")
    cards=[("① 存储爆炸",CRIT,"10km @ 10cm ≈ 10^10 texel\n≈ 20GB,大世界不可行"),
           ("② ID 闪烁(最致命)",CRIT,"远处 minify:1 像素盖多 texel\n离散 ID 不能 mip → 随机点采\n相机一动 → 材质乱跳 shimmer"),
           ("③ 带宽 ≠ 采样数",WARN,"大图缩小 → cache miss 暴涨\n'1 次采大图' 真带宽反而更高\n(移动端 TBDR 尤甚)"),
           ("④ 固定分辨率两头不讨好",WARN,"近(放大)不够 → 糊\n远(缩小)过头 → 闪\n没有单一分辨率全距离匹配")]
    pos=[(0.4,4.7),(7.2,4.7),(0.4,0.5),(7.2,0.5)]
    for (t,c,d),(x,y) in zip(cards,pos):
        box(ax,x,y,6.4,3.6,"#242422",c,"",pad=0.01)
        ax.text(x+0.3,y+3.05,t,fontsize=13,fontweight="bold",color=c if c!=WARN else "#f0c04a")
        ax.text(x+0.3,y+1.7,d,fontsize=10.5,color=INK,va="center")
    fig.tight_layout(); fig.savefig("图17_为什么不提高分辨率.png", facecolor=SURFACE, bbox_inches="tight"); plt.close(fig)

# =====================================================================
# 图18: FPS Landscape 渲染之外的坑
# =====================================================================
def fig_fps_pitfalls():
    fig,ax=plt.subplots(figsize=(15,8.2),dpi=140); fig.patch.set_facecolor(SURFACE); ax.set_facecolor(SURFACE)
    ax.set_xlim(0,15); ax.set_ylim(0,9); ax.axis("off")
    ax.set_title("FPS Landscape:一半难度在“渲染之外”", fontsize=16, fontweight="bold", color=INK, loc="left")
    items=[("渲染网格 ≠ 碰撞网格","CDLOD 是渲染用;命中/脚步/载具\n走独立碰撞高度场,两套要对齐"),
           ("服务器有地形但不渲染","命中判定/视线/移动/AI\n跑无渲染的轻量高度场"),
           ("多人确定性","碰撞/植被/掩体 全端+服务器一致\n否则不公平/穿帮"),
           ("草的公平性","能藏人的植被 渲染距离强制统一\n不能随画质变(PUBG 经典事故)"),
           ("ADS/瞄准镜 LOD","开镜要远处高细节,和常规 LOD 冲突\n→ 特殊提精度(mip bias)"),
           ("破坏/弹坑","炸坑要同时改 渲染+碰撞+网络同步\n动态地形,难一个量级"),
           ("高度场做不了悬崖/洞穴","每 XY 只一个高度 → 无倒挂/隧道\n要 mesh/voxel 补 + 融合"),
           ("全局低精高度场复用","自阴影/云影/AO/水反射/AI视线\n一图多用")]
    cols=2; cw=7.0; ch=1.85; gx=0.4; gy0=6.7
    for i,(t,d) in enumerate(items):
        r,c=i//cols,i%cols
        x=gx+c*(cw+0.2); y=gy0-r*(ch+0.12)
        col=CRIT if i<4 else BLUE
        box(ax,x,y,cw,ch,"#242422",col,"",pad=0.008)
        ax.text(x+0.25,y+ch-0.42,("★ " if i<4 else "")+t,fontsize=11.5,fontweight="bold",color=col)
        ax.text(x+0.25,y+0.55,d,fontsize=9.4,color=INK2,va="center")
    ax.text(0.4,0.25,"★ = 联机 FPS 生死线   |   画面只是冰山一角",fontsize=10.5,color=INK,fontweight="bold")
    fig.tight_layout(); fig.savefig("图18_FPS地形渲染之外的坑.png", facecolor=SURFACE, bbox_inches="tight"); plt.close(fig)

# =====================================================================
# 图19: 三角洲进阶技术点汇总
# =====================================================================
def fig_delta_advanced():
    fig,ax=plt.subplots(figsize=(14,7.6),dpi=140); fig.patch.set_facecolor(SURFACE); ax.set_facecolor(SURFACE)
    ax.set_xlim(0,14); ax.set_ylim(0,9); ax.axis("off")
    ax.set_title("三角洲 Landscape 进阶技术点", fontsize=16, fontweight="bold", color=INK, loc="left")
    rows=[("悬崖 Tri-planar 烘进 VT","离线选最佳投影面+stochastic接缝 → 近零每帧开销"),
          ("远景 SVT 法线","离线烘高精法线,和 RVT 共享物理纹理 → 零额外内存"),
          ("Soft Tessellation","CDLOD 加 LOD -1/-2,4×4 细分 → 0.15ms vs 硬件 0.62ms"),
          ("VT 运行时 ASTC 压缩","256² 页 0.2ms,带宽降 1/4~1/9"),
          ("ADS 开镜 mip bias","锁 mip 防抖(离散/mip 问题)"),
          ("深水优化","看不见的深水下地形关法线/高光/阴影/GI"),
          ("道路 Z-fighting(低端)","2m 拓扑匹配 + VS 远距上抬"),
          ("移动 RT 优化","Frame Buffer Fetch 重排通道,减 RT 数")]
    y=7.6
    for t,d in rows:
        box(ax,0.4,y-0.75,4.4,0.7,"#16283f",BLUE,t,tc=INK,fs=10.5,bold=True)
        ax.text(5.1,y-0.4,d,fontsize=10,color=INK,va="center")
        y-=0.92
    fig.tight_layout(); fig.savefig("图19_三角洲进阶技术点.png", facecolor=SURFACE, bbox_inches="tight"); plt.close(fig)

fig_glossary()
fig_pipeline()
fig_radar()
fig_lod()
fig_blend()
fig_vt()
fig_texcompare()
fig_toroidal()
fig_layers()
fig_cdlod_mech()
fig_morph_gran()
fig_backing()
fig_biome_org()
fig_texture_container()
fig_splat_flow()
fig_idfix_bary()
fig_why_not_highres()
fig_fps_pitfalls()
fig_delta_advanced()
print("done: 19 figures")
