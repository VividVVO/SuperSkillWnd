# 游戏原生窗口层级与遮挡知识库

更新时间：2026-04-11

适用工程：`G:\code\c++\SuperSkillWnd`

本文件专门记录 MapleStory 原生窗口创建、层级、鼠标命中、遮挡计算、D3D8/D3D9 自绘接入可行性，以及加载/黑屏/分辨率切换时 overlay 隐藏策略。使用时应同时读取：

- `.claude/rules/CLAUDE.md`
- `.claude/rules/d3d8_compat_knowledge.md`
- `.claude/rules/skillwnd_second_child_route_knowledge.md`
- 本文件

## 1. 置信度标准

| 等级 | 含义 |
| --- | --- |
| A | 直接来自 IDA 伪代码/汇编、源码、当前运行日志，地址/偏移/调用关系明确。 |
| B | 多个证据一致，但具体命名、方向或语义仍需运行期采样确认。 |
| C | 目前只是合理推断或历史实验结论，必须避免当成事实继续开发。 |

## 2. 当前运行态快照

| 项 | 数据 | 置信度 |
| --- | --- | --- |
| 游戏进程 | `MapleStory.exe` 已运行，PID `62300`，启动时间 `2026/4/11 6:55:58`，窗口标题为角色 `1111111222` | A |
| 游戏窗口句柄 | `0x0034250C`，日志首帧 D3D8 Present 也捕获同一 HWND | A |
| 当前 DLL marker | `v20.1-2026-04-11-d3d8-fix-blackscreen` | A |
| 当前图形模式 | D3D8，`d3d8.dll` 已加载，D3D8 Present/Reset hook 安装成功 | A |
| 日志现象 | 有 D3D8 Reset，纹理 release/reload；SkillWndEx 捕获为 `0x284D3BB4`；SuperBtn 原生对象创建为 `0x360E4F34` | A |
| 外部读内存 | `OpenProcess(PROCESS_VM_READ)` 对 MapleStory 返回 `ERROR_ACCESS_DENIED(5)`，只能 limited query | A |
| 运行期全窗口 dump | 当前 DLL 尚无完整 CWndMan 采样器，必须在进程内记录 | A |

结论：不能依赖独立 PowerShell/外部工具直接扫游戏内存；要获取所有窗口矩形、z-order、interactive list，必须把采样逻辑放进 hook.dll 里运行。

## 3. 重要纠偏：`CWndMan+0x4A74` 是 vector 指针，不是内联数组

旧代码/旧注释曾把 `CWNDMAN_TOPLEVEL_OFF = 19060 / 0x4A74` 当成“toplevel 窗口数组起点”。IDA 证据显示这是错误的。

真实结构：

```cpp
uintptr_t wndMan = *(uintptr_t*)ADDR_CWndMan;
uintptr_t vecData = *(uintptr_t*)(wndMan + 0x4A74);
int count = *(int*)(vecData - 4);
uintptr_t wnd = *(uintptr_t*)(vecData + i * 4);
```

证据：

| 证据 | 内容 | 置信度 |
| --- | --- | --- |
| `sub_BA7810` | `sub_8A25A0((unsigned int **)(dword_F59D40 + 19060), ...)` 后写入 `this`，这是 vector push_back 语义 | A |
| `sub_9CB730` | 同样对 `dword_F59D40 + 19060` 调 `sub_8A25A0`，写入窗口对象 | A |
| `sub_BC6260` | `v1 = *(this + 4765)`，若 `*(v1 - 4)` 非 0，则返回最后一项 `*(v1 + 4*count - 4)` | A |
| `sub_8A2280` | 遍历 `*(this+4765)`，按 `data[i] == a2` 删除，并 `--*(data-4)` | A |
| `sub_BE9EA0` | 初始化 `*(this + 4765) = 0`，符合 vector data 指针字段 | A |
| `sub_BEADD0` | 析构时释放 `*((DWORD*)this + 4765)` 指向的堆块 | A |

已修正代码：

- `src/dllmain.cpp`：`CollectSuperBtnOccluderRects()` 改为先解 `CWndMan+0x4A74` 的 vector data，再按 count 枚举。
- `src/ui/super_imgui_overlay.cpp`：`UpdateOverlayVisiblePieces()` 和 `ProbeQuickSlotTopLevelWindow()` 同步修正。
- `src/core/GameAddresses.h`：注释改为 “toplevel 窗口 vector data 指针”。
- 本轮源码 marker 已更新到 `v20.3-2026-04-11-second-child-safety`；当前已运行游戏进程仍可能是旧注入 DLL，需看日志 marker 区分。

影响：

- 修正前：第 0 项会把 vector data 指针本身当成 CWnd，后续项会读到 CWndMan 结构字段，遮挡枚举容易为空或随机。
- 修正后：可以真正读到所有 top-level CWnd 指针和矩形。
- 仍需注意：top-level vector 是“窗口集合”，不是已证实的前后层级顺序。
- 进一步纠偏：不能因为 `CWndMan+0x4A74` 为空，就把附近任何“`vec[-1]` 像 count”的字段当作替代 top-level vector。`0x4A64 / 0x4A68` 在 `sub_C08290` 等代码里会被当作带虚表调用和字符串查询的对象使用，不是已证实的 top-level `CWnd*` 数组。

## 4. 原生窗口创建总入口

核心入口：`sub_BD1A20(CWndMan this, mode, arg3, arg4, arg5)`

功能模型：

1. 根据 `mode` 切换窗口类型。
2. 如果对应 CWndMan slot 为空，则 `sub_401FA0(&dword_F68F50, size)` 分配对象。
3. 调对应构造函数。
4. 调 slot wrapper，例如 `sub_BD08A0((LONG*)(this+18320), wnd)`。
5. wrapper 对新对象 `+0x0C` 引用计数加一，把对象写进 `slot+4`，释放旧对象。

重要 slot wrapper 证据：

| 函数 | 用途 | 置信度 |
| --- | --- | --- |
| `sub_BD08A0` | case19 slot wrapper，写入 `*(this+1)`，释放旧对象走 `sub_BC4130` | A |
| `sub_BD0620` | case32 MacroWnd slot wrapper，写入 `*(this+1)`，释放旧对象走 `sub_BC3E30` | A |

## 5. `sub_BD1A20` 模式表

| case | CWndMan slot base | 对象大小 | ctor | 已知/推定名称 | 置信度 |
| --- | ---: | ---: | --- | --- | --- |
| 0 | `+18192` | `0xBE4` | `sub_90C980` | 未命名窗口 | B |
| 1 | `+18200` | `0xB30` | `sub_8E18F0` | 未命名窗口 | B |
| 2 | `+18208` | `0xB94` | `sub_9FBCE0(v,0)` | 未命名窗口 | B |
| 3 | `+18216` | `0xC00` | `sub_9E1110(v,0,arg3)` | 未命名窗口；特殊条件可改走 case32 | B |
| 5 | `+18232` | `0x11C4` | `sub_922AE0` | 未命名窗口 | B |
| 6 | `+18248` | `0xBAC` | `sub_9AA000(v,arg3)` | 未命名窗口 | B |
| 10 | `+18288` | `0x1660` | `sub_A65540` | 未命名窗口 | B |
| 17 | `+18312` | `0x15B0` | `sub_9723A0` | 未命名窗口 | B |
| 19 | `+18320` | `0xAEC` | `sub_8D97F0` | SkillWndEx / 技能窗 | A |
| 21 | `+18344` | `0xAFC` | `sub_983EF0` 或 `sub_983C40` | 未命名窗口 | B |
| 22 | `+18352` | `0x17A0` | `sub_9136E0` | 未命名窗口 | B |
| 25 | `+18360` | `0xB7C` | `sub_9CB400` | 未命名窗口，有等级/消息限制分支 | B |
| 26 | `+18368` | `0x1544` | `sub_900180(v,this+19144,this+19168)` | 未命名窗口 | B |
| 27 | `+18376` | `0x15BC` | `sub_900890` | 未命名窗口 | B |
| 28 | `+18392` | `0xB84` | `sub_976740` | 未命名窗口 | B |
| 29 | `+18400` | `0xAE8` | `sub_976A60` | A996B0-family 轻量窗口；项目 route-B 相关 | A |
| 30 | `+18280` | `0x1608` | `sub_945520(v,-1)` | 未命名窗口 | B |
| 31 | 无 slot | 无 | `sub_BC6E00()` | 纯动作/弹出流程 | B |
| 32 | `+18224` | `0xBA4` | `sub_9EAF60(v,0,arg3)` | MacroWnd | A |
| 35 | `+18424` | `0x157C` | `sub_8A4BC0` | 未命名窗口 | B |
| 39 | `+18384` | `0xBD8` | `sub_8A9290` | 未命名窗口 | B |
| 40 | `+18540` | `0x15F0` | `sub_BB5F30` | 未命名窗口，创建后可能立刻关闭并提示 | B |
| 41 | `+18548` | `0x1508` | `sub_BA37D0` | 未命名窗口，受角色条件限制 | B |
| 42 | `+18464` | `0x18DC` | `sub_92D5C0` | 未命名窗口 | B |
| 43 | `this + 8*v16 + 18088` | `0xB50` | `sub_8A3640(v,v16)` | 分组窗口 0 | B |
| 44 | `this + 8*v16 + 18088` | `0xB50` | `sub_8A3640(v,v16)` | 分组窗口 1 | B |
| 45 | `this + 8*v16 + 18088` | `0xB50` | `sub_8A3640(v,v16)` | 分组窗口 2 | B |
| 46 | `this + 8*v16 + 18088` | `0xB50` | `sub_8A3640(v,v16)` | 分组窗口 3 | B |
| 47 | `+18472` | `0xB78` | `sub_99DF20` | 未命名窗口 | B |
| 50 | 无 slot | 无 | `sub_BC6F60()` | 纯动作/弹出流程 | B |
| 51 | `+18500` | `0xBB4` | `sub_9191F0` | 未命名窗口 | B |
| 55 | `+18508` | `0x15E0` | `sub_8F0130` | 未命名窗口 | B |
| 58 | `+18516` | `0xB74` | `sub_8FCD00` | 未命名窗口 | B |
| 201 | `+18408` | `0xAFC` | `sub_8D6FC0` | 未命名窗口 | B |

注意：slot base 不是对象指针本身。多数 wrapper 把真实对象写在 `slot base + 4`。

## 6. CWnd 核心布局

| 字段 | 偏移 | 说明 | 置信度 |
| --- | ---: | --- | --- |
| vtable1 | `+0x00` | CWnd 主虚表 | A |
| vtable2 | `+0x04` | interface/可见性/焦点相关虚表 | A |
| vtable3 | `+0x08` | 额外 interface | B |
| refcount | `+0x0C` | wrapper retain/release 使用 | A |
| WndID | `+0x14` | `sub_B9A660` 写 `++dword_F6AD50` | A |
| COM surface | `+0x18` | Canvas/COM surface | A |
| width | `+0x28` | 宽 | A |
| height | `+0x2C` | 高 | A |
| region | `+0x30` | CWnd dirty/clip region 起点，`this+12` DWORD | A |
| render X | `+0x44` | 原生绘制使用坐标，证据来自 `sub_B9B800/sub_B9DF60` | A |
| render Y | `+0x48` | 原生绘制使用坐标 | A |
| z/layer 参数 | `+0x80` | `sub_B9A660` 写入 `a10`，但不等于完整 z-order | A |
| child draw list | `+0x70` / `this[28]` | `sub_B9F6E0/sub_B9BB60` 遍历 | A |
| child hit list | `+0x74` / `this[29]` | `sub_B9B620` 遍历 | A |
| home X/Y | `+2756/+2760` | 项目已验证可读写 home 坐标 | A |
| COM screen X/Y | COM `+0x54/+0x58` | `CWnd_GetX/Y` 读取 | A |

矩形读取优先级建议：

1. `renderX/renderY + width/height`
2. 若 render 坐标异常，再用 COM `+0x54/+0x58`
3. 若仍异常，再用 home X/Y 作为候选
4. 宽高 <=0 或 >4096 视为无效窗口

## 7. 原生窗口创建后注册链

核心函数：`sub_B9A660`

已确认流程：

1. `*(this+5) = ++dword_F6AD50`，设置 WndID。
2. `*(this+32) = a10`，记录 layer/z 参数。
3. 调 vtable `+8` 初始化窗口基础参数。
4. 若 `this+0x18` COM surface 不存在，则创建 Canvas/COM surface。
5. 调 `sub_4C6D10(dword_F5E8D4, &a7, this[32])` 取得 layer 管理对象并绑定到 surface。
6. 调 vtable `+12` 做派生类初始化。
7. 调 `dword_F6707C(this+12, 0, 0, w, h)` 初始化窗口 region。
8. 调 `sub_BA20E0(this)` 加入 dirty list。
9. 调 `sub_BA1E80(this)` 加入 z/order list。
10. 若 `a9` 为真，调用 `sub_B9EEA0(this+1)` 做 active/focus 同步。
11. 最后根据当前鼠标位置 `sub_B9EA60` + `sub_B9F570` 更新 hover/focus。

置信度：A

## 8. 四类全局窗口集合

| 集合 | 地址/偏移 | 作用 | 是否等于层级 | 置信度 |
| --- | --- | --- | --- | --- |
| CWndMan mode slots | `CWndMan + 18192..185xx` | 每种窗口类型的单例 slot，由 `sub_BD1A20` 管理 | 否 | A |
| Top-level vector | `*(CWndMan+0x4A74)` | 所有 top-level 窗口对象集合，可用于枚举矩形 | 否，顺序未证实 | A |
| Z/order list | `off_F57410`，head=`dword_F5741C`，tail=`dword_F57420` | `sub_BA1E80` 按 COM depth 排序插入；`sub_B9F570` 从 tail 向前做鼠标命中扫描 | 是层级和命中候选 | A/B |
| Dirty list | `dword_F57444 / off_F57438` | `sub_B9F6E0` 只重绘 dirty windows | 否 | A |

## 9. Z/order list 机制

核心函数：`sub_BA1E80`

证据：

- 从 CWnd 的 COM surface 调 vtable `+176` 读取当前 depth/order 值。
- 若存在另一个关联 COM 对象，也读它的 depth/order。
- 遍历 `dword_F5741C`。
- 对每个已有窗口，读取其 COM surface vtable `+176`。
- 若已有窗口 depth `v23 > 新窗口 depth v22`，则在当前节点前插入。
- 否则继续向后，最后 append。
- 插入完成后对新窗口 COM surface 调 vtable `+180` 写回原 depth。

排序模型：

```text
F5741C 链表按 COM depth 非递减排序。
遇到第一个 depth 更大的已有节点，就把新窗口插到它前面。
```

置信度：

- “按 COM vtable+176 的 depth/order 排序”：A
- “链表从低到高还是从后到前渲染”：B
- “只比较 `CWnd+0x80` 就能判断遮挡”：C，不建议这么做

开发建议：

- 要判断某窗口是否在 SkillWnd 上方，不能只读 `CWnd+0x80`。
- 应运行期 dump `F5741C`，记录每个对象的链表 index，并用实际遮挡窗口做对照。
- 若确认链表后部为更上层，则只裁掉 anchor 之后的窗口；若确认链表前部为更上层，则相反。

## 10. Dirty render 链不是鼠标链

重要纠偏：旧 `CLAUDE.md` 曾把 `0x00B9F6E0` 标为“UI 鼠标扫描总链”。IDA 显示它实际是 dirty render 链。

`sub_B9F6E0` 行为：

1. 遍历 `dword_F57444` dirty list。
2. 对每个 dirty CWnd 取 `wnd+12` region。
3. 若 region 非空，调用 `sub_B9BBF0` 准备 clip/variant。
4. 调窗口虚函数 `vtable+44` 绘制自身。
5. 遍历 `wnd[28]` 子绘制列表。
6. 对子对象调 `vtable+28` 取 rect。
7. 检查子 interface `+44` 可见，且 region 相交。
8. 调子对象 `vtable+36(child, x, y, clip)` 绘制。
9. 清空 dirty region。
10. 最后 `sub_B9F2E0(&off_F57438)` 清 dirty list。

结论：

- `sub_B9F6E0` 是“只画脏窗口/脏区域”的函数。
- 它不能代表所有窗口的层级顺序。
- 在 `sub_BBC460` 的 `sub_B9F6E0` 之后打纯 D3D 色块曾日志成功但肉眼不可见，说明这个点也不是可靠的可见 overlay 时机。

置信度：A

## 11. 鼠标命中链

核心函数：`sub_B9F570`

模型：

1. 先检查 `dword_F5E8D4` 输入/layer manager 内部优先 list，header 起点在 `+0x84`，其中 `+0x8C` 是 count，`+0x94` 是 tail cursor。
2. 如果 manager 内部优先 list 非空，则先对它的 tail 候选做 hit-test。
3. 若没有 modal/active candidate，则从 `off_F57410` 的 tail，即 `dword_F57420`，向前遍历同一条 z/order list。
4. 对每个窗口，调用 interface vtable `+52` 和 `+48` 做坐标转换。
5. 调窗口 vtable `+36` 做命中测试。
6. 如果窗口命中且有子控件命中，则检查子 interface `+36` 和 `+44`，返回 `child+4`。
7. 如果没有子控件命中，则返回 `window+1`。
8. 遍历中第一个命中的候选直接返回。

置信度：

- “`sub_B9F570` 是鼠标/交互候选查询链”：A
- “`dword_F57420` 是 `off_F57410` tail，不是独立 interact list”：A
- “tail 先扫到的命中项就是鼠标意义上的当前获胜对象”：A
- “tail 方向是否等同视觉最上层”：B，需要运行期对照遮挡/点击确认

## 12. 子控件命中链

核心函数：`sub_B9B620`

行为：

1. 遍历 `this[29]` 子命中列表。
2. 对每个 child，先查 child interface `+44` 是否可见。
3. 调 child vtable `+44` 和 `+40` 取子坐标/转换。
4. 调 child vtable `+24` 做相对坐标命中。
5. 第一个命中的 child 写入 `a4` 并返回 `2`。
6. 如果没有 child 命中，但点在父窗口宽高内，也返回 `2`。
7. 否则返回 `0`。

置信度：A

## 13. 窗口关闭/注销链

核心函数：`sub_B9E880`

行为：

1. 调当前窗口虚函数 `+16` 做关闭前处理。
2. 关闭/释放 child list。
3. 释放 COM surface 和关联资源。
4. 清 `WndID`。
5. 调 `sub_BA01D0(this)` 从 z/order list 移除。
6. 调 `sub_BA0210(this)` 从 pending/action list 移除。
7. 调 `sub_BA0240(this)` 从 dirty list 移除。
8. 调 `sub_BA0680(this+1)` 从 `dword_F5E8D4` manager 相关状态移除，并可能触发从 `dword_F57420` tail 回退重选。
9. 重新根据鼠标位置 `sub_B9EA60 + sub_B9F570` 更新 hover/focus。

置信度：A

## 14. 遮挡计算：原生真实逻辑 vs 我们可用逻辑

原生真实遮挡：

- CWnd 自身和子控件通过 dirty region + COM/Canvas depth/order 绘制。
- 真实遮挡不仅是矩形，还包含 COM surface、alpha、资源透明像素、不规则窗口区域、鼠标绘制时机。
- 仅用 `CWnd+0x80` 或 top-level vector 顺序无法完全复刻。

我们可用的矩形级遮挡：

1. 从 top-level vector 读取所有 CWnd。
2. 对每个窗口读矩形。
3. 忽略 SkillWnd、自建按钮、自建 panel、donor 资源对象。
4. 只保留与我们的按钮/面板相交的窗口。
5. 从按钮/面板矩形中减去这些窗口矩形，得到 visible pieces。
6. 绘制时只画 visible pieces。
7. 输入 hit-test 也只对 visible pieces 生效。

当前修正后的能力：

| 能力 | 状态 | 置信度 |
| --- | --- | --- |
| 读取所有 top-level CWnd 指针 | 已具备，需部署新版 DLL 后运行验证 | A/B |
| 读取 top-level CWnd 矩形 | 已具备，使用 render/com/home 坐标兜底 | A |
| 保守裁掉所有相交窗口 | 已具备 | A |
| 只裁掉 SkillWnd 上方窗口 | 需要 dump `F5741C` 后确认方向 | B |
| 像素级透明/不规则遮挡 | 当前做不到 | A |
| 软件鼠标永远盖在最上层 | Present/D3D 自绘做不到，除非重绘鼠标或接入原生 | A/B |

## 15. D3D8/D3D9 自绘能否接入游戏层级

结论：单纯 D3D8/D3D9 自绘不能接入原生 CWnd/Canvas 层级。

原因：

- Present/EndScene/D3D8 Present hook 是在游戏已把原生 UI 画进 backbuffer 后追加 draw call。
- 这些 draw call 不会进入 `CWndMan` slot、`off_F57410` z/order/hit-scan list、dirty region 和 child list。
- 因此原生窗口不知道我们的自绘面板存在，也不会自动遮挡、命中、排序或把软件鼠标盖到它上面。

可选路线：

| 路线 | 能力 | 风险 | 结论 |
| --- | --- | --- | --- |
| D3D8 直接在游戏设备画 | 当前 D3D8 模式可用，避免 D3D8/D3D9 双 swap chain 黑屏 | 仍是非原生层级 | 适合稳定可见 + 手工裁切 |
| D3D9 Present/EndScene 画 | D3D9 模式可用 | 非原生层级，可能压鼠标/压窗口 | 只适合 fallback |
| SkillWnd draw hook 后补画 | 更接近 SkillWnd 本体 | 会被后续 UI 覆盖，历史实验不可见/不稳定 | 不作为主线 |
| 创建原生 CWnd top-level | 可理论接入 `off_F57410` 与 dirty list | 构造链、COM layer、生命周期、输入同步高风险 | 仅可实验，不是当前稳定主线 |
| 创建 SkillWnd 原生 child | 理论上最接近原生体验 | 需要正确进入 `this[28]/this[29]` 子列表和官方 second-child slot，历史路线失败较多 | 可继续逆向，但不能承诺 |
| 官方 `9DC220` second-child route | 可进入 CWnd dirty 与 `off_F57410` z/order/hit-scan 链，slot 属于 SkillWndEx `+3044/+3048` | 不是普通控件 child；对象仅 `0x84`，不能用 A996B0 home 偏移；详见 second-child 专属知识库 | 原生 carrier 优先级最高 |

置信度：A/B

## 16. 软件鼠标为什么总是特殊

已知现象：

- 游戏软件鼠标视觉上应位于所有 UI 最上层。
- Present/D3D 自绘如果在最终 Present 前追加绘制，通常会盖住已经画进 backbuffer 的软件鼠标。
- 项目曾尝试自定义鼠标和 `InputSpoof`，日志出现 `[InputSpoof] FAIL: IAT slot missing`，说明隐藏原生鼠标不稳定。

结论：

- 若继续 D3D 自绘，必须接受“鼠标不是原生层级”的问题。
- 可选补救是自绘鼠标放在我们的 panel 之后，但这又会和原生鼠标叠加，且偏移/帧对齐需要独立处理。
- 真正让原生鼠标自然盖住我们，必须把我们变成原生 CWnd/child，或者找到原生鼠标绘制前的插入点。

置信度：B

## 17. 加载、黑屏、分辨率切换时的隐藏策略

当前 D3D8 证据：

- 日志确认 D3D8 Present hook 生效。
- D3D8 Reset hook 生效，Reset 时释放纹理，Reset OK 后下一帧重载。
- v20.1 已加入 D3D8 状态保存/恢复：VS、Viewport、Transform、Texture、TSS、BeginScene/EndScene。

必须隐藏 overlay 的条件：

| 条件 | 建议动作 | 置信度 |
| --- | --- | --- |
| `g_SuperExpanded == false` | 不画 panel | A |
| `g_SkillWndThis == 0` 或 SkillWnd 矩形无效 | 不画按钮/面板 | A |
| `ADDR_CWndMan` 不可读或 `*(CWndMan+0x4A74)` 不可读 | 不做遮挡枚举，必要时不画 | A |
| D3D Reset/ResetEx 进行中 | 不画，释放/重建资源 | A |
| D3D8 textures 未加载 | 不画，先加载 | A |
| 游戏 HWND 为空、client rect 为 0、窗口最小化 | 不画 | A |
| 分辨率切换后 viewport/client size 改变 | 重新计算 anchor/矩形/visible pieces | A |
| 黑屏加载期间 top-level vector count 为 0 或 SkillWnd 消失 | 隐藏，等待 debounce 后恢复 | B |
| Present 中连续 N 帧 SkillWnd 不可见/不可读 | 隐藏并清输入状态 | B |
| 加载切图时 CWndMan 还在但 SkillWnd 不在 top-level vector | 隐藏 SkillWnd 相关 overlay | B |

推荐判定顺序：

1. 先看图形设备状态：Reset 中、纹理状态、HWND/client rect。
2. 再看游戏 UI 状态：CWndMan、top-level vector、SkillWnd 全局指针。
3. 再读 anchor/rect。
4. 最后做遮挡枚举。

## 18. 更准确遮挡的下一步采样方案

因为外部 `PROCESS_VM_READ` 被拒绝，必须在 DLL 内部增加采样日志。

建议新增一次性或热键触发 sampler，日志格式建议：

```text
[WndDump] frame=... reason=toggle/open/click/reset
[WndDump:TopVec] vec=0x... count=N
[WndDump:Top] i=0 obj=0x... vt=0x... w=... h=... render=(x,y) com=(x,y) home=(x,y) zField=... slotCase=...
[WndDump:Z] order=0 obj=0x... topVecIndex=... depthMaybe=...
[WndDump:ZTailHitScan] order=0 obj=0x... topVecIndex=...
[WndDump:Hit] mouse=(x,y) result=0x... owner=...
```

采样场景：

- 打开 SkillWnd 前后。
- 打开 MacroWnd、Inventory、Quest、NPC 对话框、Tooltip 等常见遮挡窗口前后。
- 拖动 SkillWnd 与其他窗口重叠。
- 鼠标点击被遮挡区域和未遮挡区域。
- D3D8 Reset/分辨率切换前后。
- 黑屏加载/切图期间。

采样目标：

1. 确认 top-level vector 是否包含所有可见 top-level 窗口。
2. 确认 `F5741C` 链表方向与实际前后遮挡关系。
3. 确认 `F57420` 作为 `off_F57410` tail 的扫描方向与点击命中关系。
4. 建立 `CWndMan slot case -> obj` 映射，给窗口对象命名。
5. 识别 SkillWnd、MacroWnd、StatusBar、QuickSlot、Inventory 等常见窗口矩形。

置信度：A，采样必要性明确。

## 19. 当前最可靠结论

| 结论 | 置信度 |
| --- | --- |
| 原生窗口由 `sub_BD1A20` 按 mode 创建，并写入 CWndMan 对应 slot | A |
| CWnd 创建后由 `sub_B9A660` 注册 dirty list、z/order list，并可能同步 active/focus | A |
| `CWndMan+0x4A74` 是 top-level vector data 指针，不是内联数组 | A |
| `dword_F57444` 是 dirty render list，`sub_B9F6E0` 只处理 dirty 绘制 | A |
| `off_F57410` 是按 COM depth/order 排序的全局窗口 list，`dword_F5741C` 是 head，`dword_F57420` 是 tail | A |
| `sub_B9F570` 从 `dword_F57420` tail 向前扫描，第一个命中项获胜 | A |
| `sub_B9B620` 是 CWnd 子控件 hit-test | A |
| D3D8/D3D9 自绘不能天然接入原生窗口层级 | A |
| 可以通过读取 top-level vector 的所有窗口矩形来做保守遮挡裁切 | A |
| 要只隐藏“被上层窗口遮住”的部分，需要确认 `F5741C` 链表方向 | B |
| Present/D3D 自绘无法自然位于原生软件鼠标下方 | B |
| 加载/黑屏/分辨率切换应以 SkillWnd/CWndMan/Reset/HWND/client rect 联合判断是否隐藏 | A/B |

## 20. 对当前项目的开发建议

短期稳定路线：

1. 使用 D3D8/D3D9 自绘继续保证可见。
2. 使用修正后的 top-level vector 读取所有窗口矩形。
3. 先做保守矩形裁切，避免覆盖明显在上层的原生窗口。
4. 输入命中只接受 visible pieces。
5. 加载/Reset/SkillWnd 消失时立即隐藏。

中期精确路线：

1. 加 DLL 内部 sampler，dump top vector、`off_F57410` head=`F5741C` 与 tail=`F57420`。
2. 用实际窗口重叠实验确认 `F5741C` 前后方向。
3. 改遮挡过滤为“只裁 anchor 上方窗口”。
4. 给常见 slot/case 建名称映射，降低误裁。

长期原生路线：

1. 优先落地 SkillWnd 官方 second-child route：`ADDR_9DC220`、`ADDR_9D93A0`、`this+3044/3048`，但必须遵守 `skillwnd_second_child_route_knowledge.md` 的 `0x84` 对象边界。
2. 避免野生 top-level CWnd 直接插入，除非能完整复刻 COM layer、dirty、z/order、interact、生命周期。
3. 真正要让鼠标和所有原生窗口自然遮挡，只能走原生 CWnd/child 接入，不是 Present 自绘能解决的问题。
