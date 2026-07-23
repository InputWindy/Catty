# -*- coding: utf-8 -*-
"""传统管线优化(现代 API):LOD / HLOD / 剔除。深色主题。"""
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
from matplotlib.patches import FancyBboxPatch, FancyArrowPatch, Rectangle, Polygon

plt.rcParams["font.family"]="Microsoft YaHei"; plt.rcParams["axes.unicode_minus"]=False
# ---- 深色调色板(dataviz dark)----
SURFACE="#1a1a19"; INK="#f0efe9"; INK2="#c3c2b7"; MUTED="#898781"; GRID="#2c2c2a"; BASE="#383835"; PANEL="#242422"
BLUE="#3987e5"; AQUA="#199e70"; YEL="#c98500"; GREEN="#33a833"; VIOLET="#9085e9"; RED="#e05a5a"; ORANGE="#d95926"
GOOD="#0ca30c"; WARN="#fab219"; CRIT="#e0574f"
TB="#16283f"; TG="#152a1c"; TY="#2e2611"; TR="#331a1a"; TV="#231d34"; TN="#26251f"

def box(ax,x,y,w,h,fc,ec,txt="",tc=INK,fs=10,bold=False,pad=0.02):
    ax.add_patch(FancyBboxPatch((x,y),w,h,boxstyle=f"round,pad={pad},rounding_size=0.03",fc=fc,ec=ec,lw=1.7,zorder=2))
    if txt: ax.text(x+w/2,y+h/2,txt,ha="center",va="center",color=tc,fontsize=fs,fontweight="bold" if bold else "normal",zorder=3)
def arrow(ax,x1,y1,x2,y2,color=MUTED,lw=2):
    ax.add_patch(FancyArrowPatch((x1,y1),(x2,y2),arrowstyle="-|>",mutation_scale=16,color=color,lw=lw,zorder=1))

# 图1: 减几何三件套
def fig_trinity():
    fig,ax=plt.subplots(figsize=(15,6),dpi=140);fig.patch.set_facecolor(SURFACE);ax.set_facecolor(SURFACE)
    ax.set_xlim(0,15);ax.set_ylim(0,7);ax.axis("off")
    ax.set_title("减几何三件套:各解决什么",fontsize=16,fontweight="bold",color=INK,loc="left")
    cols=[("剔除 Culling",BLUE,TB,"看不见的不画","不可见 → 零成本"),
          ("LOD",AQUA,TG,"远物体每个用更少三角形","减 三角形/顶点"),
          ("HLOD",YEL,TY,"远处一堆物体合成更少物体","减 Draw Call/物体数")]
    w=4.6;x=0.5
    for t,c,tint,a,b in cols:
        box(ax,x,4.9,w,0.85,c,c,t,tc="#ffffff",fs=13,bold=True)
        box(ax,x,1.4,w,3.2,tint,c,pad=0.01)
        ax.text(x+w/2,3.4,a,ha="center",fontsize=11,color=INK,fontweight="bold")
        ax.text(x+w/2,2.4,b,ha="center",fontsize=11,color=c)
        x+=w+0.35
    ax.text(0.5,0.5,"LOD 减「每物体三角形」,HLOD 减「物体个数」——正交互补;剔除按 视锥→距离→遮挡 逐层筛",
            fontsize=10.5,color=GREEN,fontweight="bold")
    fig.tight_layout();fig.savefig("opt_减几何三件套.png",facecolor=SURFACE,bbox_inches="tight");plt.close(fig)

# 图2: LOD vs HLOD
def fig_lod_hlod():
    fig,(a1,a2)=plt.subplots(1,2,figsize=(15,6),dpi=140);fig.patch.set_facecolor(SURFACE)
    for a in (a1,a2):a.set_facecolor(SURFACE);a.set_xlim(0,10);a.set_ylim(0,10);a.axis("off")
    a1.set_title("Mesh LOD:减「每物体三角形」",fontsize=13.5,fontweight="bold",color=INK,loc="left")
    for i,(r,c,lab,tri) in enumerate([(1.4,BLUE,"LOD0 近·高模","(密三角)"),(1.0,AQUA,"LOD1 中",""),(0.65,YEL,"LOD2 远·低模","(疏三角)")]):
        y=7.8-i*2.3
        a1.add_patch(Rectangle((1.2,y-r),2*r,2*r,fc=PANEL,ec=c,lw=2))
        # grid density hint
        n=[5,3,2][i]
        for k in range(1,n):
            a1.plot([1.2+k*2*r/n]*2,[y-r,y+r],color=c,lw=0.5,alpha=0.5)
            a1.plot([1.2,1.2+2*r],[y-r+k*2*r/n]*2,color=c,lw=0.5,alpha=0.5)
        a1.text(4.2,y,f"{lab} {tri}",color=c,fontsize=10.5,fontweight="bold",va="center")
    a1.text(0.3,0.8,"仍是 1 物体 = 1 Draw Call;按屏幕占比切;dither+TAA 消 popping",fontsize=9.6,color=INK2)
    a2.set_title("HLOD:减「物体个数」",fontsize=13.5,fontweight="bold",color=INK,loc="left")
    # many objects -> 1 proxy
    import math
    for k in range(9):
        gx=0.8+(k%3)*0.9; gy=6.6+(k//3)*0.9
        a2.add_patch(Rectangle((gx,gy),0.6,0.6,fc=TB,ec=BLUE,lw=1.2))
    a2.text(1.0,9.3,"远处一簇物体(N 个,N 个 DC)",color=INK,fontsize=10)
    arrow(a2,3.9,7.4,5.6,7.4,color=GREEN,lw=2.4)
    a2.text(4.0,7.9,"HLOD",color=GREEN,fontsize=10,fontweight="bold")
    a2.add_patch(Rectangle((6.0,6.4),3.0,2.0,fc=TY,ec=YEL,lw=2))
    a2.text(7.5,7.4,"1 个 proxy\n(1 mesh·1 材质·1 DC)",ha="center",color=INK,fontsize=10,fontweight="bold")
    box(a2,0.4,1.6,9.2,3.4,PANEL,YEL,pad=0.01)
    a2.text(0.7,4.4,"• 合并 N 个物体为 1 个简化 proxy",fontsize=10.2,color=INK)
    a2.text(0.7,3.7,"• 层级(HLOD tree):越远合并越大",fontsize=10.2,color=INK)
    a2.text(0.7,3.0,"• 代价:离线烘焙 / 内存 / 不能单独动",fontsize=10.2,color=INK)
    a2.text(0.7,2.3,"• 开 Nanite 常不需要;不用 Nanite 时它是远景减 DC 主力",fontsize=10.2,color=INK)
    a2.text(0.4,0.7,"LOD × HLOD 叠加:近处真物体走 LOD,远景整片走 HLOD proxy",fontsize=9.6,color=GREEN,fontweight="bold")
    fig.tight_layout();fig.savefig("opt_LOD_vs_HLOD.png",facecolor=SURFACE,bbox_inches="tight");plt.close(fig)

# 图3: HLOD 材质烘焙(关键)
def fig_hlod_bake():
    fig,(a1,a2)=plt.subplots(1,2,figsize=(16,6.8),dpi=140);fig.patch.set_facecolor(SURFACE)
    for a in (a1,a2):a.set_facecolor(SURFACE);a.set_xlim(0,10);a.set_ylim(0,10);a.axis("off")
    a1.set_title("烘的是「最终输出」,不是拷源纹理",fontsize=13.5,fontweight="bold",color=INK,loc="left")
    box(a1,0.3,7.4,9.4,1.7,PANEL,BLUE,pad=0.01)
    a1.text(0.6,8.7,"源材质(各不同)",fontsize=10.5,fontweight="bold",color=BLUE)
    a1.text(0.6,8.0,"albedo = lerp(砖Tex, 苔Tex, mask)×tint + fresnel×rim",fontsize=9.4,color=INK)
    arrow(a1,5,7.4,5,6.5,color=GREEN,lw=2.2)
    box(a1,0.3,5.0,9.4,1.4,TG,GREEN,pad=0.01)
    a1.text(5,5.7,"烘焙:每 texel 评估原 shader 跑一遍 → 采成品值",ha="center",fontsize=10.5,fontweight="bold",color=GREEN)
    arrow(a1,5,5.0,5,4.1,color=GREEN,lw=2.2)
    box(a1,0.3,2.6,9.4,1.5,PANEL,YEL,pad=0.01)
    a1.text(5,3.35,"图集:成品 albedo / normal / ORM(源纹理已消费丢弃)",ha="center",fontsize=10.5,fontweight="bold",color="#e8b74a")
    arrow(a1,5,2.6,5,1.7,color=MUTED,lw=2.2)
    box(a1,0.3,0.5,9.4,1.2,PANEL,VIOLET,"proxy:一个 fallback PBR 材质,采成品图",tc=INK,fs=10.5,bold=True,pad=0.01)

    a2.set_title("烘「属性」,不烘「光照」→ 高光保留",fontsize=13.5,fontweight="bold",color=INK,loc="left")
    box(a2,0.3,6.9,9.4,2.1,TG,GREEN,pad=0.01)
    a2.text(0.6,8.5,"烘进图集(视角无关属性)",fontsize=11,fontweight="bold",color=GREEN)
    a2.text(0.6,7.7,"albedo · normal · roughness · metallic · AO · emissive",fontsize=9.6,color=INK)
    a2.text(0.6,7.15,"(额外计算跑一次、结果冻进图集)",fontsize=8.8,color=INK2)
    box(a2,0.3,4.0,9.4,2.5,TB,BLUE,pad=0.01)
    a2.text(0.6,6.0,"运行时算(动态)",fontsize=11,fontweight="bold",color=BLUE)
    a2.text(0.6,5.3,"diffuse + 高光(BRDF 用烘好的 normal/rough/metallic)",fontsize=9.6,color=INK)
    a2.text(0.6,4.75,"阴影 / GI / 雾",fontsize=9.6,color=INK)
    a2.text(0.6,4.2,"→ 高光每帧重算,随相机动,没被扔",fontsize=9.6,color=GREEN,fontweight="bold")
    box(a2,0.3,1.1,9.4,2.5,TR,CRIT,pad=0.01)
    a2.text(0.6,3.1,"真正丢失(材质图里的视角花活)",fontsize=11,fontweight="bold",color=CRIT)
    a2.text(0.6,2.4,"自定义 fresnel 调色 / WPO 动画 / masked 动画 / 视差",fontsize=9.6,color=INK)
    a2.text(0.6,1.5,"→ 冻成快照,失去动态",fontsize=9.6,color=CRIT)
    a2.text(0.3,0.4,"判据不是「视角相关」,而是「光照 pass 重算 vs 材质图烘死」——烘 surface,不烘 shine",
            fontsize=9.2,color=GREEN,fontweight="bold")
    fig.suptitle("HLOD 材质烘焙:烘最终输出的属性 → 高光运行时重算(保留),只丢材质图的视角花活",
                 fontsize=14.5,fontweight="bold",color=INK,y=0.995)
    fig.tight_layout(rect=[0,0,1,0.94]);fig.savefig("opt_HLOD材质烘焙.png",facecolor=SURFACE,bbox_inches="tight");plt.close(fig)

# 图4: 剔除
def fig_cull():
    fig,(a1,a2)=plt.subplots(1,2,figsize=(15.5,6),dpi=140);fig.patch.set_facecolor(SURFACE)
    for a in (a1,a2):a.set_facecolor(SURFACE);a.set_xlim(0,10);a.set_ylim(0,10);a.axis("off")
    a1.set_title("剔除流水:逐层筛",fontsize=13.5,fontweight="bold",color=INK,loc="left")
    steps=[("所有物体",BASE),("视锥剔除(视野外)",BLUE),("距离/小物体剔除",AQUA),
           ("遮挡剔除(被挡住)",YEL),("可见 → LOD/HLOD → 提交",GREEN)]
    y=8.6
    for i,(t,c) in enumerate(steps):
        box(a1,1.2,y-0.75,7.6,0.85,PANEL,c,t,tc=INK,fs=11,bold=True)
        if i<len(steps)-1: arrow(a1,5,y-0.75,5,y-1.15)
        y-=1.7
    a2.set_title("遮挡剔除方法 + 现代化",fontsize=13.5,fontweight="bold",color=INK,loc="left")
    rows=[("HZB 层级Z","盒投影测深度金字塔(上帧→偶尔错,两趟修)"),
          ("Occlusion Query","GPU 查询,有延迟 1+帧 → pop-in"),
          ("软件遮挡","CPU 光栅遮挡体(Umbra/Intel)"),
          ("PVS 预计算","离线可见集,只适静态")]
    y=8.5
    for t,d in rows:
        box(a2,0.3,y-0.7,3.0,0.65,TY,YEL,t,tc=INK,fs=9.6,bold=True)
        a2.text(3.5,y-0.38,d,fontsize=9.2,color=INK,va="center")
        y-=1.05
    box(a2,0.3,1.6,9.4,2.2,TB,BLUE,pad=0.01)
    a2.text(0.6,3.3,"现代 API 调整",fontsize=11,fontweight="bold",color=BLUE)
    a2.text(0.6,2.6,"剔除搬 GPU compute(frustum+HZB)+ indirect draw",fontsize=9.6,color=INK)
    a2.text(0.6,2.05,"= 部分 GPU-driven,不必上全 Nanite",fontsize=9.6,color=INK)
    a2.text(0.3,0.8,"剔除是 per-view(客户端),不影响玩法状态(与碰撞/确定性不同)",fontsize=9.2,color=GREEN,fontweight="bold")
    fig.tight_layout();fig.savefig("opt_剔除.png",facecolor=SURFACE,bbox_inches="tight");plt.close(fig)

# 图5: 现代 API 下的传统优化总表
def fig_modern_table():
    fig,ax=plt.subplots(figsize=(15,8),dpi=140);fig.patch.set_facecolor(SURFACE);ax.set_facecolor(SURFACE)
    ax.set_xlim(0,15);ax.set_ylim(0,9.2);ax.axis("off")
    ax.set_title("现代 API 下的传统管线优化(老策略仍有效)",fontsize=15.5,fontweight="bold",color=INK,loc="left")
    rows=[("CPU 提交侧",BLUE,"逐 draw 设状态·单线程·每 draw 绑资源","PSO 预编译 · 多线程录制 · bindless · MultiDrawIndirect · async compute"),
          ("批处理",AQUA,"静态/动态合批 · Instancing(ISM/HISM)","MDI;per-draw 便宜了但合批仍减 setup"),
          ("剔除",YEL,"视锥 · 遮挡(HZB/query/软件) · 距离/小物体","可搬 GPU compute + indirect"),
          ("LOD",GREEN,"Mesh LOD+dither · HLOD · Impostor · 材质LOD","逻辑不变"),
          ("Overdraw",ORANGE,"Depth prepass · Early-Z · 不透明 front-to-back","逻辑不变"),
          ("内存/带宽",VIOLET,"纹理流送+mip · 压缩(BC/ASTC) · 顶点量化 · 精简GBuffer","async 重叠"),
          ("分辨率",RED,"—","动态分辨率 · 时域上采样(TAA/TSR/DLSS/FSR)【现代新增】")]
    ax.text(0.4,8.3,"类别",fontsize=10,fontweight="bold",color=MUTED)
    ax.text(3.0,8.3,"老做法",fontsize=10,fontweight="bold",color=MUTED)
    ax.text(9.2,8.3,"现代 API 调整",fontsize=10,fontweight="bold",color=MUTED)
    y=7.7
    for t,c,old,new in rows:
        box(ax,0.4,y-0.5,2.3,0.7,c,c,t,tc="#ffffff",fs=10.5,bold=True)
        ax.text(3.0,y-0.15,old,fontsize=8.9,color=INK,va="center")
        ax.text(9.2,y-0.15,new,fontsize=8.9,color=INK2,va="center")
        ax.plot([0.4,14.7],[y-0.72,y-0.72],color=GRID,lw=0.8)
        y-=1.02
    ax.text(0.4,0.4,"一句话:老策略全有效;现代 API 升级 CPU 提交侧(PSO+多线程+bindless+MDI+async),新增时域上采样。移动 FPS 首选这套,不必 Nanite。",
            fontsize=9.6,color=GREEN,fontweight="bold")
    fig.tight_layout();fig.savefig("opt_现代API传统优化总表.png",facecolor=SURFACE,bbox_inches="tight");plt.close(fig)

fig_trinity();fig_lod_hlod();fig_hlod_bake();fig_cull();fig_modern_table()
print("done: 5 figures (opt, dark)")
