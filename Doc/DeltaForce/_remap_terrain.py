# -*- coding: utf-8 -*-
"""读旧 高质量地形/make_figs.py,转深色,写出 make_figs_terrain.py 到本文件夹。"""
import re, io, os
OLD = r"C:/Users/luchunyi/Desktop/书架/vulkan/graphics_books/读书笔记/高质量地形/make_figs.py"
NEW = r"C:/Users/luchunyi/Desktop/书架/vulkan/graphics_books/读书笔记/场景物体渲染数据准备/make_figs_terrain.py"

DARK = {
    "#fcfcfb":"#1a1a19","#0b0b0b":"#f0efe9","#52514e":"#c3c2b7",
    "#e1e0d9":"#2c2c2a","#c3c2b7":"#383835",
    "#2a78d6":"#3987e5","#1baf7a":"#199e70","#eda100":"#c98500",
    "#4a3aa7":"#9085e9","#e34948":"#e05a5a","#eb6834":"#d95926",
    "#008300":"#33a833","#d03b3b":"#e0574f",
    "#eef4fc":"#16283f","#eef7f0":"#152a1c","#fffbe8":"#2e2611","#fffceb":"#2e2611",
    "#fdeeec":"#331a1a","#efeaf7":"#231d34","#f4f1ea":"#26251f","#f9f9f7":"#0d0d0d",
    "#cfe6d6":"#1c3a26","#cfe0f7":"#1c3350","#cde2fb":"#16324f","#6da7ec":"#3a6ea5",
    "#0d366b":"#9ec5f4","#b37d00":"#f0c04a",
}
s = io.open(OLD, encoding="utf-8").read()
# 保护"白色文字",其余白色是填充/描边 → 深色 panel
s = s.replace('tc="#ffffff"','tc="\x00W\x00"').replace('color="#ffffff"','color="\x00W\x00"')
s = s.replace('#ffffff','#242422')
s = s.replace('\x00W\x00','#ffffff')
# 单遍字典替换(防碰撞)
keys = sorted(DARK, key=len, reverse=True)
pat = re.compile("|".join(re.escape(k) for k in keys))
s = pat.sub(lambda m: DARK[m.group()], s)
io.open(NEW, "w", encoding="utf-8").write(s)
print("wrote", NEW)
