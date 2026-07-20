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
