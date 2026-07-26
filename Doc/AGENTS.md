# AGENTS.md - Book Repository Documentation Rules

本仓库用于沉淀源码学习文档，尤其是 Unreal Engine 渲染源码解析文档。后续 Agent 生成或维护文档时，必须遵循本文件规则。

## 默认输出目录

所有源码学习文档默认写入：

```text
Doc/
```

不要把生成的学习文档散落到仓库根目录。根目录只保留仓库说明、协作规范和必要配置。

## HTML 文档标准模板

后续所有代码解析 HTML 文档都以以下文件作为标准模板风格：

```text
Doc/LumenDiffuseIndirectComposite_ConvergenceAnnotated.html
```

必须保持同类结构和视觉风格，后续所有 HTML 最终会汇聚成一本源码学习书。

## 必须产出主文档和函数附录

每个源码主题必须成组产出至少两个 HTML：

```text
<主题名>_ConvergenceAnnotated.html
<主题名>_FunctionAppendix.html
```

主文档负责粗粒度流程，函数附录负责具体函数算法。

## 主文档结构

主文档必须按以下顺序组织：

1. 标题与主题说明。
2. 颜色图例。
3. 汇聚点 / 发散点骨架。
4. 函数算法附录跳转入口。
5. 完整源码对照。
6. 阶段级源码片段和说明。
7. 汇聚 / 发散总表。
8. 最终总结。

主文档重点回答：这个文件的主流程是什么、哪些地方是稳定汇聚点、哪些地方是分叉发散点、每个阶段存在的意义是什么。

## 函数附录结构

函数附录必须包含：

1. 返回主文档的跳转链接。
2. 函数级汇聚 / 发散骨架。
3. 函数目录锚点。
4. 完整源码对照。
5. 每个关键函数的源码和算法说明。
6. 函数调用关系表。

每个函数说明建议包含：

- 作用
- 输入
- 输出
- 算法步骤
- 共同点
- 不同点
- 关键实现细节

## 双向跳转规则

主文档必须链接到函数附录：

```html
<a href="<主题名>_FunctionAppendix.html">打开函数算法附录</a>
```

函数附录必须链接回主文档：

```html
<a href="<主题名>_ConvergenceAnnotated.html">返回主文档</a>
```

链接使用相对路径，不使用绝对路径。

## 源码对照规则

完整源码对照区域必须直接把源码文本内嵌进暗色代码框中，不能依赖浏览器读取本地文件。

推荐形式：

```html
<textarea class="embedded-source" readonly spellcheck="false">...源码...</textarea>
```

原因：浏览器通常会限制本地文件嵌入，`object`、`iframe`、`file://` 等方式不稳定。

## 路径规则

文档中只写 UE 工程内相对路径，例如：

```text
Engine/Shaders/Private/DiffuseIndirectComposite.usf
```

不要写绝对路径，例如：

```text
D:\LCY\UnrealEngine\Engine\Shaders\Private\DiffuseIndirectComposite.usf
```

## 说明区视觉规范

说明卡片必须使用深色背景、中文友好字体、分层配色和关键词标签。当前推荐风格包括：

- 深色渐变背景。
- 中文字体栈：`Microsoft YaHei UI`, `Microsoft YaHei`, `Segoe UI`, `Arial`。
- 标题使用暖黄色。
- 正文分层使用冷蓝、暖橙、淡紫、绿色等颜色。
- `共同点` / `不同点` 使用 pill 标签高亮。
- 内联代码加轻微底色和描边。

说明文字保持技术说明口吻，不使用“老师批注”等拟人口吻。

## 分叉阶段说明规则

只要某阶段存在分叉，说明中必须写：

```text
共同点：...
不同点：...
```

目的是先理解稳定骨架，再展开具体分支。

## 内容组织原则

优先讲清楚：

- 输入是什么
- 输出是什么
- 汇聚点在哪里
- 发散点在哪里
- 每个阶段为什么存在
- 对上游是否透明
- 对下游输出契约是否固定

不要只逐行翻译源码；要解释代码结构和设计意图。

## 当前已完成示例

可参考以下文档：

```text
Doc/LumenDiffuseIndirectComposite_ConvergenceAnnotated.html
Doc/LumenDiffuseIndirectComposite_FunctionAppendix.html
```

注意：章节文件现统一放在 `Doc/Chapters/` 子目录（例如 `Doc/Chapters/LumenDiffuseIndirectComposite_ConvergenceAnnotated.html`），上方旧路径仅留作历史示例。

---

# 实战经验（Lumen 文档系列，2026-07 补充）

> 以下是实际写完 40+ 篇 Lumen 源码解析文档后沉淀的经验，供写**其他功能文档**时参考。上面是「规范」，这里是「怎么把规范落地不踩坑」。

## 目录与总目录结构

实际文件布局：

```text
Doc/
  UE_GI_Lumen_解析_总目录.html      # 全书总目录（唯一入口）
  Chapters/
    <主题名>_ConvergenceAnnotated.html
    <主题名>_FunctionAppendix.html
```

- **章节一律放 `Doc/Chapters/`**，不要直接丢在 `Doc/` 下。
- **每个新主题必须挂进总目录**，否则等于没写。总目录按「数据流分层」组织（底层→中层→顶层→横切→补充参考），新主题要放进语义最贴切的分层区块，而非随便追加到末尾。
- 章节页返回总目录用相对路径 `../UE_GI_Lumen_解析_总目录.html`（注意 `../`，因为在子目录里）。

## 模板复用：直接抄一篇同类文档，别从零手写

- 新建文档时，**先 Read 一篇最近的同类主文档/附录**，连 `<style>` 整块一起复用。CSS 配色变量固定：`--blue/--green/--yellow/--orange/--pink/--purple/--red/--cyan`。
- 主文档骨架 class：`.flow/.node/.gate/.branch/.func/.out/.merge`；附录精简 class：`.mono/.toc/.table`。
- 源码内嵌代码框实际用的类名是 `codebox`（大段用 `codebox large`）——沿用现存文档的即可。
- 文件末尾统一挂一段 `<script>`，把行首「共同点：/不同点：」转成 pill 标签。整段一起抄。

## 内容准确性：对着源码核验，绝不凭记忆写 API

这是**最容易翻车**的地方。写之前务必用 Grep/Read 在工作区真实源码里确认：

- **CVar 名逐字核对**。踩过的坑：`r.Lumen.MaxTraceDistance` 根本不存在，真实是 `r.Lumen.TraceMeshSDFs.TraceDistance`（近场）和 `r.LumenScene.FarField.MaxTraceDistance`（远场）。
- **区分 RDG 资源名 vs GPU stat/event scope 名**：RDG 纹理名是点分式 `TEXT("Lumen.ScreenProbeGather.X")`（真实），但 ProfileGPU 里的 scope 名是 camelCase `LumenScreenProbeGather`（真实）。两者不能混。
- **枚举/默认值核对**：如 `r.DynamicGlobalIlluminationMethod`、`r.ReflectionMethod` 的 `1=Lumen`。
- 类名、shader 入口名、结构体字段、公式，全部从源码提取，并在文档里标注「所有符号/CVar 均取自 UE5.8 本地源码」。

## 抓「设计角色」而非逐行翻译

好文档的判断标准是能不能一句话点破某模块的**角色定位**。例子：

- 毛发遮蔽 = 「只减光、不加光的遮蔽体」（`Lighting *= Transparency`），与所有其它「命中后取光照」的追踪方向相反。
- 反射去噪 = 三阶段流水线，串起时空两步的关键信号是「二阶矩→方差」。
- 每个分叉阶段都要写「共同点 / 不同点」，先讲稳定骨架再展开分支。

## 覆盖度自查（判断「写全了没」）

用脚本盘源码总量再对文档，别靠感觉：

- 三个维度都要盘：`.cpp` / `.usf` / `.ush`。**`.ush` 公共库最容易被忽略**——毛发遮蔽就是藏在 `LumenHairTracing.ush` 里差点漏掉的完整功能。
- 盘完列一张「已覆盖 / 缺口 / 不值得单独成篇」清单。几十行的编码工具类 ush、纯 debug/visualize shader 通常不单独成篇，塞进已有附录一句话即可。

## 交叉链接：新主题要双向挂

- 新主题主文档/附录之间双向链（模板已有）。
- **还要给相关老章节加反向链接**：找出新主题的上下游/消费者/对照篇，在它们的「关联文档」或「函数算法附录」区块追加一个 `.tag` 链接。这样全书是网状而非孤岛。

## 静态验证（提交前必做）

用 PowerShell 脚本批量查，别手工点：

1. **死链**：提取所有 href 的 .html，`Test-Path` 逐个验（注意 `../` 前缀）；总目录里的 `Chapters/xxx.html` 也要验。
2. **标签平衡**：数 `<div` 与 `</div>`、`<section` 与 `</section>`。

- **标签平衡有个坑**：本模板的主文档正常就是 `<div` 比 `</div>` **多 1**（固有写法）。所以差 1 正常，差 2 才是真有未闭合——要和同目录一篇已知正确的文档对比基准。典型错误：漏了一个 `.body` 的 `</div>` 导致差 2。
- 别的小坑：`</b >`（多空格）这类笔误 grep 一下。

## 提交规范

- Commit 格式：`docs(lumen): <描述> [AI-assisted]`，body 用多个 `-m` 列要点。
- **推送前必须询问用户**，这是本项目铁律。本地 commit 可以直接做，`git push` 一定先问（除非用户对当前整块任务已明确授权「做完不用问」）。
- PowerShell 环境：命令分隔用 `;`，不能用 `&&`。

---

# 实战经验（全书 Shader 整源升级，2026-07 补充）

> 把 6 本书（Lumen / VirtualTexture / MegaLights / TSR / Shadow / Nanite）的 shader 文档统一拉齐到「**每个 shader 一篇独立文档 + 内嵌完整真实源码 + 真实多行代码批注**」标准后沉淀的经验。核心目标：**覆盖率 = 引擎该模块 shader 目录下每个 .usf/.ush 都有独立文档**，零伪代码、零节选。

## 覆盖率的硬定义与自查

- 覆盖率不是「主要 shader 都讲了」，而是**逐文件**：引擎目录（含 `Voxel/` 等子目录）里每个 `.usf`/`.ush` 都要有对应 `<名>.usf.html`/`<名>.ush.html`。
- 收尾自查三件套（PowerShell，全部要 `-Encoding UTF8`）：
  1. 数量对齐：文档侧 `.usf`/`.ush` 计数 == 引擎侧（含子目录）计数。
  2. **逐文件名交叉校验**：遍历引擎每个 shader，`Test-Path` 对应 html，列出缺失（数量相等也可能张冠李戴，必须按名核）。
  3. 残留标记归零：所有 `.usf.html`/`.ush.html` 里 `@@SRC@@` 计数 == 0。

## 整源注入流水线（关键技术，CLM 兼容）

先 Write 建「壳」（正文批注 + 一个 `@@SRC@@` ASCII 占位），再用脚本把真实源码注入占位，好处是 Write 时不必转义大段源码、批注和源码解耦：

```powershell
$usf = (Get-Content $src -Raw -Encoding UTF8) -replace '&','&amp;' -replace '<','&lt;' -replace '>','&gt;'
$p   = (Get-Content $doc -Raw -Encoding UTF8) -split '@@SRC@@'
if($p.Count -eq 2){ Set-Content -Path $doc -Value ($p[0]+$usf+$p[1]) -Encoding UTF8 -NoNewline }
```

- **转义顺序必须 `&`→`<`→`>`**（先转 & 否则把 `&lt;` 里的 & 二次转义）。
- 占位用纯 ASCII 标记 `@@SRC@@`，`-split` 后 `Count==2` 才写，避免误伤。
- 读写**都要 `-Encoding UTF8`**，否则中文批注乱码；`-NoNewline` 防尾部多空行。
- 多个源码目录（如 `Nanite/` 与 `Nanite/Voxel/`）用 hashtable 映射每个文件的基路径，一轮循环注入。
- 源码放进 `<textarea class="embedded-source" readonly spellcheck="false">@@SRC@@</textarea>`。

## PowerShell ConstrainedLanguage(CLM) 红线

本机是 CLM 模式，**禁止 .NET 方法调用**。踩过：`$_.Line.Substring(...)`、`[Math]::Min(...)` 直接抛 `MethodInvocationNotSupportedInConstrainedLanguage`。

- 需要看行/截断时改用 `Select-String` + `Select-Object -First N` 或直接 `Read` 文件，别用字符串方法。
- `git push` 的进度输出会走 stderr 被 PowerShell 当报错、exitCode=1，但**看到 `<old>..<new>  master -> master` 就是成功**；再 `git status -sb` / `git log origin/master..HEAD` 确认 ahead 0。

## 整源版单篇文档结构

- header 双回链：`../<总目录>.html` + 对应章节 `../Chapters/<章>.html`。
- 大主 shader（`.usf`）：一眼看懂 flow（node + arrow）→ 完整源码卡（`source-path` 标注关键符号@行号 + `embedded-source`）→ 若干 `grid2` 批注卡（左 `<pre><code>` 真实多行 `code-line`，用 blue/green/yellow/orange 着色；右 `.note` 说明）→ 关联文档卡。
- 小型 `.usf` / 库 `.ush`：1 个整源卡 + 1~2 个真实代码批注卡 + 关联卡；`.ush` 标题加 `libtag` 紫色标签。
- **整源版 CSS 差异**：`code-line{white-space:pre-wrap}`（节选版是 `pre`）；去掉旧节选版的 note-label/n-blue `<script>`。`embedded-source` 高度按源码行数调（小库 380~400px，大文件 600px）。
- `source-path` 里的符号@行号先用 `Select-String` 读引擎真实签名，别凭记忆。

## 重建既有节选版

老书里若已有「节选/伪代码」版旧文档，直接用 Write **整体覆盖**成整源模板（含 `@@SRC@@`）再注入，不要在旧结构上打补丁。

## 总目录的 Shader 全覆盖索引

- 每本书总目录末尾挂一张**分组**全覆盖索引卡（按剔除/编码流送/着色/光追/体素/核心库/其余库等语义分组），用 `.tag` pill 链接列全部 shader，标注「已 100% 覆盖 N 篇」。
- 收尾脚本再验一遍：把总目录里所有 `Shaders/xxx.html` 提出来 `Test-Path`，失效数必须为 0。

## 批次节奏

- 按语义分批（每批 6~13 篇）：建壳 → 批量注入 → 残留归零校验 → `git commit`（`docs(<book>): 批N ... [AI-assisted]`）。
- 一本书全部批次 + 总目录索引都 commit 完，再统一 `git push`（推送前守铁律，除非已获整块授权）。
