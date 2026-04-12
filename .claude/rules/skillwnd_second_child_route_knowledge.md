# SkillWnd 官方 second-child route 专属知识库

更新时间：2026-04-11

适用工程：`G:\code\c++\SuperSkillWnd`

本文件专门记录 SkillWndEx 官方 `9DC220` second-child route 的逆向结论、可落地调用方案、风险边界和运行期采样计划。使用时必须同时读取：

- `.claude/rules/CLAUDE.md`
- `.claude/rules/d3d8_compat_knowledge.md`
- `.claude/rules/native_window_layering_knowledge.md`
- `.claude/rules/native_window_sampler_knowledge.md`
- `.claude/rules/skillwnd_second_child_lifecycle_knowledge.md`
- `.claude/rules/skillwnd_second_child_primary_carrier_knowledge.md`
- `.claude/rules/skillwnd_second_child_tasks_4_5_6_knowledge.md`

## 1. 置信度标准

| 等级 | 含义 |
| --- | --- |
| A | 直接来自 IDA 伪代码/汇编、本地二进制字符串、源码或已验证运行日志。 |
| B | 多条证据一致，但具体运行期表现仍需 DLL 内采样确认。 |
| C | 合理推断或设计建议，不能当成事实写死。 |

## 2. 执行结论

| 结论 | 置信度 |
| --- | --- |
| `9DC220` 是 SkillWndEx 官方 owned second-child slot 创建/替换链。 | A |
| second-child 指针在 `SkillWndEx+0xBE8 / +3048`，release wrapper base 在 `SkillWndEx+0xBE4 / +3044`。 | A |
| 该 child 对象由 `9DB2B0` 构造，分配大小固定 `0x84`，是小型 CWnd，不是 MacroWnd/A996B0 大对象。 | A |
| 该 child 通过 `B9A660` 注册 dirty list 和 `off_F57410` z/order 链，因此比 D3D overlay 更接近原生层级。 | A |
| 它不是 `SkillWndEx+0xBEC` 控件容器中的普通 child control，不会自动进入 SkillWnd 控件列表。 | A |
| `9D95A0` 移动 SkillWnd 时只同步 MacroWnd `+2948`，不会同步 second-child `+3048`。 | A |
| 当前源码里的 `CWnd_SetHomePos/GetHomeX/Y` 不能用于该官方 child，因为 `+2756/+2760` 超出 `0x84` 对象边界。 | A |
| 当前源码里的 `SetSuperWndVisible()` 对官方 child 基本无效，因为该 child 的 VT2 对应 show/visible 槽是 nullsub/固定返回。 | A |
| 如果要落地，推荐“创建时立即缩小/移动/替换 draw，隐藏时 close+release”，不要保留 800x600 后靠 show/hide 隐藏。 | A/B |

一句话：`9DC220` 是当前最值得继续实验的原生 carrier 路线，但必须按 `0x84` 官方 child 规则写，不能混用 MacroWnd/A996B0 的 home 坐标和可见性接口。

## 3. 地址与函数地图

| 地址 | 函数 | 作用 | 置信度 |
| --- | --- | --- | --- |
| `009DDB30` | `sub_9DDB30` | SkillWndEx 控件消息分发；ctrlID `3001..3004` 进入 `9DC220`。 | A |
| `009DC220` | `sub_9DC220` | 创建/替换 second-child slot。 | A |
| `009DB2B0` | `sub_9DB2B0` | second-child 构造函数，分配体大小 `0x84`。 | A |
| `009D93A0` | `sub_9D93A0` | second-child wrapper release helper。 | A |
| `009D97E0` | `sub_9D97E0` | SkillWnd 侧主动关闭 second-child。 | A |
| `009D98F0` | `sub_9D98F0` | second-child 鼠标消息处理，`514/517` 时关闭 second-child。 | A/B |
| `009D95A0` | `sub_9D95A0` | SkillWnd move；只移动 MacroWnd，不移动 second-child。 | A |
| `009E14D0` | `sub_9E14D0` | SkillWndEx 析构；release `+3048` second-child。 | A |
| `009E17D0` | `sub_9E17D0` | SkillWndEx 子控件初始化；创建 MacroWnd 和按钮容器，不创建 second-child。 | A |
| `00AE2900` | `sub_AE2900` | 另一条官方 second-child 创建路线，mode 来自 `this+697` 列表 count。 | A |
| `00B9A660` | `sub_B9A660` | CWnd 初始化并注册 dirty/z-order/hit scan。 | A |
| `00B9AB50` | `sub_B9AB50` | CWnd resize/reposition/surface rebuild。 | A |
| `0056D630` | `sub_56D630` | 移动 CWnd COM surface。 | A |
| `00B9A5D0` | `sub_B9A5D0` | 标记 CWnd dirty。 | A |
| `00B9E880` | `sub_B9E880` | CWnd close/unregister。 | A |
| `00BA1E80` | `sub_BA1E80` | 按 COM depth 插入 `off_F57410` z/order 链。 | A |
| `00B9F570` | `sub_B9F570` | 从 `off_F57410` tail 扫描鼠标命中。 | A |

## 4. SkillWndEx 相关偏移

| 偏移 | 字段 | 说明 | 置信度 |
| ---: | --- | --- | --- |
| `+0xB80 / +2944` | MacroWnd wrapper base | `9E17D0`/`9D95A0`/`9E14D0` 使用。 | A |
| `+0xB84 / +2948` | MacroWnd pointer | SkillWnd move 时移动到 `parentX+174,parentY`。 | A |
| `+0xBBC / +3004` | 内部列表/资源区 | `9E14D0` 清理，具体语义不要过度命名。 | B |
| `+0xBE4 / +3044` | second-child wrapper base | 必须作为 `9D93A0` 的 ECX。 | A |
| `+0xBE8 / +3048` | second-child pointer | 官方 `9DC220` 创建后写入这里。 | A |
| `+0xBEC / +3052` | control container | `9E17D0` 传给 `6688B0`，按钮控件挂这里；second-child 不挂这里。 | A |

## 5. second-child 对象布局

`9DC220` 调 `401FA0(&dword_F68F50, 0x84)`，所以对象可安全访问范围只有 `0x00..0x83`。

| 偏移 | 字段 | 来源/含义 | 置信度 |
| ---: | --- | --- | --- |
| `+0x00` | VT1 | `9DB2B0` 写 `off_E66C00`。 | A |
| `+0x04` | VT2 | `9DB2B0` 写 `off_E66BA8`。 | A |
| `+0x08` | VT3 | `9DB2B0` 写 `off_E66BA4`。 | A |
| `+0x0C` | refcount | wrapper retain/release 使用。 | A |
| `+0x14` | WndID | `B9A660` 写 `++dword_F6AD50`。 | A |
| `+0x18` | COM surface | `B9A660/B9AB50/435A50` 使用。 | A |
| `+0x1C/+0x20` | 额外 surface/object | `B9BF60/B9B800/B9E880` 管理。 | A |
| `+0x28/+0x2C` | width/height | `B9A660/B9AB50` 写。 | A |
| `+0x30` | region/list 起点 | dirty/clip region，`this+12` DWORD。 | A |
| `+0x40` | surface/layer flag | `B9AB50` 写 `a7`。 | A |
| `+0x44/+0x48` | draw offset/render pos 候选 | `B9B800` 使用。可读写前需运行期对照。 | B |
| `+0x70/+0x74` | child draw/hit lists | `B9BF60` 初始化；普通 child 列表。 | A |
| `+0x7C` | draw image ptr | `B9B800` 若非空会绘制。 | A |
| `+0x80` | `B9A660` 的 `a10` | 官方构造传 0；不是完整 z-order。 | A |

禁止把这些偏移用于官方 second-child：

| 禁止项 | 原因 | 置信度 |
| --- | --- | --- |
| `+2756/+2760` home X/Y | 超出 `0x84` 对象边界，会读写别的 heap 内存。 | A |
| `A996B0-family` 扩展字段 | second-child 不是 A996B0/MacroWnd 大对象。 | A |
| `SkillWndEx+2948/+3048` 等父对象字段 | 这些是 SkillWndEx 字段，不在 child 对象内。 | A |

## 6. vtable 关键槽位

官方 second-child 构造后 vtable 固定：

| vtable | 地址 | 关键槽位 | 置信度 |
| --- | --- | --- | --- |
| VT1 | `00E66C00` | index 11 = `00B9B800` 默认 draw。 | A |
| VT2 | `00E66BA8` | index 2 = `009D98F0` 鼠标消息处理；index 8/10 是 nullsub；index 9/11 固定返回 1。 | A |
| VT3 | `00E66BA4` | adjustor thunk/interface 区。 | B |

落地含义：

- 替换 VT1 index 11 接自定义 draw 是结构上合理的。
- 替换前必须确认 `*(child+0) == 0x00E66C00`，避免误 patch 非官方对象。
- 自定义 draw 只能使用 `435A50` 取 surface、`+0x28/+0x2C` 取大小、COM/render 安全字段，不能读 home X/Y。
- 当前 `SetSuperWndVisible()` 通过 VT2 index 8/10 show/hide 对官方 child 不成立。

## 7. 参数闭环：ctrlID、mode、资源路径

`9DDB30` 伪代码显示：

```cpp
if ((unsigned int)a2 - 3001 <= 3)
    sub_9DC220(this, a2 - 750);
```

这里 IDA 把 ctrlID 误反编译成 `int*`，所以 `a2 - 750` 是指针步长语义。汇编实质是：

```asm
lea edx, [ebp-0BB9h] ; ctrlID - 3001
cmp edx, 3
lea eax, [ebp-0BB8h] ; ctrlID - 3000
push eax
call sub_9DC220
```

正确映射：

| ctrlID | 传给 `9DC220/9DB2B0` 的 mode | `9DB2B0` 格式化路径 | 置信度 |
| ---: | ---: | --- | --- |
| `3001` | `1` | `UI/UIWindow.img/AranSkillGuide/0` | A |
| `3002` | `2` | `UI/UIWindow.img/AranSkillGuide/1` | A |
| `3003` | `3` | `UI/UIWindow.img/AranSkillGuide/2` | A |
| `3004` | `4` | `UI/UIWindow.img/AranSkillGuide/3` | A |

二进制字符串证据：

- `0x00E66B60` 是宽字符串格式：`UI/UIWindow.img/AranSkillGuide/%d`
- `strings_index.csv` 从子串 `0x00E66B64` 命中 `/UIWindow.img/AranSkillGuide/%d`

重要失败路线：

- 不能把 `2251..2254` 传给 `9DB2B0`。
- 不能把 ctrlID `3001..3004` 原样传给 `9DB2B0`。
- 正确直接调用是 `9DC220(skillWndThis, mode=1..4)`。

## 8. 生命周期闭环

创建/替换：

1. `9DC220` 先检查 `SkillWndEx+3048`。
2. 如果已有 child，先 `B9E880(child)` close。
3. 如果 slot 仍非空，调用 `9D93A0(skillWndThis+3044, 0)` release，并清 `+3048`。
4. 分配 `0x84`。
5. 调 `9DB2B0(newObj, mode)`。
6. 对 `obj+0x0C` refcount 加一。
7. 写入 `SkillWndEx+3048`。
8. 若存在旧对象 wrapper，release 旧对象。

释放：

1. 正常主动关闭应先 `B9E880(child)`，从 dirty/z-order/focus 等全局状态注销。
2. 再 `9D93A0(skillWndThis+3044, 0)`。
3. 最后手动清 `SkillWndEx+3048 = 0`。

析构：

- `9E14D0` 在 SkillWndEx 析构里检测 `+3048`，调用 `9D93A0(this+3044,0)` 并清 slot。
- 析构路径没有在伪代码中显式先调 `B9E880`，所以我们的主动关闭不应依赖析构兜底。

点击关闭：

- `9D98F0` 在消息 `514` 或 `517` 时，如果全局 `dword_F6A0C0` 指向 SkillWnd 且 `+3048` 非空，会 close+release+zero second-child。
- `514/517` 很可能对应鼠标释放类消息。是否会误关我们的自定义 carrier，需要运行期日志确认。

## 9. 原生层级与鼠标命中接入

`9DB2B0` 内部调用：

```cpp
sub_B9A660(0, 0, 800, 600, 10, 1, 0, 1, 0);
```

汇编 push 顺序确认参数语义为：

```text
x=0, y=0, w=800, h=600, comDepth=10, a7=1, a8=0, a9=1, a10=0
```

`B9A660` 做的关键事情：

1. 分配/绑定 COM surface。
2. 设置 COM surface 位置和 depth。
3. 初始化 dirty region。
4. 调 `BA20E0` 插入 dirty list。
5. 调 `BA1E80` 插入 `off_F57410` z/order 链。
6. 若 `a9=1`，调用 `B9EEA0(this+1)` 同步 active/focus。
7. 调 `B9EA60 + B9F570` 根据当前鼠标刷新 hover/focus。

`off_F57410` 语义：

- `dword_F5741C` 是该链 head。
- `dword_F57420` 是该链 tail。
- `BA1E80` 从 head 按 COM depth 非递减排序插入。
- `B9F570` 从 tail 往前扫描，第一个命中的窗口/子控件获胜。
- 所以 `dword_F57420` 不是独立 interact list，而是同一 z/order 链的命中扫描起点。

落地含义：

- second-child 能进入原生 CWnd 层级/dirty/命中栈，这是 D3D8/D3D9 自绘做不到的。
- 但它不在 CWndMan top-level vector 里也有可能成立，因此只扫 top-level vector 可能漏掉它。
- 如果用它做 carrier，应优先从 `off_F57410` 验证层级和 hit，而不是只看 CWndMan top-level vector。

## 10. 位置、尺寸、显示隐藏

默认构造后：

- 位置 `(0,0)`。
- 尺寸 `800x600`。
- COM depth `10`。
- 会绘制 `AranSkillGuide/(mode-1)` 静态图到自身 surface。

必须立即做的修正：

1. 用 `B9AB50(child, x, y, PANEL_W, PANEL_H, 10, 1, 0)` 收缩 surface 和窗口尺寸。
2. 用 `56D630(child, x, y)` 再移动 COM surface。
3. 用 `B9A5D0(child, 0)` 标记 dirty。
4. 替换 VT1[11] 后，由自定义 draw 在 `(0,0)` 画面板内容。

不要做：

- 不要先保留 `800x600` 再靠 show/hide 隐藏。
- 不要调用 `CWnd_SetHomePos(child, ...)`。
- 不要依赖 VT2 show/visible。
- 不要假设 SkillWnd move 会自动移动 second-child。

移动策略：

- 每次 SkillWnd anchor 变化、展开/收起、分辨率切换、D3D Reset 后，都重新算 `x/y` 并调用 `56D630`。
- 如果尺寸也可能变化，则调用 `B9AB50`。
- `9D95A0` 只移动 MacroWnd，所以必须自己同步 second-child。

隐藏策略：

| 场景 | 推荐动作 | 置信度 |
| --- | --- | --- |
| 面板收起 | `B9E880 + 9D93A0 + clear +3048`，下次展开重建。 | A/B |
| SkillWnd 关闭/切图/黑屏 | 同上，立即释放。 | A |
| D3D Reset | 如果走原生 route，暂停 draw/dirty，必要时关闭并重建。 | B |
| 只想临时隐藏 | 暂无 A 级可见性接口；不要用当前 VT2 show/nullsub。 | A |

## 11. 可直接落地的调用配方

创建：

```cpp
bool CreateOfficialSecondChildCarrier(uintptr_t skillWndThis) {
    if (!skillWndThis) return false;
    if (*(DWORD*)(skillWndThis + 3048)) return false;

    int mode = 1; // 1..4
    CallThiscall_1(0x009DC220, skillWndThis, mode);

    uintptr_t child = *(DWORD*)(skillWndThis + 3048);
    if (!IsOfficialSecondChild(child)) {
        ReleaseOfficialSecondChild(skillWndThis);
        return false;
    }

    OfficialSecondChild_SetPanelRect(child, x, y, PANEL_W, PANEL_H);
    InstallDrawHook_VT1_11(child);
    MarkDirty(child);
    return true;
}
```

对象校验：

```cpp
bool IsOfficialSecondChild(uintptr_t child) {
    if (!Readable(child, 0x84)) return false;
    if (*(DWORD*)(child + 0x00) != 0x00E66C00) return false;
    if (*(DWORD*)(child + 0x04) != 0x00E66BA8) return false;
    if (*(DWORD*)(child + 0x08) != 0x00E66BA4) return false;
    int refcnt = *(int*)(child + 0x0C);
    int w = *(int*)(child + 0x28);
    int h = *(int*)(child + 0x2C);
    return refcnt > 0 && refcnt < 100 && w > 0 && h > 0;
}
```

移动/缩放：

```cpp
void OfficialSecondChild_SetPanelRect(uintptr_t child, int x, int y, int w, int h) {
    CallThiscall_7(0x00B9AB50, child, x, y, w, h, 10, 1, 0);
    CallThiscall_2(0x0056D630, child, x, y);
    CallThiscall_1(0x00B9A5D0, child, 0);
}
```

关闭：

```cpp
void ReleaseOfficialSecondChild(uintptr_t skillWndThis) {
    uintptr_t child = *(DWORD*)(skillWndThis + 3048);
    if (child) CallThiscall_0(0x00B9E880, child);
    if (*(DWORD*)(skillWndThis + 3048)) {
        CallThiscall_1(0x009D93A0, skillWndThis + 3044, 0);
        *(DWORD*)(skillWndThis + 3048) = 0;
    }
}
```

自定义 draw 规则：

- `thisPtr` 必须先 `IsOfficialSecondChild` 或至少检查可读。
- 用 `435A50(thisPtr,&surface)` 取 surface。
- 绘制坐标固定用 surface 内 `(0,0)`，不要用 home X/Y。
- 日志只记录 COM/render/width/height/refcount/vtable，禁止读 `+2756/+2760`。

## 12. 当前源码风险点与修正状态

| 位置/逻辑 | 风险 | 建议 |
| --- | --- | --- |
| `CreateSuperWnd()` 曾调 `CWnd_SetHomePos(wndObj, xPos, yPos)` | 对 `0x84` official child 越界写。 | 已移除；只同步 render/com 安全坐标。 |
| `SuperCWndDraw()` 曾调 `CWnd_GetHomeX/Y(thisPtr)` | 对 `0x84` official child 越界读，且 SafeIsBadReadPtr 不能证明属于本对象。 | 已加 official child 分支：只记录 COM/render/size/refcount。 |
| `SetSuperWndVisible()` 调 VT2 `+0x28/+0x20` | 对官方 child 是 nullsub/无效 show。 | 已对 official child 显式 no-op；收起仍走 close+release。 |
| `ApplySuperChildCustomDrawVTable()` 只检查可读，不校验官方 vtable | 可能误 patch 非目标 CWnd。 | 已增加 `VT1/VT2/VT3` 精确校验。 |
| `CreateSuperWnd()` 创建后先默认隐藏 | 当前隐藏无效，可能留下 800x600 或 panel carrier。 | 已移除默认隐藏步骤；创建时立即 resize/move/dirty。 |

源码 marker：`v20.3-2026-04-11-second-child-safety`。

## 13. 与 D3D8/D3D9 overlay 的关系

| 路线 | 是否进入原生层级 | 遮挡/鼠标能力 | 结论 |
| --- | --- | --- | --- |
| D3D8 Present 自绘 | 否 | 只能靠采样矩形手工裁切，鼠标可能被压。 | 当前稳定 fallback。 |
| D3D9 Present/EndScene 自绘 | 否 | 同 D3D8，且 D3D8 模式下容易错 swap chain。 | 只作兼容 fallback。 |
| 官方 second-child carrier | 是，进入 CWnd dirty 和 `off_F57410`。 | 有机会自然参与层级/命中，但生命周期需闭环。 | 原生路线优先级最高。 |
| 野生 CWnd/top-level | 理论可进层级 | 构造、slot、close、focus 风险更高。 | 低于 `9DC220`。 |

如果 second-child route 稳定：

- 可以减少 D3D overlay 的遮挡模拟需求。
- 仍可能需要 D3D/自绘代码来生成面板内容，但 carrier 应放在原生 CWnd surface 中绘制。
- 如果 route 被 `9D98F0` 鼠标释放自动关闭，则要接受“展开时重建”或另起 hook 保护，后者风险更高。

## 14. 运行期采样计划

外部 `OpenProcess(PROCESS_VM_READ)` 已返回 `ERROR_ACCESS_DENIED(5)`，所以采样必须在 DLL 内部完成。

创建时记录：

```text
[SecondChild:Create] skill=0x... slotWrap=0x... child=0x... mode=...
[SecondChild:Core] vt1=0x... vt2=0x... vt3=0x... ref=... wndId=... size=... com=0x...
[SecondChild:Surface] comPos=(x,y) depth=... zField=... flag40=...
[SecondChild:List] zIndex=... fromHead=... fromTail=... prev=0x... next=0x...
```

移动/缩放时记录：

```text
[SecondChild:Rect] reason=... skillPos=(x,y) panel=(x,y,w,h) b9ab50Ret=... comAfter=(x,y)
```

点击/关闭时记录：

```text
[SecondChild:Msg] msg=514/517 childBefore=0x... childAfter=0x...
[SecondChild:Close] reason=... closeOk=... releaseOk=... slotAfter=0x...
```

层级采样：

```text
[WndDump:ZHead] order=0 obj=0x... depth=... rect=...
[WndDump:ZTailHitScan] order=0 obj=0x... depth=... rect=...
[WndDump:Hit] mouse=(x,y) result=0x... owner=0x...
```

必须覆盖场景：

- 打开 SkillWnd 后创建 second-child。
- 展开/收起超级面板。
- 鼠标在面板上按下/释放，确认 `9D98F0` 是否会关闭 carrier。
- 拖动 SkillWnd，确认 second-child 是否跟随我们的同步逻辑。
- 打开背包、任务、NPC、Tooltip 与面板重叠，确认 z/head/tail 与视觉遮挡一致。
- D3D8 Reset/分辨率切换/黑屏加载/切图，确认是否释放或隐藏。

## 15. 当前未完全闭环的数据

| 问题 | 当前判断 | 置信度 | 下一步 |
| --- | --- | --- | --- |
| `9D98F0` 是否必然关闭我们的 carrier | 消息 `514/517` 会关 `+3048`，但触发条件需运行期确认。 | B | 打 log 包住 mouse msg 前后 slot。 |
| `B9AB50` resize 后 official child 的 COM depth/list 是否稳定 | 结构上安全，但视觉要验证。 | B | 采样 depth、list index、实际遮挡。 |
| second-child 是否存在于 top-level vector | 可能不在；不能依赖 top vector。 | B | 同时 dump top vector 和 `off_F57410`。 |
| 链表 tail 是否视觉最上层 | `B9F570` 从 tail 命中是 A；视觉绘制方向仍需对照。 | B | 用重叠窗口采样 head/tail 与截图现象。 |
| 只靠 official child 是否完全解决鼠标压层 | 理论更接近原生，但要看软件鼠标绘制时机。 | B | 实机看鼠标是否盖住 carrier。 |

## 16. 不建议路线清单

| 路线 | 为什么不建议 | 置信度 |
| --- | --- | --- |
| 直接 `9DB2B0(child, 2251)` | mode 错，`sub_419110(..., mode-1)` 会走异常资源路径。 | A |
| 直接 `9DB2B0(child, 3001)` | 同上，ctrlID 不是 constructor mode。 | A |
| 把 `+3048` 当 SkillWnd 控件容器 child | 它不是 `+0xBEC` 控件容器对象。 | A |
| 用 `CWnd_SetHomePos/GetHomeX/Y` 管官方 child | 越界。 | A |
| 用 VT2 show/hide 隐藏官方 child | 对该 vtable 槽位无效。 | A |
| 只扫 CWndMan top-level vector 判断全部层级 | 可能漏掉 second-child 或其他非 top-level CWnd。 | B |
| 野生 CWnd 优先于 `9DC220` | 生命周期和 slot 归属更不确定。 | A/B |

## 17. 最小落地方案

第一阶段只做“可控、不越界、不长期隐藏”的原生 carrier：

1. 仅在展开时调用 `9DC220(skillWndThis, 1)`。
2. 校验 vtable 和 `0x84` 对象核心字段。
3. 立即 `B9AB50 + 56D630` 改到 panel rect。
4. 替换 VT1[11]，custom draw 不读 home。
5. 收起或关闭 SkillWnd 时立刻 `B9E880 + 9D93A0 + clear slot`。
6. 每次 SkillWnd 移动都重新 `56D630`。
7. 全程打印 second-child/list/hit 日志。

第二阶段再决定是否常驻：

- 如果找到官方可见性位或安全 hide 函数，再考虑收起时隐藏而不是释放。
- 如果 `9D98F0` 会频繁关闭，则优先接受重建，不急着 hook 掉官方关闭逻辑。
- 如果层级/鼠标完全符合预期，再把 D3D overlay 降为 fallback。
