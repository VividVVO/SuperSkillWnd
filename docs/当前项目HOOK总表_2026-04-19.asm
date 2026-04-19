; =====================================================================================
; SuperSkillWnd - 当前项目HOOK总表（2026-04-19）
; 路径：G:\code\c++\SuperSkillWnd\docs\当前项目HOOK总表_2026-04-19.asm
;
; 目的：
; 1. 用“能当教材看”的方式，整理当前项目所有活跃 HOOK / Patch 点。
; 2. 对每个点说明：
;    - 原地址 / 原职责
;    - 当前实现方式（InstallInlineHook / GenericInlineHook5 / 手写JMP / VTable Patch / NOP Patch）
;    - 为什么这样做
;    - 需要注意的栈、寄存器、copyLen、rel32 call、生命周期风险
; 3. 让新手直接看懂：这个项目到底在游戏哪里下手、为什么这么下手。
;
; 重要说明：
; - 本文件只整理“当前源码里真正启用的 HOOK / Patch”。
; - 不包含 *.bak 历史实验版。
; - 某些 D3D9 / D3D8 hook 是运行时动态取地址（vtable / thunk），不是固定 RVA。
; - 某些点已经明确“禁用”，这里也会写出来，避免下次误开。
;
; 核心源码入口：
; - G:\code\c++\SuperSkillWnd\src\runtime\dllmain_section_runtime_hooks.inl
; - G:\code\c++\SuperSkillWnd\src\hook\InlineHook.h
; - G:\code\c++\SuperSkillWnd\src\core\GameAddresses.h
; =====================================================================================

; =====================================================================================
; [A] HOOK 机制总览
; =====================================================================================

; 机制 A1：InstallInlineHook(targetAddr, hookFunc)
; -------------------------------------------------
; 适用：
; - 目标函数地址固定
; - 目标开头没有复杂 rel32 call/jmp 风险
; - 允许自动跟随 JMP 链后在真实入口写 5-byte JMP
;
; 做法：
; 1. FollowJmpChain() 找到真实入口
; 2. CalculateRelocatedByteCount() 算出至少 5 字节的完整指令长度
; 3. 拷贝原指令到 trampoline
; 4. 原入口覆写成 JMP hook
;
; 优点：
; - 调用简单
; - 适合大多数普通 __thiscall / __stdcall 入口
;
; 风险：
; - 依然是“原样 memcpy + 尾跳回”，不自动重写被搬走指令里的 rel32 call/jmp
; - 所以不是所有函数都安全

; 机制 A2：GenericInlineHook5(pTarget, myFunc, copyLen)
; ----------------------------------------------------
; 适用：
; - 我们已经手工确认 copyLen 安全
; - 或者要 hook 的不是固定地址，而是运行时 FollowJmpChain / vtable 取出的函数地址
;
; 做法：
; 1. 手工给 copyLen
; 2. VirtualAlloc 新 trampoline
; 3. memcpy 原始指令到 trampoline
; 4. trampoline 尾部跳回原函数
; 5. 原函数入口改写成 JMP myFunc
;
; 风险：
; - 这是“裸 memcpy trampoline”
; - 不会自动修 rel32 call/jmp
; - 所以 copyLen 不是越大越好，必须按指令边界和相对跳转来选

; 机制 A3：手写 callsite JMP Patch
; ----------------------------------
; 适用：
; - 不是 hook 整个函数，而是 hook 某个特定 callsite / prologue
; - 比如发包点、收包分发点、能力面板某个 pre-sub 点
;
; 做法：
; - VirtualProtect 改页属性
; - 直接把入口几个字节改成 JMP hkXXX
; - hkXXX 里自己保存寄存器 / 补发原逻辑 / 跳回 continue
;
; 优点：
; - 可以精准拦截“这一处调用点”
; - 不影响同一个函数的其他调用点
;
; 风险：
; - 需要自己完全搞懂栈布局和 continue 地址
; - 裸函数写错一个 ret / add esp 就会炸

; 机制 A4：VTable Patch
; ----------------------
; 适用：
; - D3D9 live device
; - D3D8 dummy device / live device
; - COM / interface 类对象
;
; 做法：
; - 改 vtable[index] = hookFunc
; - 保存原始函数指针
;
; 优点：
; - 不依赖固定代码段地址
; - 适合 wrapper / proxy / 系统 DLL
;
; 风险：
; - 必须明确 vtable index
; - 设备重建 / 对象重建后要重新 patch

; 机制 A5：Rel32 thunk patch（D3D8 专用）
; ----------------------------------------
; 适用：
; - 入口本身只是 “JMP 真函数” 的 rel32 thunk
; - 比如 d3d8 导出的某些 wrapper 入口
;
; 做法：
; - 不去 hook thunk 后面的真实函数
; - 直接把 thunk 的 JMP 目标改到我们的 hk
; - 原真实目标保存到 oD3D8Present / oD3D8Reset
;
; 目的：
; - 避免对 DLL 内部 thunk 做不必要的大面积 trampoline 拷贝

; 机制 A6：NOP Patch
; -------------------
; 适用：
; - 不需要转发逻辑
; - 只需要把某段上限限制逻辑直接抹掉
;
; 当前只用于：
; - speed/jump upper cap

; =====================================================================================
; [B] 固定地址 HOOK / Patch 总览（按功能分组）
; =====================================================================================

; -------------------------------------------------
; B1. 技能窗 / SuperSkill 主链
; -------------------------------------------------
; 0x009E17D0  SkillWndEx 子控件初始化
;   方法：InstallInlineHook
;   hk：hkSkillWndInitChildren
;   作用：
;   - 拿到 SkillWndEx this
;   - 原函数返回后补建超级按钮
;   - 建立整个 overlay / child 的起点

; 0x009DDB30  SkillWndEx 消息分发
;   方法：InstallInlineHook
;   hk：hkSkillWndMsgNaked
;   作用：
;   - 拦截 SUPER_BTN_ID
;   - 点击超级按钮时切换超级技能窗
;   - 和 WndProc fallback 共享节流，避免一次点击重复 toggle

; 0x009D95A0  SkillWndEx move 同步
;   方法：InstallInlineHook
;   hk：hkSkillWndMoveNaked
;   作用：
;   - SkillWnd 拖动时同步超级窗 / 按钮锚点

; 0x009E1770  SkillWndEx refresh
;   方法：InstallInlineHook
;   hk：hkSkillWndRefreshNaked
;   作用：
;   - SkillWnd 刷新时同步超级窗状态

; 0x009DEE30  SkillWndEx draw
;   方法：InstallInlineHook
;   hk：hkSkillWndDrawNaked
;   作用：
;   - 接入 SkillWnd 绘制节奏
;   - 给原生 child / 面板层补绘制机会

; 0x00BBC965  sub_BBC460 内部 post-B9F6E0 draw 点
;   方法：GenericInlineHook5
;   hk：hkPostB9F6E0DrawNaked
;   作用：
;   - 历史上用于在 dirty render 后补绘制
;   - 当前仍保留为独立 draw 观察点

; 0x009E14D0  SkillWndEx 析构
;   方法：InstallInlineHook
;   hk：hkSkillWndDtorNaked
;   作用：
;   - 关闭 SkillWnd 时回收 super child / overlay 状态

; 0x007DD67D  技能列表构建 filter 点（LABEL_42）
;   方法：GenericInlineHook5
;   hk：hkSkillListBuildFilterNaked
;   作用：
;   - 在技能加入 entries 前最后一刻过滤 / 注入
;   - 超级技能与原生列表联动的关键入口

; -------------------------------------------------
; B2. 网络封包链
; -------------------------------------------------
; 0x0043D94D  通用发包 callsite
;   方法：手写 callsite JMP patch
;   hk：hkSendPacketNaked
;   作用：
;   - 拦截 outgoing packet
;   - 调 SkillOverlayBridgeInspectOutgoingPacketMutable
;   - 做超级技能发包改写 / 观察 / quickslot 相关处理
;
;   教学版伪汇编：
;   0x0043D94D  E9 xx xx xx xx      ; JMP hkSendPacketNaked
;
;   hkSendPacketNaked:
;   pushad
;   ; 取 packetDataSlot / packetLenSlot / callerRetAddr
;   call hkSendPacketInspect
;   popad
;   call g_SendPacketOriginalCallTarget
;   jmp  oSendPacket

; 0x004D6A13  收包 opcode 分发入口
;   方法：
;   - 优先 direct prologue patch
;   - 如果入口不匹配，则 fallback GenericInlineHook5
;   hk：
;   - hkRecvPacketNaked
;   - hkRecvPacketNakedFallback
;   作用：
;   - 拦截 incoming packet
;   - 识别 give/cancel buff、virtual buff、自定义技能 reset preview
;   - 驱动独立BUFF overlay 状态机

; -------------------------------------------------
; B3. StatusBar / 右上角 BUFF 真实链
; -------------------------------------------------
; 0x009F4F00  StatusBar fixed-slot refresh A
;   方法：GenericInlineHook5
;   hk：hkStatusBarRefreshSlotsPrimaryNaked
;   作用：
;   - 观测 +0xAE8 / +0xB18 wrapper family
;   - 记录 observed statusBar / slot summary / native visible count

; 0x009F4C30  StatusBar fixed-slot refresh B
;   方法：GenericInlineHook5
;   hk：hkStatusBarRefreshSlotsSecondaryNaked
;   作用：
;   - 同上，辅助捕获 top-row 状态

; 0x009FCAE0  StatusBar transient cleanup
;   方法：GenericInlineHook5
;   hk：hkStatusBarCleanupTransientNaked
;   作用：
;   - 观测临时 child 链收尾

; 0x009FC110  StatusBar transient refresh / constructor root
;   方法：GenericInlineHook5
;   hk：hkStatusBarTransientRefreshNaked
;   作用：
;   - 观测真实 statusBar this
;   - 记录 +0xAD8 / +0xB2C / +0xB30 / +0xB5C

; 0x009FCC10  StatusBar transient dispatch
;   方法：GenericInlineHook5
;   hk：hkStatusBarTransientDispatchNaked
;   作用：
;   - 观测 transient 分发 case

; 0x009FCBD0  StatusBar transient toggle
;   方法：GenericInlineHook5
;   hk：hkStatusBarTransientToggleNaked
;   作用：
;   - 观测 popup on/off 相关链

; 右上角 BUFF fallback 语义占位（非新增 hook，属于 B3 观测链的消费逻辑）
;   路径：
;   - src/skill/skill_overlay_bridge.cpp
;   - src/ui/super_imgui_overlay.cpp
;   - src/ui/super_imgui_overlay_d3d8.cpp
;   当前规则（2026-04-19 晚）：
;   1) 语义槽按“每个 skill 1 个代表位（该 skill 最小 order）”建模。
;   2) fallback 语义行做紧凑化：保持顺序，但固定一技能一格，不保留跨 mask 洞位。
;   3) 目的：在 statusBar 实链缺失时，先消除 1301006 这类 case 触发的“右侧多 2~3 格”漂移。
;   4) native visible 注册增加 packetSkillId 合法性过滤（异常大值直接 suppress）。

; 0x009F5FE0  StatusBar aggregate refresh entry
;   当前状态：明确禁用，不 hook
;   原因：
;   - 前几字节里包含 rel32 call 9F4F00
;   - GenericInlineHook5 不会自动重定位 rel32 call
;   - 直接 hook 会把 trampoline 弄坏并导致崩溃

; -------------------------------------------------
; B4. Surface draw 观测链（黑幕/鼠标候选）
; -------------------------------------------------
; 0x00401C90  通用 surface draw image wrapper
;   方法：GenericInlineHook5
;   hk：hkSurfaceDrawImage
;   作用：
;   - 只观测，不篡改 draw 结果
;   - 捕捉：
;     1. 全屏 image draw 候选（用于黑幕 fade 学习）
;     2. 鼠标附近的小图 draw 候选（用于原生 cursor 状态学习）
;   - 当前增量：
;     1. 对 near-fullscreen draw 额外记录 VARIANT vt/raw[4]
;     2. 为下一轮确认黑幕真实 alpha 字段保留直接日志证据
;     3. bridge 侧 fade candidate 允许 near-fullscreen 学习（不再只接受严格 full-viewport）
;
;   关键原因：
;   - 用户要的黑幕和原鼠标都属于“游戏已经画出来的真实对象”
;   - 直接观测 draw 比猜状态更稳
;
; 0x005F3EC0  原生 cursor state setter
;   方法：GenericInlineHook5
;   hk：hkNativeCursorStateSetNaked
;   作用：
;   - 记录原生 cursor manager 当前状态号（this+625）
;   - 记录当前生效 cursor 资源句柄（this+606）
;   - 把状态同步到 overlay bridge，供 overlay 命中区域优先按原版状态类别复画
;
;   当前边界：
;   - 已闭环到真实 setter/state 链
;   - 但 overlay 资产只有 normal/hover/pressed/drag 四类
;   - 因此目前是“真实状态号 -> overlay 等价类别”的复画，不是 100% 原版全部 cursor 资源逐帧直出

; -------------------------------------------------
; B5. Local Independent Potential / 独立BUFF本地属性链
; -------------------------------------------------
; 0x00853B49  基础平属性潜能链入口
;   方法：手写 JMP patch
;   hk：hkLocalIndependentPotentialPrimaryFlatStatsNaked
;   作用：
;   - 截获原本 flat stat 合成逻辑
;   - 插入独立BUFF本地属性

; 0x00853E5A  百分比属性链入口
;   方法：手写 JMP patch
;   hk：hkLocalIndependentPotentialPrimaryPercentStatsNaked

; 0x00856879  扩展平属性链入口
;   方法：手写 JMP patch / 外部 stub 兼容
;   hk：hkLocalIndependentPotentialFlatStatsNaked

; 0x00AE0A70  技能等级提升显示链
; 0x008538C0  基础百分比显示链
; 0x00853E10  全属性/HP/MP百分比显示链
; 0x00853B00  基础平属性显示链
; 0x00856830  攻防/速度/暴伤等显示链
;   方法：GenericInlineHook5
;   作用：
;   - 拦截面板显示结果
;   - 把独立BUFF本地属性同步进 UI 显示

; -------------------------------------------------
; B6. AbilityRed 红字链（角色面板红字）
; -------------------------------------------------
; hash/container:
; 0x0049C9C0  hash lookup
; 0x0052FD80  hash insert/update
;
; aggregate:
; 0x00856BA0  extended aggregate
; 0x00856C60  master aggregate
;
; sibling diff helpers:
; 0x0082F780
; 0x0082F870
; 0x0082F960
; 0x0082FA50
;
; pre-sub style callsite hooks:
; 0x009F7241
; 0x009F7546
; 0x009F7893
; 0x009F7C7F
; 0x009F8048
; 0x009F82A8
;
; positive style select:
; 0x009F6E6F  attack range style
; 0x009F7A7C  critical rate style
; 0x009F8565  speed style
; 0x009F8696  jump style
;
; bake/write chain:
; 0x00857BB6
; 0x00857C29
; 0x00857C9C
; 0x00857D0F
; 0x008569C3
; 0x00856D57
; 0x0085725F
; 0x00857C3B
; 0x00858AED
; 0x00831A50
; 0x0083AF02
;
; final consumers:
; 0x0084C470
; 0x0084CA90
; 0x0084CBD0
;
; candidate / level / writes:
; 0x00AE0E60  display candidate
; 0x00AE6C21  display callsite
; 0x00AE43D5  level read point
; 0x0052FE14  skill write A
; 0x006226CE  skill write B
; 0x0049CA01  skill write C
; 0x00A4CA60  potential text display
;
; 作用总述：
; - 这整组 hook 不直接控制 BUFF 坐标
; - 它们负责“本地属性改了以后，角色面板红字/结果链也要一起看起来对”
; - 是独立BUFF“数值展示闭环”的关键

; -------------------------------------------------
; B7. 技能释放分类 / 表现链
; -------------------------------------------------
; 0x00B31349  classifier root
; 0x00B3144D  classifier branch
; 0x00B2F370  SkillWnd 双击释放大分支
; 0x00ABAF70  技能本地表现分发表现
;
; 0x007CE790 / 0x007D0000
;   方法：GenericInlineHook5
;   作用：
;   - 深层 native id allow/gate
;   - 让 custom skill 走 donor 行为同时保留自己的 skillId 语义

; 作用总述：
; - 这是“超级技能借 donor 原生行为”最核心的一条链

; -------------------------------------------------
; B8. 坐骑 / 飞行 / Soaring 链
; -------------------------------------------------
; 0x004069E0  mount action gate A
; 0x00406AB0  mount action gate B
; 0x007CF370  native flight skill map
; 0x007DC1B0  soaring gate
;
; 方法：
; - InstallInlineHook
;
; 作用：
; - 让自定义坐骑/飞行技能在原生 gate 下正常通过

; -------------------------------------------------
; B9. 技能等级 / 字形查找
; -------------------------------------------------
; 0x007DA7D0  base level lookup
; 0x007DBC50  current level lookup
; 0x5000E520  glyph lookup
;
; 作用：
; - skillId donor/custom 映射时，等级查询不能错
; - 字形/tooltip 路径也要知道最终展示的 skill/文字对象

; -------------------------------------------------
; B10. D3D9 / D3D8 图形 hook
; -------------------------------------------------
; D3D9:
; - Present：动态拿 live device vtable[17] 或直接 inline
; - EndScene：vtable[42] / inline
; - Reset：vtable[16]
; - ResetEx：vtable[132]
; - Direct3DCreate9：export 入口 inline
;
; D3D8:
; - Present：vtable[15] / rel32 thunk patch / inline fallback
; - Reset：vtable[14] / rel32 thunk patch / inline fallback
;
; 作用：
; - 驱动 DX9 / D3D8 两套 overlay 渲染
; - 设备丢失 / 重建时重建资源

; -------------------------------------------------
; B11. 速度/跳跃上限补丁
; -------------------------------------------------
; 0x00858D30  speed upper clamp
; 0x00858D49  jump upper compare
; 0x00858D4E  jump upper clamp
;
; 方法：
; - PatchNopsIfExpected
;
; 作用：
; - 抹掉客户端本地上限限制
; - 避免独立BUFF把 speed/jump 算进来后又被客户端硬截断

; =====================================================================================
; [C] 教学用伪汇编示例（按实现方式讲）
; =====================================================================================

; -------------------------------------------------
; C1. 手写 callsite JMP：发包点 0x0043D94D
; -------------------------------------------------
; 原始逻辑：
;   这里不是整个 send 函数入口，而是一个“call raw send”的调用点。
;   我们只想在发包前看一眼 packet，所以直接改这个 callsite 最稳。
;
; 改写后思路：
;   0x0043D94D  JMP hkSendPacketNaked
;
; hkSendPacketNaked:
;   pushad
;   ; 取：
;   ; [esp+36] -> packetDataSlot
;   ; [esp+40] -> packetLenSlot
;   ; [esp+32] -> callerRetAddr
;   call hkSendPacketInspect
;   popad
;   call g_SendPacketOriginalCallTarget
;   jmp  oSendPacket
;
; 为什么这样做：
; - packetData / packetLen 在 callsite 这里最容易拿
; - 不需要 hook 整个网络类
; - 可以最小侵入地做 packet rewrite

; -------------------------------------------------
; C2. 手写 recv prologue / fallback hook：0x004D6A13
; -------------------------------------------------
; 目标：
; - 在 opcode dispatch 入口处读到 incoming packet
;
; 直接 prologue 成功时：
;   0x004D6A13  JMP hkRecvPacketNaked
;
; hkRecvPacketNaked:
;   movzx eax, ax        ; opcode
;   lea   ecx, [eax-10h] ; 原逻辑保留
;   pushad
;   push 0
;   push eax             ; opcode
;   push esi             ; inPacket
;   call hkRecvPacketInspect
;   popad
;   cmp ecx, 0Ah
;   jmp oRecvPacket
;
; 如果入口不是预期字节：
; - 不硬 patch 原 9 字节
; - 改用 GenericInlineHook5 fallback 到 hkRecvPacketNakedFallback
;
; 为什么这样做：
; - MapleStory 某些版本/壳会把入口改成外部 stub
; - fallback 方案能保证“至少还能观察封包”

; -------------------------------------------------
; C3. GenericInlineHook5：StatusBar 0x009F4F00
; -------------------------------------------------
; 原理：
; 1. 复制原函数前 copyLen 字节到 trampoline
; 2. trampoline 末尾 JMP 回原函数 copyLen 之后
; 3. 原函数入口改成 JMP hkStatusBarRefreshSlotsPrimaryNaked
;
; hkStatusBarRefreshSlotsPrimaryNaked:
;   push ecx
;   call hkStatusBarRefreshSlotsPrimaryHandler
;   pop ecx
;   jmp oStatusBarRefreshSlotsPrimary
;
; 为什么：
; - 这里需要“先记录 statusBar this / slot summary，再继续原逻辑”
; - 这种 before-log-after-original 的场景，GenericInlineHook5 最合适

; -------------------------------------------------
; C4. 为什么 0x009F5FE0 明确禁用
; -------------------------------------------------
; 0x009F5FE0 开头：
;   push esi
;   mov  esi, ecx
;   call 0x009F4F00   ; rel32 CALL
;
; 如果把前 5~8 字节 memcpy 到 trampoline：
; - 这个 rel32 call 的位移会相对 trampoline 基址重新解释
; - 跳到完全错误的位置
; - 直接炸
;
; 结论：
; - 当前 InlineHook 引擎不做 rel32 重定位
; - 所以 9F5FE0 只能禁用，不能硬 hook

; -------------------------------------------------
; C5. VTable Patch：D3D9 live device
; -------------------------------------------------
; 目标：
; - 某些环境下 inline Present 抓不到
; - 直接 patch live device vtable 更稳
;
; 伪过程：
;   void** vtable = *(void***)pDevice;
;   original = vtable[17];
;   VirtualProtect(&vtable[17], sizeof(void*), PAGE_EXECUTE_READWRITE, ...);
;   vtable[17] = hkPresent;
;   VirtualProtect(...)
;
; 为什么：
; - D3D9Ex / wrapper / 第三方 d3d9.dll 时，export 入口不一定稳定
; - 但 live device vtable 永远是最终调用点

; -------------------------------------------------
; C6. Rel32 thunk patch：D3D8
; -------------------------------------------------
; 背景：
; - D3D8 某些入口只是一个 JMP thunk
; - 如果直接对 thunk 后的目标再做大 copyLen trampoline，风险更高
;
; 做法：
;   entry[0] = E9
;   *(entry+1) = hkD3D8Present - entry - 5
;   outOriginal = resolvedRealTarget
;
; 为什么：
; - 我们只需要把导向改到自己
; - 没必要复制一大段 thunk 逻辑

; -------------------------------------------------
; C7. NOP patch：速度/跳跃上限
; -------------------------------------------------
; PatchNopsIfExpected(address, expectedBytes, length, label)
;
; 设计原则：
; - 先 memcmp expected，确保补丁命中的是“我们认得的版本”
; - 只有字节完全相同才抹掉
;
; 为什么：
; - 这是最基本的版本保护
; - 防止换版本后把不相干代码 NOP 掉

; =====================================================================================
; [D] 当前最重要的风险提醒
; =====================================================================================

; 1. StatusBar 右上角真实链还没完全闭环
;    - 9F4F00 / 9F4C30 hook 已经装了
;    - 但 runtime 里 topVisible 常常还是 0x00，只看到 placeholder child
;    - 当前 fallback 已降级为“一技能一格”紧凑语义，优先避免空 2~3 格
;    - userlocal 切换已改为“同 netclient 且场景活跃时重绑不清空”，避免切图即丢状态
;    - 但只要 statusBar 指针长期拿不到，仍可能出现“从最右开始覆盖原生BUFF”

; 2. 401C90 观测链目前仍是“候选观测”
;    - 已经能装 hook
;    - 已额外补 near-fullscreen + raw VARIANT 日志
;    - bridge 已允许 near-fullscreen candidate 学习
;    - 但仍要靠运行日志确认是否稳定学到真实黑幕 imageObj / alpha 字段
;    - 所以不能宣称黑幕遮罩已经完成

; 3. 原生鼠标状态已从“候选 near-mouse draw”推进到“真实 setter/state 链”
;    - 已直接 hook 5F3EC0 记录当前状态号
;    - overlay 已优先按真实状态类别复画（panel/button/BUFF 命中区域）
;    - 但还不能说“已经 1:1 复画原版全部 cursor 资源”

; =====================================================================================
; [E] 文件索引
; =====================================================================================

; Inline hook engine:
;   G:\code\c++\SuperSkillWnd\src\hook\InlineHook.h
;
; Hook installation main file:
;   G:\code\c++\SuperSkillWnd\src\runtime\dllmain_section_runtime_hooks.inl
;
; Fixed addresses:
;   G:\code\c++\SuperSkillWnd\src\core\GameAddresses.h
;
; Current task knowledge:
;   G:\code\c++\SuperSkillWnd\.claude\rules\independent_buff_overlay_handoff_2026_04_18_night_statusbar_real_chain.md
;
; =====================================================================================
