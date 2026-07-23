# -*- coding: utf-8 -*-
"""① 静态网格 的图。"""
import numpy as np
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
from matplotlib.patches import FancyBboxPatch, FancyArrowPatch, Rectangle, Polygon

plt.rcParams["font.family"]="Microsoft YaHei"; plt.rcParams["axes.unicode_minus"]=False
SURFACE="#1a1a19";INK="#f0efe9";INK2="#c3c2b7";MUTED="#898781";GRID="#2c2c2a";BASE="#383835";PANEL="#242422"
BLUE="#3987e5";AQUA="#199e70";YEL="#c98500";GREEN="#33a833";VIOLET="#9085e9";RED="#e05a5a";ORANGE="#d95926"
GOOD="#0ca30c";WARN="#fab219";CRIT="#e0574f"

def box(ax,x,y,w,h,fc,ec,txt="",tc=INK,fs=10,bold=False,pad=0.02):
    ax.add_patch(FancyBboxPatch((x,y),w,h,boxstyle=f"round,pad={pad},rounding_size=0.03",fc=fc,ec=ec,lw=1.7,zorder=2))
    if txt: ax.text(x+w/2,y+h/2,txt,ha="center",va="center",color=tc,fontsize=fs,fontweight="bold" if bold else "normal",zorder=3)
def arrow(ax,x1,y1,x2,y2,color=MUTED,lw=2):
    ax.add_patch(FancyArrowPatch((x1,y1),(x2,y2),arrowstyle="-|>",mutation_scale=16,color=color,lw=lw,zorder=1))

# 图1: 数据构成
def fig_data():
    fig,ax=plt.subplots(figsize=(15,6),dpi=140);fig.patch.set_facecolor(SURFACE);ax.set_facecolor(SURFACE)
    ax.set_xlim(0,15);ax.set_ylim(0,7);ax.axis("off")
    ax.set_title("静态网格实例:渲染数据构成",fontsize=16,fontweight="bold",color=INK,loc="left")
    cats=[("几何 Geometry",BLUE,["Vertex Buffer","  pos/normal/tangent","  UV/color","Index Buffer"]),
          ("材质 Material",AQUA,["Shader / PSO","Textures","参数 params"]),
          ("变换 Transform",YEL,["World Matrix","(每实例一个)"]),
          ("剔除 Culling",ORANGE,["Bounds","  AABB / Sphere"]),
          ("LOD",GREEN,["LOD0(近·高模)","LOD1 …","LODn(远·低模)"])]
    w=2.75;x=0.35
    for t,c,items in cats:
        box(ax,x,4.9,w,0.7,c,c,t,tc="#fff",fs=11.5,bold=True)
        box(ax,x,1.2,w,3.4,"#242422",c,pad=0.01)
        for i,it in enumerate(items):
            ax.text(x+0.15,4.1-i*0.62,it,ha="left",va="center",fontsize=9.6,color=INK)
        x+=w+0.22
    ax.text(0.35,0.4,"准备好这几样 → 送进 GPU 就能画;后面 角色/植被/道路 都是在此之上加东西",fontsize=10,color=GREEN,fontweight="bold")
    fig.tight_layout();fig.savefig("01静态网格_数据构成.png",facecolor=SURFACE,bbox_inches="tight");plt.close(fig)

# 图2: DrawCall 三解法
def fig_drawcall():
    fig,ax=plt.subplots(figsize=(15,7.5),dpi=140);fig.patch.set_facecolor(SURFACE);ax.set_facecolor(SURFACE)
    ax.set_xlim(0,15);ax.set_ylim(0,9);ax.axis("off")
    ax.set_title("核心挑战:Draw Call 太多 → CPU 瓶颈",fontsize=16,fontweight="bold",color=INK,loc="left")
    box(ax,0.4,6.9,14.2,1.3,"#331a1a",CRIT,"传统:每个物体 1 个 Draw Call → 数千物体 = 数千 DC + 状态切换 → CPU 撑不住",tc=INK,fs=12,bold=True)
    sols=[("① 静态合批",BLUE,["相同材质的静态物","离线合并进一个 buffer","→ 少 DC","代价:失去单独剔除/移动"]),
          ("② Instancing",AQUA,["同一 mesh + N 个 transform","1 个 DC 画 N 个","树/石/建筑构件","HISM = 层级+LOD"]),
          ("③ GPU-driven",GREEN,["GPU 做剔除 + 生成命令","Indirect Draw","CPU 只 kick 一下","现代主流(Nanite 同源)"])]
    w=4.5;x=0.4
    for t,c,items in sols:
        box(ax,x,4.5,w,0.8,c,c,t,tc="#fff",fs=13,bold=True)
        box(ax,x,0.7,w,3.5,"#242422",c,pad=0.01)
        for i,it in enumerate(items):
            ax.text(x+0.2,3.6-i*0.62,"• "+it,ha="left",va="center",fontsize=10,color=INK)
        x+=w+0.35
    fig.tight_layout();fig.savefig("01静态网格_DrawCall三解法.png",facecolor=SURFACE,bbox_inches="tight");plt.close(fig)

# 图3: GPU-driven 管线
def fig_gpudriven():
    fig,ax=plt.subplots(figsize=(15,7),dpi=140);fig.patch.set_facecolor(SURFACE);ax.set_facecolor(SURFACE)
    ax.set_xlim(0,15);ax.set_ylim(0,8);ax.axis("off")
    ax.set_title("GPU-driven 管线:数据统一 → 渲染通用",fontsize=16,fontweight="bold",color=INK,loc="left")
    # 输入
    box(ax,0.4,5.6,4.4,1.5,"#16283f",BLUE,"Mega-Buffer\n所有 mesh 的 顶点/索引",tc=INK,fs=11,bold=True)
    box(ax,0.4,3.6,4.4,1.5,"#152a1c",GREEN,"Instance Buffer\ntransform / 材质idx / LOD / bounds",tc=INK,fs=10.5,bold=True)
    box(ax,0.4,1.6,4.4,1.5,"#2e2611",WARN,"Bindless Textures\n所有贴图 index 访问",tc=INK,fs=11,bold=True)
    # 流程
    box(ax,5.6,4.4,3.0,1.4,"#242422",ORANGE,"GPU 剔除\nfrustum+HZB遮挡+距离",tc=INK,fs=10.5,bold=True)
    box(ax,9.0,4.4,3.0,1.4,"#242422",ORANGE,"可见实例列表\n(每实例选 LOD)",tc=INK,fs=10.5,bold=True)
    box(ax,12.2,4.4,2.5,1.4,"#242422",VIOLET,"Indirect Draw\nGPU 自己发起",tc=INK,fs=10.5,bold=True)
    box(ax,9.0,1.6,5.7,1.4,"#152a1c",GOOD,"统一着色 → GBuffer\n(所有表面物走同一路)",tc=INK,fs=11.5,bold=True)
    arrow(ax,4.8,6.3,5.6,5.4);arrow(ax,4.8,4.3,5.6,5.0);arrow(ax,4.8,2.3,9.0,4.4)
    arrow(ax,8.6,5.1,9.0,5.1);arrow(ax,12.0,5.1,12.2,5.1)
    arrow(ax,13.4,4.4,11.8,3.0)
    ax.text(0.4,0.5,"关键:mesh/实例/贴图 全进 GPU buffer 按 index 访问 → CPU 几乎不干活,渲染对所有表面物通用",fontsize=10.2,color=GREEN,fontweight="bold")
    fig.tight_layout();fig.savefig("01静态网格_GPU驱动管线.png",facecolor=SURFACE,bbox_inches="tight");plt.close(fig)

# 图4: LOD 策略
def fig_lod():
    fig,(a1,a2)=plt.subplots(1,2,figsize=(15,6),dpi=140);fig.patch.set_facecolor(SURFACE)
    for a in (a1,a2):a.set_facecolor(SURFACE);a.set_xlim(0,10);a.set_ylim(0,10);a.axis("off")
    a1.set_title("离散 LOD + dither 过渡",fontsize=13.5,fontweight="bold",color=INK,loc="left")
    cx=5
    for i,(r,col,lab) in enumerate([(1.3,BLUE,"LOD0 近·高模"),(1.0,AQUA,"LOD1 中"),(0.7,YEL,"LOD2 远·低模")]):
        y=7.6-i*2.2
        a1.add_patch(Rectangle((cx-r,y-r),2*r,2*r,fc="none",ec=col,lw=2))
        a1.text(cx+2.0,y,lab,color=col,fontsize=10.5,fontweight="bold",va="center")
    a1.text(0.3,1.3,"距离切换 → popping",color=CRIT,fontsize=10.5,fontweight="bold")
    a1.text(0.3,0.6,"解:dither(屏幕门)+ TAA 平滑",color=GREEN,fontsize=10.5,fontweight="bold")
    a2.set_title("Nanite:cluster LOD(极致)",fontsize=13.5,fontweight="bold",color=INK,loc="left")
    a2.text(0.3,8.4,"• cluster LOD DAG,GPU 逐 cluster 选",fontsize=10.8,color=INK)
    a2.text(0.3,7.4,"• 连续、无缝,无 popping",fontsize=10.8,color=INK)
    a2.text(0.3,6.4,"• 免手工做 LOD 链",fontsize=10.8,color=INK)
    a2.text(0.3,5.4,"• GPU 剔除 + 两趟遮挡 + VisBuffer",fontsize=10.8,color=INK)
    box(a2,0.3,2.6,9.3,1.9,"#16283f",BLUE,"= 静态网格 LOD 做到极致\n(你已在 Nanite 章节学过)",tc=INK,fs=12,bold=True)
    fig.tight_layout();fig.savefig("01静态网格_LOD.png",facecolor=SURFACE,bbox_inches="tight");plt.close(fig)

# 图5: PC 瓶颈迁移 + 小三角 quad overdraw + 带宽 PC vs 移动
def fig_pc_bottleneck():
    fig,(a1,a2)=plt.subplots(1,2,figsize=(15.5,6.6),dpi=140,gridspec_kw={"width_ratios":[1.55,1]})
    fig.patch.set_facecolor(SURFACE)
    for a in (a1,a2):a.set_facecolor(SURFACE);a.set_xlim(0,10);a.set_ylim(0,10);a.axis("off")
    a1.set_title("PC 瓶颈迁移:CPU(DC) → GPU(小三角)",fontsize=14,fontweight="bold",color=INK,loc="left")
    stages=[("D3D11/GL","驱动单线程\nDC 开销大\nCPU bound",CRIT),
            ("D3D12/Vulkan","多线程+PSO\nDC 开销大降",WARN),
            ("GPU-driven","indirect\nCPU≈0",AQUA),
            ("Nanite","cluster+软光栅",GREEN)]
    for i,(t,d,c) in enumerate(stages):
        x=0.2+i*2.45
        box(a1,x,7.0,2.2,1.7,"#242422",c,"",pad=0.008)
        a1.text(x+1.1,8.35,t,ha="center",fontsize=10.5,fontweight="bold",color=c if c!=WARN else "#f0c04a")
        a1.text(x+1.1,7.5,d,ha="center",fontsize=8.2,color=INK)
        if i>0:arrow(a1,x-0.25,7.85,x+0.02,7.85)
    a1.text(0.2,6.1,"瓶颈:  CPU 提交 Draw Call   ──→   GPU 顶点吞吐 / 小三角",fontsize=10.3,color=INK,fontweight="bold")
    a1.text(0.2,5.2,"小三角 quad overdraw(GPU 侧真瓶颈):",fontsize=10,color=CRIT,fontweight="bold")
    ox,oy,s=0.7,1.4,1.5
    for i in range(2):
        for j in range(2):
            a1.add_patch(Rectangle((ox+i*s,oy+j*s),s,s,fc="#16283f",ec=BASE,lw=1.3))
    a1.add_patch(Polygon([(ox+0.25,oy+0.25),(ox+0.8,oy+0.35),(ox+0.4,oy+0.8)],fc=GREEN,ec=GREEN,alpha=0.75,zorder=3))
    a1.text(ox+2*s+0.35,oy+s,"1 三角形 < 1 像素\n→ 2×2 quad 里 3 个 lane 浪费\n→ 效率崩(Nanite 软光栅救)",fontsize=9.3,color=INK,va="center")
    a2.set_title("带宽:PC vs 移动",fontsize=14,fontweight="bold",color=INK,loc="left")
    box(a2,0.3,6.5,9.4,2.2,"#16283f",BLUE,"",pad=0.01)
    a2.text(0.6,8.1,"PC(IMR)",fontsize=12,fontweight="bold",color=BLUE)
    a2.text(0.6,7.3,"带宽大(几百GB/s~1TB/s)+大 cache",fontsize=9.6,color=INK)
    a2.text(0.6,6.75,"→ 通常不是首要瓶颈",fontsize=9.6,color=INK2)
    box(a2,0.3,3.6,9.4,2.2,"#152a1c",GREEN,"",pad=0.01)
    a2.text(0.6,5.2,"移动(TBDR)",fontsize=12,fontweight="bold",color=GREEN)
    a2.text(0.6,4.4,"tile 内存 / 无大 cache / 功耗限",fontsize=9.6,color=INK)
    a2.text(0.6,3.85,"→ 带宽是王",fontsize=9.6,color=GREEN,fontweight="bold")
    a2.text(0.3,2.5,"PC 影响带宽的:高分贴图 / 大 GBuffer / 4K",fontsize=9.4,color=INK2)
    fig.tight_layout();fig.savefig("01静态网格_PC瓶颈迁移.png",facecolor=SURFACE,bbox_inches="tight");plt.close(fig)

# 图6: GPU-driven 光谱 + 走/不走
def fig_spectrum():
    fig,(a1,a2)=plt.subplots(1,2,figsize=(15.5,6),dpi=140);fig.patch.set_facecolor(SURFACE)
    for a in (a1,a2):a.set_facecolor(SURFACE);a.set_xlim(0,10);a.set_ylim(0,10);a.axis("off")
    a1.set_title("GPU-driven 是光谱,不是开关",fontsize=14,fontweight="bold",color=INK,loc="left")
    tiers=[("传统 + 现代 API","D3D12/Vulkan + instancing","大多数游戏",BLUE),
           ("部分 GPU-driven","GPU 剔除 → indirect","几何较多",AQUA),
           ("完全 GPU-driven","统一 buffer+bindless+VisBuffer","海量静态",YEL),
           ("Nanite","+ cluster LOD + 软光栅","开放世界 turnkey",GREEN)]
    y=8.6
    for t,d,use,c in tiers:
        box(a1,0.3,y-1.0,9.4,0.95,"#242422",c,"",pad=0.005)
        a1.text(0.55,y-0.52,t,fontsize=10.8,fontweight="bold",color=c if c!=YEL else "#f0c04a",va="center")
        a1.text(4.5,y-0.32,d,fontsize=8.6,color=INK,va="center")
        a1.text(4.5,y-0.72,"适用:"+use,fontsize=8.2,color=INK2,va="center")
        y-=1.12
    a1.text(0.3,3.5,"最大杠杆其实是换现代 API(D3D11→D3D12)",fontsize=10,color=GREEN,fontweight="bold")
    a1.text(0.3,2.8,"很多游戏止步于此,不必全 GPU-driven",fontsize=9.4,color=INK2)
    a2.set_title("走 / 不走 GPU-driven",fontsize=14,fontweight="bold",color=INK,loc="left")
    box(a2,0.3,5.4,9.4,3.2,"#152a1c",GOOD,"",pad=0.01)
    a2.text(0.6,8.1,"适合",fontsize=12.5,fontweight="bold",color=GOOD)
    for i,t in enumerate(["静态、不透明、统一格式","海量密集几何(开放世界)","kitbash 环境"]):
        a2.text(0.7,7.3-i*0.62,"• "+t,fontsize=9.8,color=INK)
    box(a2,0.3,0.6,9.4,4.2,"#331a1a",CRIT,"",pad=0.01)
    a2.text(0.6,4.4,"难 / 不划算",fontsize=12.5,fontweight="bold",color=CRIT)
    for i,t in enumerate(["骨骼/角色(蒙皮+per-object 动画)","透明(要排序)","高度定制材质","小场景(固定开销反亏)"]):
        a2.text(0.7,3.6-i*0.66,"• "+t,fontsize=9.8,color=INK)
    fig.tight_layout();fig.savefig("01静态网格_GPUdriven光谱.png",facecolor=SURFACE,bbox_inches="tight");plt.close(fig)

# 图7: Nanite 何时用(固定开销交叉点)
def fig_nanite_when():
    fig,(a1,a2)=plt.subplots(1,2,figsize=(15.5,6),dpi=140,gridspec_kw={"width_ratios":[1.15,1]})
    fig.patch.set_facecolor(SURFACE);a1.set_facecolor(SURFACE);a2.set_facecolor(SURFACE)
    x=np.linspace(0,10,100)
    trad=1.0+0.9*x; nanite=5.5+0.12*x
    a1.plot(x,trad,color=BLUE,lw=2.6,label="传统 (LOD+instancing)")
    a1.plot(x,nanite,color=GREEN,lw=2.6,label="Nanite")
    xc=(5.5-1.0)/(0.9-0.12); yc=1.0+0.9*xc
    a1.plot(xc,yc,marker="o",ms=11,color=CRIT,zorder=5)
    a1.axvline(xc,color=MUTED,ls=":",lw=1.2)
    a1.annotate("交叉点:几何够重才划算",(xc,yc),xytext=(xc-0.2,yc+2.2),fontsize=10,color=CRIT,fontweight="bold",
                arrowprops=dict(arrowstyle="->",color=CRIT,lw=1.4))
    a1.set_xlim(0,10);a1.set_ylim(0,11)
    a1.set_xlabel("几何密度 / 三角形量 →",fontsize=11,color=INK2)
    a1.set_ylabel("开销 →",fontsize=11,color=INK2)
    a1.set_title("Nanite 用固定开销换'几何越重越省'",fontsize=13.5,fontweight="bold",color=INK,loc="left")
    a1.legend(loc="upper left",frameon=False,fontsize=11,labelcolor=INK2)
    a1.set_xticks([]);a1.set_yticks([])
    for sp in ("top","right"):a1.spines[sp].set_visible(False)
    for sp in ("left","bottom"):a1.spines[sp].set_color(BASE)
    a1.text(0.5,-0.9,"左(几何轻):传统更便宜   右(几何重):Nanite 赢",fontsize=9.3,color=INK2)
    a2.set_xlim(0,10);a2.set_ylim(0,10);a2.axis("off")
    a2.set_title("何时用 / 不用",fontsize=13.5,fontweight="bold",color=INK,loc="left")
    box(a2,0.3,6.4,9.4,2.3,"#152a1c",GOOD,"",pad=0.01)
    a2.text(0.6,8.2,"值",fontsize=12,fontweight="bold",color=GOOD)
    a2.text(0.6,7.4,"超高模 / 海量密集 / 小三角 overdraw",fontsize=9.4,color=INK)
    a2.text(0.6,6.85,"想免手工 LOD / PC 高端",fontsize=9.4,color=INK)
    box(a2,0.3,3.4,9.4,2.6,"#331a1a",CRIT,"",pad=0.01)
    a2.text(0.6,5.6,"不值 / 短板",fontsize=12,fontweight="bold",color=CRIT)
    a2.text(0.6,4.85,"几何轻(固定开销亏)/ 简单低模",fontsize=9.4,color=INK)
    a2.text(0.6,4.3,"骨骼 / 透明 / Masked·WPO / 移动端",fontsize=9.4,color=INK)
    box(a2,0.3,0.5,9.4,2.3,"#2e2611",WARN,"",pad=0.01)
    a2.text(0.6,2.3,"你的项目(移动 FPS)",fontsize=11,fontweight="bold",color="#f0c04a")
    a2.text(0.6,1.5,"大概率不用 Nanite → 传统 LOD+instancing",fontsize=9.5,color=INK)
    a2.text(0.6,0.95,"地形走 CDLOD+RVT(非 Nanite 地形)",fontsize=9.5,color=INK)
    fig.tight_layout();fig.savefig("01静态网格_Nanite何时用.png",facecolor=SURFACE,bbox_inches="tight");plt.close(fig)

fig_data();fig_drawcall();fig_gpudriven();fig_lod()
fig_pc_bottleneck();fig_spectrum();fig_nanite_when()
print("done: 7 figures (01)")
