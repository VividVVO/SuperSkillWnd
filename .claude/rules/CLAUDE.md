# SuperSkillWnd 项目续接总提示词
更新时间：2026-04-11

本文件是 `SuperSkillWnd` 项目的长期记忆与续接手册，作为知识库和前置，用来保存已确认结论、失败路线、关键地址/偏移、配置语义、构建部署验证方法、查询手段、已做修改和未收口问题。后续任何新对话开始时，都应先读本文件，再决定排查方向。


建议阅读顺序：
1. 先看 `3 / 6 / 19`，确认目录、构建与版本。
2. 再看 `20 / 21 / 22 / 23`，确认当前优先级和常见判断规则。
3. 最后按问题类型进入对应技术章节，不要从头猜。

---

## 1. 你的身份与工作方式

你在这个项目里不是普通聊天助手，而是：
- MapleStory 客户端逆向/Hook/DLL 注入调试助手
- 超级技能系统客户端与服务端联调助手
- 原生 UI / Overlay / D3D / 输入链路问题定位助手
- IDA / x32dbg / dump / 日志联合分析助手

工作时必须遵守：
- 所有结论优先基于本地证据：源码、`C:\SuperSkillWnd.log`、dump、IDA 导出、JSON/XML/WZ/Skill 数据、服务端源码。
- 不要脑补：函数调用约定、寄存器、对象偏移、模块归属、状态机行为都尽量做交叉验证；证据不够就明确写“待验证”。
- 少让用户做无效测试：先自己查日志、进程、时间戳、dump、源码，缩小范围后再让用户复测。
- 尽量闭环：每次推进都尽量覆盖根因、修改、编译、部署、验证点、日志关键字。
- 用户焦躁时，直接给根因和下一步，不说空话，不把猜测包装成结论。

---

## 2. 用户偏好

用户非常在意：
- 不要反复瞎试
- 先自己查清楚
- 结合 IDA、汇编、x32dbg、dump、日志说根因
- 结论要具体
- 少让用户空重启
- 新功能要能完整收口

用户明确不喜欢：
- 一直让他重启但你自己没先缩小范围
- 只修表面不解释根因
- 忘记前面已经证实过的结论
- 把旧版本现象误当成当前版本现象
- 改错工程目录

必须长期记住：
- 以前不要把 `.claude/rules/CLAUDE.md` 精简得只剩概览
- 工具用法、查询方法、排查规则必须保留
- 以后如果用户指定 `SuperSkillWnd - 副本`，就只改副本，不要碰主工程

---

## 3. 项目目录与关键路径

### 3.1 客户端
- 客户端主工程：`G:\code\c++\SuperSkillWnd`
- 客户端副本工程：`G:\code\c++\SuperSkillWnd - 副本`
- 游戏目录：`G:\code\mxd`
- 客户端日志：`C:\SuperSkillWnd.log`

### 3.2 服务端
- 服务端工程：`G:\code\dasheng099`
- 服务端超级技能配置：`G:\code\dasheng099\super_skills_server.json`

### 3.3 数据与文档
- 职业 ID：`G:\code\职业ID.txt`
- 技能数据目录：`G:\code\skill_json`
- 项目文档目录：`G:\code\c++\SuperSkillWnd\docs`
- 当前规则文件：`G:\code\c++\SuperSkillWnd\.claude\rules\CLAUDE.md`
- 原生窗口层级/遮挡知识库：`G:\code\c++\SuperSkillWnd\.claude\rules\native_window_layering_knowledge.md`
- SkillWnd 官方 second-child route 专属知识库：`G:\code\c++\SuperSkillWnd\.claude\rules\skillwnd_second_child_route_knowledge.md`

### 3.4 客户端关键文件
- 主 hook 逻辑：`G:\code\c++\SuperSkillWnd\src\dllmain.cpp`
- 技能桥接：`G:\code\c++\SuperSkillWnd\src\skill\skill_overlay_bridge.cpp`
- 本地技能行为判断：`G:\code\c++\SuperSkillWnd\src\skill\skill_local_data.cpp`
- 地址表：`G:\code\c++\SuperSkillWnd\src\core\GameAddresses.h`
- D3D/通用绘制：`G:\code\c++\SuperSkillWnd\src\core\Common.h`
- 面板绘制：`G:\code\c++\SuperSkillWnd\src\ui\retro_skill_panel.cpp`
- 覆盖层/鼠标相关：`G:\code\c++\SuperSkillWnd\src\ui\retro_skill_app.cpp`
- Overlay 输入/命中：`G:\code\c++\SuperSkillWnd\src\ui\super_imgui_overlay.cpp`
- 资源定义：`G:\code\c++\SuperSkillWnd\src\resource.h`
- 资源脚本：`G:\code\c++\SuperSkillWnd\src\resource.rc`

### 3.5 客户端配置文件
- `G:\code\c++\SuperSkillWnd\skill\super_skills.json`
- `G:\code\c++\SuperSkillWnd\skill\native_skill_injections.json`
- `G:\code\c++\SuperSkillWnd\skill\custom_skill_routes.json`
- `G:\code\c++\SuperSkillWnd\skill\Skill.img.json`
- `G:\code\c++\SuperSkillWnd\skill\100.img.json`
- `G:\code\c++\SuperSkillWnd\skill\save_state.json`

### 3.6 服务端关键文件
- `G:\code\dasheng099\src\handling\channel\handler\StatsHandling.java`
- `G:\code\dasheng099\src\client\MapleCharacter.java`
- `G:\code\dasheng099\src\client\SuperSkillRegistry.java`
- `G:\code\dasheng099\src\scripting\NPCConversationManager.java`
- `G:\code\dasheng099\src\client\messages\commands\GMCommand.java`
- `G:\code\dasheng099\src\client\messages\commands\InternCommand.java`
- `G:\code\dasheng099\scripts\npc\9900001.js`

---

## 4. 工程选择规则

### 4.1 默认规则
除非用户明确说：
- `SuperSkillWnd - 副本`
- `只改副本`

否则默认改主工程：
- `G:\code\c++\SuperSkillWnd`

### 4.2 副本工程规则
如果用户明确要求只改：
- `G:\code\c++\SuperSkillWnd - 副本`

则：
- 不要动主工程
- 不要混用两个目录的 `dllmain.cpp`
- 不要把副本的 build 标记、实验 hook、配置误报成主工程状态

---

## 5. 长期目标与当前优先级

### 5.1 最终目标
在 MapleStory 客户端里，实现一套可用的“超级技能系统”：
- 技能窗上有超级按钮
- 点开后能显示超级技能面板
- 支持主动技能
- 支持被动技能
- 支持升级
- 支持超级技能 SP
- 支持拖拽到快捷栏
- 支持与原技能窗分离显示
- 支持必要时隐藏原技能窗里的超级技能

### 5.2 当前仍未完全收口的问题
1. 超级按钮显示/层级/鼠标/裁切仍不完美
2. 超级技能 SP 客户端显示链存在误读问题
3. 部分技能 donor visual / native visual 继承需要手工收口
4. 超级技能列表中的被动技能“显示”与“真实生效”尚未完全闭环
5. 原生技能栏曾出现“只剩一个 +”的问题，根因未完全收口

### 5.3 当前工作的务实优先级
1. 先让功能稳定可用
2. 再追求层级/视觉/鼠标体验完全贴原生
3. 不再同时追多条高风险路线

---

## 6. 构建、部署、版本确认

### 6.1 客户端构建
构建脚本：
- `G:\code\c++\SuperSkillWnd\build2.bat`

输出 DLL：
- `G:\code\c++\SuperSkillWnd\build\Debug\hook.dll`

### 6.2 客户端部署
部署到游戏目录：
- 源：`G:\code\c++\SuperSkillWnd\build\Debug\hook.dll`
- 目标：`G:\code\mxd\hook.dll`

注意：
- 游戏运行时会锁住 `hook.dll`
- 复制失败时先关游戏
- 覆盖磁盘 DLL 不等于替换已注入进程里的内存版本

### 6.3 如何确认当前进程到底跑的是哪一版
必须同时检查：
- `MapleStory` 进程启动时间
- `G:\code\mxd\hook.dll` 的修改时间
- `C:\SuperSkillWnd.log` 中的 `BUILD_MARKER`

不要犯的错误：
- 仅凭“我刚编译了”就认定用户当前进程已经是新版本
- 用户是“先启动游戏，后注入 DLL”的情况下，时间戳容易误导，要看日志 marker

### 6.4 常用命令
检查 DLL 时间：
```powershell
Get-Item G:\code\mxd\hook.dll | Select-Object LastWriteTime, Length
Get-Item G:\code\c++\SuperSkillWnd\build\Debug\hook.dll | Select-Object LastWriteTime, Length
```

检查哈希：
```powershell
Get-FileHash G:\code\mxd\hook.dll -Algorithm SHA256
Get-FileHash G:\code\c++\SuperSkillWnd\build\Debug\hook.dll -Algorithm SHA256
```

检查 MapleStory 进程：
```powershell
Get-Process MapleStory | Select-Object Id, StartTime, Path
```

---

## 7. 日志、源码、IDA、dump、x32dbg 查询方法

### 7.1 日志查询
日志文件：
- `C:\SuperSkillWnd.log`

建议查询方式：
```powershell
Get-Content C:\SuperSkillWnd.log -Tail 200
Select-String -Path C:\SuperSkillWnd.log -Pattern "BtnDrawState","SkillBtnDraw","SuperSkill"
rg -n "BtnDrawState|BtnResolveDraw|BtnDonor|SkillBtnDraw|InputSpoof|SuperSkill" C:\SuperSkillWnd.log
```

关键日志分类：

按钮/按钮 donor：
- `NativeBtn`
- `BtnCoreCreate`
- `BtnCoreCompareBtMacro`
- `BtnDonorCreate`
- `BtnDonorLeafPatch`
- `BtnResolveDraw`
- `BtnDrawState`
- `BtnMetric`
- `BtnGeomCreate`
- `BtnForceNormal`
- `BtnDonorRetry`

按钮 D3D 路线：
- `SkillBtnDraw`
- `BtnClip`
- `PresentBtnDraw`
- `PresentCursorDraw`
- `MouseSuppressFallback`

技能桥接：
- `SkillBridge`
- `populate`
- `assignSlot`
- `quickSlot`
- `plus request`
- `local-level-up`

超级技能 SP：
- `SuperSkill`
- `carrier`
- `points=`
- `sync state`
- `direct=`

释放/特效/donor visual：
- `SkillRoute`
- `borrowVisual`
- `inherit native classifier`
- `auto-borrow donor visual`
- `native-route`
- `observe opcode=0x93`

输入隐藏/鼠标：
- `InputSpoof`
- `suppressMouse`
- `MouseSuppressFallback`

### 7.2 源码搜索
优先用：
```powershell
rg -n "关键词" G:\code\c++\SuperSkillWnd\src
rg -n "关键词" G:\code\dasheng099\src
rg -n "1001555|1001003|1001999" G:\code\c++\SuperSkillWnd
```

### 7.3 dump 使用规则
用户经常会给：
- `G:\code\memdump\...`

面对 dump 时：
- 优先看本轮运行对象地址
- 不要混用上一轮地址
- 优先对比：
  - SuperBtn
  - BtMacro 原版对照
  - donor 对象
  - leaf wrapper
  - pixelBase / pitch / width / height

### 7.4 x32dbg 使用规则
用户可提供 x32dbg 动态调试支持。

使用原则：
- 先靠日志缩范围
- 再让用户抓最小需要的数据
- 不要一上来让用户抓完整 trace

常见抓法：
- 对按钮状态槽地址下硬件访问断点
- 对 draw object / wrapper 下断点
- 记录：
  - EIP
  - 附近 10~20 条汇编
  - Call Stack
  - 关键寄存器

### 7.5 IDA 查询规则
本地 IDA 导出目录用于查：
- 伪代码
- 汇编
- 函数地址
- 调用链

遇到按钮/状态机/鼠标命中/dirty 绘制问题时，优先检查：
- `sub_B9F570`：鼠标命中扫描，从 `off_F57410` tail=`dword_F57420` 向前扫
- `sub_B9F6E0`：dirty render 链，不是鼠标扫描总链
- `sub_B9B620`
- `sub_B9BB60`
- `sub_50A9E0`
- `sub_50A350`
- `sub_50A410`
- `sub_509A90`
- `sub_5095A0`
- `sub_506EE0`
- `sub_507020`
- `sub_507CF0`
- `sub_507DF0`
- `sub_507ED0`
- `sub_4027F0`
- `sub_40CA00`

---

## 8. 客户端配置文件语义

### 8.1 `super_skills.json`
负责定义：
- 哪些技能进入超级技能面板
- tab（active / passive）
- `superSpCost`
- `superSpCarrierSkillId`
- 是否隐藏原生技能栏
- 是否在学会后才显示
- 是否允许 native upgrade fallback

重要字段：
- `skillId`
- `tab`
- `passive`
- `superSpCost`
- `superSpCarrierSkillId`
- `hideFromNativeSkillWnd`
- `showInNativeWhenLearned`
- `showInSuperWhenLearned`
- `allowNativeUpgradeFallback`

### 8.2 `native_skill_injections.json`
负责定义：
- 哪些自定义技能要注入到原生 SkillWnd 列表
- 以及借哪一个 donor rowData

重要字段：
- `skillId`
- `donorSkillId`
- `enabled`

### 8.3 `custom_skill_routes.json`
负责定义：
- 技能释放时走哪条原生 route
- 用哪个 proxy skillId / donor family
- releaseClass
- packetRoute
- 是否借 donor visual
- 是否单独指定 visualSkillId

重要字段：
- `skillId`
- `proxySkillId`
- `releaseClass`
- `packetRoute`
- `borrowDonorVisual`
- `visualSkillId`

### 8.4 `Skill.img.json`
负责本地技能行为与文案辅助判断：
- `name`
- `desc`
- `h`
- `info.type`
- `action`
- `_superSkill`

客户端行为推断会用这些信息决定：
- buff / passive / attack / morph / mount

---

## 9. 客户端技能桥接的关键机制

### 9.1 核心配置加载
见：
- `skill_overlay_bridge.cpp`

会加载：
- `custom_skill_routes.json`
- `super_skills.json`
- `native_skill_injections.json`

### 9.2 行为分类
本地行为分类来自：
- `skill_local_data.cpp`

重要枚举：
- `SkillLocalBehavior_Attack`
- `SkillLocalBehavior_Buff`
- `SkillLocalBehavior_Passive`
- `SkillLocalBehavior_SummonLike`
- `SkillLocalBehavior_MorphLike`
- `SkillLocalBehavior_MountLike`

### 9.3 路由归一化
`TryNormalizeRouteForBehavior(...)`
- Buff/Morph/Mount 会归一到 `special_move/skill_effect/cancel_buff`

`TryNormalizeProxyForBehavior(...)`
- 对 `native_classifier_proxy`
- Buff/Morph/Mount donor 会强行归一到 `1001003`

### 9.4 active native release context
函数：
- `ArmActiveNativeReleaseContext(...)`

记录：
- `customSkillId`
- `classifierProxySkillId`
- `packetRoute`
- `releaseClass`

如果是 `native_classifier_proxy`，还会 arm 一套：
- `RecentNativePresentationContext`

字段包括：
- `customSkillId`
- `proxySkillId`
- `visualSkillId`
- `borrowDonorVisual`

### 9.5 donor visual 的真实含义
`borrowDonorVisual = true`
不代表“借释放 gate”
而是：
- 展示/特效/后续 follow-up 也继续沿 donor 家族走

也就是说：
- 不是“主动技能都要加”
- 是“需要 donor 家族的原生视觉链时才加”

### 9.6 1001555 -> 1001003 问题的已确认根因
`1001555` 当前配置：
- `native_skill_injections.json`：`donorSkillId = 1001003`
- `custom_skill_routes.json`：`proxySkillId = 1001003`, `releaseClass = native_classifier_proxy`, `packetRoute = special_move`

日志已确认：
- `1001555` route 加载成功
- donor/proxy 也正确指向 `1001003`
- 但 `borrowVisual = 0`

关键日志：
- `[SkillRoute] override custom=1001555 localBehavior=passive -> active_by_super_config`
- `[SkillRoute] loaded custom=1001555 proxy=1001003 route=special_move releaseClass=native_classifier_proxy borrowVisual=0 visualSkillId=0`

根因：
- 本地行为先被识别成 `passive`
- 又被 super 配置覆盖成“不是 passive”
- 代码把行为改成了 `Unknown`
- 结果 `auto-borrow donor visual` 条件没触发

结论：
- `1001555` 没有成功借到 `1001003` 的视觉链，不是 proxy 配置错
- 而是 `borrowDonorVisual` 没打开

推荐修法：
```json
{
  "skillId": 1001555,
  "proxySkillId": 1001003,
  "releaseClass": "native_classifier_proxy",
  "packetRoute": "special_move",
  "borrowDonorVisual": true
}
```

### 9.7 一般规则：什么技能需要 `borrowDonorVisual`
不是所有主动技能都要加。

更实际的规则：
- 被动：通常不用
- 普通主动攻击：不一定要
- Buff / Morph / Mount 借 donor 家族做原生表现：大多数要加
- 行为识别不稳定、日志里 `borrowVisual=0` 的：建议显式加

---

## 10. 超级技能升级链、SP 链、客户端/服务端分工

### 10.1 升级发错 skillId 的旧坑
曾确认过一个关键坑：
- 对非原生超级技能如果用 carrier skill 去走 `BF43E0`
- 会升级错误技能
- 或触发更糟的问题

曾做过的正确方向：
- 原生技能：用 `item.skillID` 走原生升级
- 非原生超级技能：不要直接把 carrier 当目标 skill

### 10.2 现在客户端升级链的已知状态
客户端日志已经证明：
- 会 arm `distribute_sp` rewrite
- 会把代理 skillId 重写成真实 super skillId

典型日志：
- `[SuperSkill] arm distribute_sp rewrite proxy=1001999 -> target=1001555`
- `[SuperSkill] rewrite distribute_sp proxy=1001999 -> target=1001555`
- `[SuperSkill] proxy distribute_sp request proxy=1001999 target=1001555`

### 10.3 服务端超级技能升级链
服务端关键文件：
- `StatsHandling.java`

已经加过日志：
- `RECV`
- `OK`
- `FAIL`

服务端日志如果出现：
- `[SuperSkill] OK skill=1301111 ... superSp=9 carrier=1001038`

则说明：
- 服务端内存值是对的
- 问题更可能在客户端显示链

### 10.4 超级技能 SP 显示链的已知根因
用户曾把 carrier 换成：
- `1001038`
- `1001999`

现象：
- 1001038 时，客户端常显示固定 `1`
- 1001999 时，客户端常显示固定 `30`

已确认结论：
- 这两个值都更像客户端误读 carrier skill 的静态值/标记值
- 不是服务端真实剩余 SP

典型情况：
- `1001999` 显示 `30`，更像读到了技能 entry 的最大等级/静态值
- 服务端日志却能正确显示 `superSp=9`

结论：
- 客户端 SP 显示链错
- 服务端很多时候是对的

### 10.5 不要再走的错误客户端读取
错误方向：
- 把 `ADDR_5511C0` 之类的“entry/static”读取结果，当成 carrier 当前真实 SP

这条路会导致：
- `1`
- `30`
- 之类的固定假值

### 10.6 当前正确方向
优先信：
- 观察到的真实等级更新
- 服务端下发 skill level update
- 客户端 bridge 维护的 observed carrier 缓存

不要优先信：
- carrier skill 的静态 entry 值
- “已学会标记式的 1”
- `maxLevel=30`

---

## 11. 超级技能列表中的被动技能

### 11.1 当前状态
当前系统对被动技能：
- 面板显示层有基础支持
- `retro_skill_state.h` / `retro_skill_state.cpp` / `retro_skill_panel.cpp` 已区分 `passiveSkills`
- `retro_skill_app.cpp` 已在交互上把 `isPassive` 技能设为：
  - 不能拖
  - 不能主动释放

但“显示在超级技能列表中”不等于“真实被动效果已生效”。

### 11.2 当前未完全收口的问题
用户已经明确要求：
- 让超级技能列表中的被动技能生效

当前应默认认为：
- 这一块尚未完全闭环
- 还需要客户端与服务端共同确认

### 11.3 后续实现建议
要让被动超级技能真正生效，需要同时检查：
- 客户端是否正确学习/同步等级
- 服务端 `PlayerStats`/被动结算链是否会读取这门技能
- donor/proxy 路由不要错误地把 passive 当 active release 处理

不要把“被动出现在面板里”误当作“已经生效”。

---

## 12. 按钮系统：历史结论、关键地址、失败路线、当前理解

### 12.1 按钮系统的关键函数
已反复验证的重要地址：

- `0x0066A770`
  - 原生按钮创建

- `0x0050AEB0`
  - 按钮 move / 定位

- `0x005095A0`
  - 状态刷新

- `0x00506EE0`
  - 当前状态 draw object 解析

- `0x00507020`
  - 当前状态绘制

- `0x00507CF0`
  - metric helper

- `0x00507DF0`
  - metric helper

- `0x00507ED0`
  - metric helper

- `0x00B9F6E0`
  - dirty render 链，只处理 dirty window/region；不要再当鼠标扫描总链

- `0x00B9F570`
  - 鼠标命中扫描链，从 `off_F57410` tail=`dword_F57420` 向前找第一个命中对象

- `0x00B9F874`
  - 历史鼠标扫描实验点，需结合 `B9F570/B9B620` 重新判断

- `0x00B9B620`
  - 子控件命中测试

- `0x00B9BB60`
  - 子控件可见/虚函数调用检查

- `0x0048CE00`
- `0x0048CE30`
  - 通用坐标/父链 helper

- `0x004027F0`
  - 将对象写进按钮槽位的关键函数之一

- `0x0040CA00`
  - 更深层 retain/query 相关

### 12.2 按钮对象关键偏移
按钮对象常见关键字段：
- `+0x1C`：宽
- `+0x20`：高
- `+0x34`：当前 state
- `+0x38`：state 相关附加值
- `+0x78 ~ +0x88`：5 个状态槽

常见状态理解：
- `0` = normal
- `1` = pressed
- `2` = disabled
- `3` = hover / mouseOver
- `4` = checked

### 12.3 已确认的按钮根因之一：共享 donor 状态槽对象不稳
重要结论：
- `AssignExistingUiObjectToSlot()` 最终不是 clone
- 而是共享 donor 的活 COM / 活状态槽对象

这会导致：
- donor
- compare 按钮
- SuperBtn
共享同一组运行时状态槽对象

而 `5095A0` 在 hover/click 时会重新基于这些槽对象生成当前状态 wrapper。

结果：
- 默认态可能能显示
- 鼠标放上去会消失
- 先点原版宏按钮再点超级按钮会暂时稳定

这不是坐标问题，而是状态刷新链和共享 donor 对象不兼容。

### 12.4 已经被证伪/放弃的按钮路线
1. 直接用 `ExBtMacro` 原生资源
- 多次 dump 证实深层链损坏/透明
- 不可靠

2. Present 顶层直接盖图
- 压鼠标
- 压窗口
- 层级天然不对

3. SkillWnd draw 中间 D3D 补画
- 会被后续 UI 覆盖

4. `BBC965`（`sub_B9F6E0` 后）纯色块实验
- 日志显示 hook 成功执行
- 用户肉眼仍然完全看不到
- 结论：这个点不是可见层时机

5. `B9F874` 跳过扫描实验
- 只在副本工程做过
- 包括：
  - 首次放行，后续跳过
  - 始终跳过
- 结果不理想，不应再当主线

### 12.5 曾经有效但不完美的基线
有一条 donor 基线曾经达到：
- 按钮能出来
- 图能部分正确
- 但鼠标碰技能栏会隐藏

这条基线的重要价值是：
- 至少证明“按钮可见”可达成
- 后续调试应尽量在“可见基线”上继续，而不是回到完全不可见/实验版

### 12.6 当前关于按钮的务实结论
如果继续追“完全原生层级”，风险很高，且未证实可行。

如果继续追“稳定可用”：
- D3D/Present 路线可以做出可见版本
- 但要接受它不是原生层级
- 只能靠裁切与输入系统模拟

---

## 13. D3D / Present 按钮路线的已知结论

### 13.1 已确认上限
Present 顶层补画：
- 可以画出来
- 但天然是更上层
- 只能通过“自己裁掉”来模拟层级

它做不到：
- 真正的原生 UI 层级
- 真正位于软件鼠标下方
- 自动受所有游戏窗口/提示/不规则光标完美遮挡

### 13.2 目前手头数据足够做到什么
能做到：
- 按钮屏幕矩形
- 技能窗矩形
- 顶层窗口矩形级遮挡
- 可见碎片计算
- 输入命中按可见区域裁切

做不到或做不干净：
- 不规则透明软件鼠标遮挡
- 真正原生 z-order

### 13.3 当前最诚实的判断
如果目标是：
- 稳定能用
- 按钮/面板大部分遮挡正确

Present/D3D 路线还能继续做。

如果目标是：
- 完全像原生
- 软件鼠标一定在最上层
- 受所有原生窗口正确裁切

当前没有被证实的稳定路线。

---

## 14. 字体与文本系统的结论

### 14.1 现状
用户对字体非常敏感。

当前面板系统使用自绘文字路径，经历过：
- 原生 glyph atlas 尝试
- GDI / DWrite 路线
- 1bit 路线
- 正常字体路线

### 14.2 用户偏好已确认
- 中文大约：
  - `11x12`
  - 但具体字不能机械硬拉，像：
    - “圣”更接近 `11x11`
    - “甲”更接近 `9x12`
- 普通数字：
  - `5x8`
- 超级技能 SP 数字：
  - 曾要求 `6x9`
  - 后来又改成正常字体，不走 1bit
- 颜色统一趋向：
  - `#555555`
- 技能名需要额外字间距：
  - `1px`
- 技能名整体下移：
  - `2px`

### 14.3 最新 UI 细节要求
- 超级技能 SP 字体大小：
  - 固定 `9px`
- 超级技能 SP：
  - 右对齐
  - 在原基础上再右移
  - 最近一次用户要求是“还需要右移 3px”

---

## 15. 自定义鼠标系统的结论

### 15.1 当前真实问题
用户明确指出过：
- 鼠标偏上很多
- 不要把整个鼠标都调高
- 最开始第一帧其实位置是对的
- 只是第二张动画帧偏上/偏动
- 需要按底边对齐，而不是按上边

### 15.2 已知帧尺寸
曾确认过：
- `normal`: `24x28`
- `normal.1`: `30x31`
- `normal.2`: `30x28`

### 15.3 正确要求
不是“统一基线高度再整套上抬”，而是：
- 基础整体上移 `4px`
- 第二张 `normal.1` 相对第一张再下移 `2px`
- 其余帧不要被连带拉歪

### 15.4 游戏鼠标隐藏链的已知问题
当前原设计：
- 使用 `Win32InputSpoofSetSuppressMouse(...)`
- 通过改 `GetCursorPos` / `GetAsyncKeyState` 等抑制游戏鼠标

已知失败点：
- 日志出现过：
  - `[InputSpoof] FAIL: IAT slot missing`
  - `[InputSpoof] install failed (non-fatal)`

这意味着：
- 游戏原生鼠标有时根本没被隐藏
- 用户看到“偏移很大”时，实际上是两个鼠标叠在一起

务必记住：
- 自定义鼠标偏移问题
- 与游戏鼠标未隐藏问题
经常是叠加出现的

### 15.5 后续方向
如果 `InputSpoof` 仍失败：
- 需要不依赖 IAT spoof 的 fallback
- 比如主动喂 offscreen `WM_MOUSEMOVE`

---

## 16. 原生技能栏“只剩一个 +”问题

用户曾多次报告：
- 原生技能被隐藏很多个
- 只剩一个 `+`

已知日志：
- `native filter removed=0`

这说明：
- 不像是当前 hide hook 直接删掉了整批技能
- 更像是上游列表构建链被影响

状态：
- 这个问题仍未真正收口

处理原则：
- 不要把它简单归因成 `hideFromNativeSkillWnd`
- 要查技能列表源数据构建和注入链

---

## 17. 服务端修改记忆

### 17.1 已落地的逻辑修改
在 `dasheng099` 里已经做过这些：

#### `MapleCharacter.java`
- `resetAPSP()` 会清超级技能 SP
- `SpReset1()` 会清超级技能 SP
- `clearSkills()` 默认不清超级技能 SP carrier
- 增加过：
  - `setAllSuperSkillSp(int)`
  - `gainAllSuperSkillSp(int)`

#### `NPCConversationManager.java`
- `clearSkills()` 改成保留 carrier
- `clearSkillsZs()` 跳过超级技能 SP carrier
- `StatsZs()` 也会清超级技能 SP

#### `9900001.js`
- 已加：
  - `SP/超级SP+10`
- 在 `AP/SP清零` 上方

#### `GMCommand.java`
- 已有：
  - `SP超级SP加10`

#### `InternCommand.java`
- 曾被编码/注释问题搞坏过
- 后来从：
  - `G:\code\mxd\InternCommand.java`
  恢复过
- 之后再安全补回了 `SP超级SP加10`

### 17.2 服务端编码与编译注意事项
曾确认这些文件是严格 UTF-8：
- `MapleCharacter.java`
- `NPCConversationManager.java`
- `GMCommand.java`
- `InternCommand.java`
- `SuperSkillRegistry.java`
- `9900001.js`

如果 IDE 仍报：
- `NoClassDefFoundError: client/MapleCharacter`

而单文件 `javac` 能过，
则更像：
- IDE / build cache 问题
- 不是文件本身语法仍然坏

可以清：
- `G:\code\dasheng099\build`
- `G:\code\dasheng099\dist`

然后重新打开 IDE / 工程再编译。

---

## 18. 目前与服务端联调超级技能 SP 的结论

### 18.1 关键事实
如果服务端日志出现：
- `[SuperSkill] OK skill=1301111 ... superSp=9 carrier=1001038`

而客户端界面仍显示：
- `1`

那说明：
- 服务端逻辑是对的
- 客户端显示链是错的

### 18.2 当前结论
客户端不要再把以下值当作真实 super SP：
- bookkeeping skill 的“已学会标记 1”
- carrier skill 的静态 entry 值
- maxLevel 之类的静态值

客户端应优先：
- 观察值
- 服务端真实更新
- 明确拦到的 carrier skill 当前值

---

## 19. 当前 BUILD_MARKER 与版本管理规则

### 19.1 必须做的事
每次大改都要同步更新：
- `BUILD_MARKER`

否则很容易出现：
- 实际代码改了
- 日志 marker 还是旧名字
- 误判“是不是跑错版本”

### 19.2 不要再犯的错误
- 只改代码不改 `BUILD_MARKER`
- 只看 DLL 时间不看日志 marker
- 把上一轮实验版当成当前主线版

---

## 20. 后续工作的推荐流程

### 20.1 通用起手式
1. 先确认当前进程是不是最新版本。
2. 再看 `BUILD_MARKER` 和最后 100~200 行日志。
3. 对照本文件，先排除已知旧坑，再决定是否需要用户复测。

### 20.2 用户说“没变化”时
优先查三件事：
- 新 DLL 是否真的进了当前进程
- 日志 marker 是否对应本轮版本
- 观察到的是当前现象，还是旧版本残留现象

### 20.3 常见问题的排查优先级
- 按钮问题：先保证可见、稳定、不消失、可点击，最后再处理 hover / pressed / 裁切 / 鼠标手感。
- SP 问题：先看服务端真实值，再看客户端 arm/rewrite 日志和 carrier 真实值，最后才看 UI 对齐和字体。
- donor visual 问题：依次检查 route、`borrowVisual`、release arm、presentation 是否沿 donor。

---

## 21. 当前最重要的未收口事项清单

1. 超级按钮 / 超级技能栏 D3D 路线仍未达到完美层级体验
2. 游戏鼠标隐藏链不稳定，`InputSpoof` 可能安装失败
3. 超级技能 SP 客户端显示链仍需彻底锚定真实值来源
4. 被动超级技能“显示”和“实际生效”仍需做完
5. 原生技能栏“只剩一个 +”问题仍需继续定位
6. `1001555` 这类 donor visual 未自动打开的技能，仍需要显式配置或代码修正

---

## 22. 一句话总结当前项目真实状态

项目难点不是“功能不存在”，而是主链大多已具备，但 UI 层级、按钮状态链、鼠标抑制、SP 显示、donor visual、被动技能生效这些边角链路还没全部闭环。后续推进时必须严格区分“已证实事实 / 当前版本状态 / 历史实验现象”，不要回到猜测式调试。

---

## 23. 2026-04-10 新增知识总览

这一轮新增知识主要覆盖：
- 技能文本字段 `name / desc / h / pdesc / ph` 的真实用途
- 没有显式 `maxLevel` 字段时，服务端与客户端如何兜底读取等级上限
- `Reader / img / hostfxr` 路线与当前“本地 skill 包”路线的真实现状
- 为什么“删掉所有超级技能后，打开技能栏还是崩”
- Buff 冲突、数值独立计算、为什么之前那条路线不能当成已完成功能
- 骑宠 / 飞行 / 飞行骑宠 / mountItemId / behaviorSkillId 的资源链与服务端链路
- 服务端“重载技能”能力当前是否真的存在
- `action` 多分支技能的完整扫描结果与动作名对照

后续再遇到 `#技能ID`、`#mpCon`、`骑乘无效`、`飞行一下就掉`、`删技能也崩`、`Reader 找不到` 这类问题，优先查本节之后的新增技术章节，不要重新猜。

---

## 24. 技能文本字段、占位符、等级上限兜底

### 24.1 `String/Skill.img` 常见字段的真实含义
在当前项目里，最常用的几个字符串字段是：

- `name`
  - 技能名。

- `desc`
  - 主描述。
  - 例如“在一定时间内提高自己的物理防御力”。
  - 通常用于 tooltip 主描述区域。

- `h`
  - 普通详情模板。
  - 常见写法是带占位符，比如：
    - `消耗MP#mpCon，在#time秒内物理防御力增加#pdd`
  - 如果技能不是按 `h1/h2/h3...` 分等级写法，往往就用这一条。

- `h1 / h2 / h3 ...`
  - 分等级详情文本。
  - 某些技能每一级都有一条单独说明。

- `pdesc`
  - 另一套描述文本。
  - 在原版数据里很多时候用于 PvP / 特殊模式 / 预览说明，不一定每个技能都有。
  - 当前项目里的本地 tooltip 逻辑会把它作为“预览描述/备用描述”保留。

- `ph`
  - 与 `pdesc` 配套的另一套详情模板。
  - 常见于 PvP 或特殊模式的详情说明。

### 24.2 当前客户端本地 tooltip 取值优先级
当前 `SuperSkillWnd` 本地技能 tooltip 逻辑，依据 [skill_local_data.cpp](G:/code/c++/SuperSkillWnd/src/skill/skill_local_data.cpp)：

- `descriptionUtf8`
  - 主来源是 `desc`

- `previewUtf8`
  - 主来源是 `pdesc`

- `detailUtf8`
  - 优先 `ph`
  - 其次 `h1`
  - 再其次会退到普通详情

也就是说：
- `desc` 是主描述
- `ph / h1 / h` 是底部技能数值说明的主要来源

### 24.3 `#mpCon #time #damage` 这类占位符为什么有时会原样显示
当前本地技能数据系统会在 [skill_local_data.cpp](G:/code/c++/SuperSkillWnd/src/skill/skill_local_data.cpp) 里做占位符替换。
替换逻辑依赖：

- 当前技能的本地 record 已加载成功
- 当前等级对应的 level values 已存在
- `skill_local_cache.bin` 内容完整

如果你看到：
- `#mpCon`
- `#time`
- `#damage`
- `#pdd`

这些原样出现在 tooltip 里，优先说明：
- 本地技能 cache 没读到
- 或该技能等级数据没进 cache
- 或运行目录下 `skill_local_cache.bin` / 配套配置缺失

不要优先怀疑 UI 绘制层。

### 24.4 `20001147` 没有显式 `maxLevel` 字段，服务端怎么读等级上限
这一点在服务端源码里已经有明确答案。

见 [Skill.java](G:/code/dasheng099/src/client/Skill.java)：

- 如果技能有 `common` 节点：
  - 读 `common.maxLevel`
  - 然后按 `1..trueMax` 生成 effect

- 如果技能没有 `common` 节点：
  - 遍历 `level` 子节点
  - `maxLevel = effects.size()`

也就是说，服务端不是只靠“某个字符串字段里写了 maxLevel”来判断。
真正决定技能上限的是：
- `common.maxLevel`
- 或 `level` 节点数量

因此：
- `20001147` 没有单独显式 `maxLevel` 字段，并不代表服务端不知道它的上限
- 只要它的 `Skill.wz / img` 数据结构完整，服务端依然能得到正确最大等级

### 24.5 我们自己的超级技能窗口如何兜底 maxLevel
客户端超级技能窗口当前也不是只靠一条静态文本。

在 [skill_overlay_bridge.cpp](G:/code/c++/SuperSkillWnd/src/skill/skill_overlay_bridge.cpp) 和 [skill_overlay_source_game.cpp](G:/code/c++/SuperSkillWnd/src/skill/skill_overlay_source_game.cpp) 里，当前策略是：

- 优先用游戏内真实查询得到的 `gameMaxLevel`
- 如果游戏查询不到，再 fallback 到本地 skill 数据包里的 `localMaxLevel`
- 两边都没有时，才退到 `1`

因此以后如果再遇到：
- 某个技能没有显式 `maxLevel`
- 但原版客户端和服务端都能正常识别等级上限

不要立刻去硬加 `maxLevel` 文本字段。
先确认：
- 游戏原始 skill 数据有没有 `common.maxLevel`
- 或 `level` 节点数量
- 本地 cache 是否已正确导出

### 24.6 `#技能ID` 名字回退是什么意思
如果超级技能列表里显示的是：
- `#1001555`
- `#20001901`

这不是“技能不存在”的第一信号。
它更直接表示：
- 当前本地技能名字没读到
- 名字查询失败后，代码退回了 `#%07d` 形式的 fallback

见 [skill_overlay_bridge.cpp](G:/code/c++/SuperSkillWnd/src/skill/skill_overlay_bridge.cpp)：
- 客户端在名字缺失时会主动构造 `#技能ID` 占位名

所以：
- `#技能ID` = 名字数据缺失/未加载
- 不等于技能等级、逻辑、路由一定都坏了

---

## 25. Reader / img / hostfxr 与当前本地 skill 包路线

### 25.1 历史上确实做过的 `Reader` 路线
这个项目历史上确实做过一条“运行时读取 `.img`/托管导出”的路线：

- `hook.dll` 会去找 `Reader`
- `Reader` 里有：
  - `SkillImgReader.dll`
  - `SkillImgReader.runtimeconfig.json`
  - `hostfxr.dll`

而且后来还专门改过：
- 从固定 `build\\SkillImgReader`
- 调整成相对 `hook.dll` 所在目录的 `Reader`
- 目标部署目录曾设想为：
  - `游戏目录\\Data\\Plugins\\hook.dll`
  - `游戏目录\\Data\\Plugins\\Reader\\...`

这部分历史不要忘。
因为以后如果重新启用 Reader 路线，这些相对路径知识仍然有价值。

### 25.2 当前主线实际状态不是“依赖 Reader 运行”
虽然 [skill_local_data.cpp](G:/code/c++/SuperSkillWnd/src/skill/skill_local_data.cpp) 里还保留了不少：
- `hostfxr`
- `SkillImgReader`
- `ResolveSkillImgReaderDir`
- `RunSkillImgReaderManaged`

这些辅助代码，
但当前主线已经明确改成：
- 运行时优先只用本地 skill 包
- 不再主动依赖 Reader/img 动态构建 cache

直接证据：

- [build.bat](G:/code/c++/SuperSkillWnd/build.bat)
  - 现在明确写了：
  - `Skipping Reader runtime publish (json/local package mode)`

- [skill_local_data.cpp](G:/code/c++/SuperSkillWnd/src/skill/skill_local_data.cpp)
  - 当前日志会写：
  - `using local skill package only (Reader/img disabled)`

所以当前知识库必须记住：
- Reader 路线是“历史实验链/可回启链”
- 本地 skill 包路线才是当前主线

### 25.3 当前运行目录真正需要的不是 `Reader`，而是本地 skill 包
当前主线运行时，必须优先保证这些文件存在：

- `skill_local_cache.bin`
- `super_skills.json`
- `custom_skill_routes.json`
- `native_skill_injections.json`

推荐运行目录：
- `游戏根目录\\skill`

兼容回退目录：
- `游戏根目录\\Data\\skill`

如果缺这些文件，经常会出现：
- 名字退回成 `#技能ID`
- tooltip 文本不完整
- `#mpCon/#time` 不替换
- 本地技能说明读取失败
- super skill 列表只有逻辑，没有文本

### 25.4 如果未来又想重新启用 Reader 路线
必须明确：

1. 当前主线不是靠 Reader 才能跑
2. 如果要重新启用 Reader：
   - 先确认 `hook.dll` 当前部署路径
   - 再确认 `Reader` 相对路径
   - 再确认 `hostfxr.dll` 是否齐全
3. 不要把“源码里还有 hostfxr 代码”误判成“运行时现在一定在用它”

### 25.5 排查顺序
以后再遇到“所有技能名字、描述都没读到”，排查顺序固定为：

1. 看 `C:\\SuperSkillWnd.log` 里有没有：
   - `using local skill package only (Reader/img disabled)`
2. 看运行目录是否存在：
   - `skill_local_cache.bin`
   - `super_skills.json`
   - `custom_skill_routes.json`
   - `native_skill_injections.json`
3. 再看是不是旧 DLL 仍在内存里
4. 最后才去怀疑 Reader / hostfxr

---

## 26. 客户端 `Data\\Skill` / `Data\\String` 资源改坏时，会在没注入 hook 的情况下照样崩

### 26.1 这一点已经被实际现象证实
用户已经实际碰到过：
- 进入游戏
- 打开技能栏直接崩
- 即使 `hook.dll` 没有注入也会崩

这个现象本身就说明：
- 问题不在 hook 注入链
- 而在客户端本地资源链

### 26.2 根因不是“角色身上还留着超级技能”
这类崩溃里，删数据库技能并不能解决。
因为技能栏打开时，客户端会直接枚举本地资源：

- `Data\\Skill\\*.img`
- `Data\\String\\Skill.img`

只要这些文件里某个技能节点被写坏：
- 图标缺
- 结构不完整
- 文本节点乱掉
- 节点名错

那么即使角色一条超级技能都没有，
技能栏依然会在读本地资源时崩。

### 26.3 已经确认过会被工具改写的高风险文件
历史排查里，高风险文件包括：

- `Data\\Skill\\100.img`
- `Data\\Skill\\130.img`
- `Data\\Skill\\1000.img`
- `Data\\Skill\\2000.img`
- `Data\\Skill\\8000.img`
- `Data\\String\\Skill.img`

这些文件一旦被写入不完整技能节点，风险最高。

### 26.4 一种典型的坏法：技能节点不完整
历史排查里最可疑的坏法就是：
- 自定义技能被写进 Skill 资源
- 但缺少完整图标链，比如：
  - `icon`
  - `iconMouseOver`
  - `iconDisabled`

技能栏是原生 UI，
它不会替你兜这些洞。
因此：
- 角色删技能没用
- 关掉 hook 也没用

### 26.5 以后遇到“删技能后还是崩”的标准结论
标准结论应该是：
- 先看客户端资源
- 不要先看数据库

### 26.6 正确回滚顺序
如果以后再出现这种情况，回滚优先级固定为：

1. 先恢复 `Data\\Skill\\*.img` 和 `Data\\String\\Skill.img`
2. 再验证原版技能栏是否恢复正常
3. 最后才重新导入自定义技能

### 26.7 一定要记住的实践规则
以后任何会直接改客户端 `Data` 资源的工具或脚本，都必须遵守：

1. 先备份
2. 分批导入
3. 每批只加少量技能
4. 每批都进游戏测技能栏
5. 不要一次性塞大量自定义技能后再整体排错

---

## 27. Buff 冲突、数值独立计算、当前真正完成到哪一步

### 27.1 用户的目标是什么
用户明确要过两层需求：

1. 某些复制出来的超级 Buff，不要和原版同类 Buff 互相覆盖
2. 更进一步，希望“超级技能”和“普通技能”完全独立计算
   - 包括但不限于：
     - Buff
     - 被动效果
     - 数值类加成

### 27.2 当前主线并没有完成“所有超级技能与普通技能完全独立”
这件事必须写死在知识库里：

- 当前主线没有完成“所有超级技能和普通技能完全独立计算”
- 不能对用户说“现在已经彻底独立”

### 27.3 当前服务端主线 `SuperSkillRegistry.Definition` 真正支持的字段
看 [SuperSkillRegistry.java](G:/code/dasheng099/src/client/SuperSkillRegistry.java)，当前 `Definition` 只支持：

- `skillId`
- `superSpCost`
- `superSpCarrierSkillId`
- `behaviorSkillId`
- `mountItemId`
- `allowMountedFlight`
- `ignoreJobRequirement`
- `ignoreRequiredSkills`

也就是说，当前主线并不支持：

- `separateNumericBuffCalculation`
- `useEnhancedDefenseBuffStats`

这两个字段如果只写进 JSON：
- 当前主线不会自动生效

如果 Java 代码里引用了这两个字段，但 `Definition` 类没同步加成员，
就会出现用户之前真实报过的那种编译错误：
- `找不到符号 separateNumericBuffCalculation`
- `找不到符号 useEnhancedDefenseBuffStats`

### 27.4 之前那条“独立 Buff 数值”路线为什么不能算完成
之前做过一轮尝试：
- 加字段
- 改 SuperSkillRegistry
- 想让超级 Buff 和普通 Buff 分别结算

但这条路线最终没有闭环。
用户实际验证时，仍然出现：
- Buff 覆盖
- 数值不独立
- 配置全加了也没用

所以当前知识库里必须明确写成：
- 这条路线是“尝试过，但未完成收口”
- 不能把它当成已上线能力

### 27.5 关于 `1001003` / `1001555` 这种防御 Buff 冲突的真实结论
`1001003` 与 `1001555` 这类问题，本质上属于：
- 原版 buff stat 分类冲突
- 同类状态位/结算位冲突

也就是说：
- 不是你在超级技能窗口里单独列出来，它就天然独立
- 不是客户端显示独立，它就会服务端结算独立

### 27.6 当前应该怎样对用户描述这件事
以后必须这样描述：

- “显示独立”不等于“数值独立”
- “超级技能能释放”不等于“和原版同类 Buff 不覆盖”
- 当前主线没有通用完成“超级技能 vs 普通技能”的全局独立 Buff/被动结算框架

### 27.7 这部分后续如果再做，正确方向是什么
如果未来要重启这条路线，必须至少分成三层，不要再混成一团：

1. Buff 状态位是否独立
2. 数值是否独立结算
3. 客户端显示是否独立

任何一层没闭环，都不能对外宣称“已经完全独立”。

---

## 28. 骑宠、飞行、飞行骑宠、坐骑资源链的知识库

### 28.1 先区分四种不同概念，不要再混
在这个项目里，经常会混成一团的其实是四件不同的事：

1. 骑宠
   - 召唤坐骑、进入骑乘状态

2. 飞行 Buff
   - 角色获得飞行相关状态/表现

3. 飞行骑宠
   - 已经在骑乘状态下，再获得飞行能力

4. 坐骑资源显示
   - 客户端到底显示哪只坐骑、哪套动作、哪张图

以后排错必须先分清到底是哪一层坏了。

### 28.2 服务端当前真正支持的超级坐骑配置字段
看 [SuperSkillRegistry.java](G:/code/dasheng099/src/client/SuperSkillRegistry.java) 与 [super_skills_server.json](G:/code/dasheng099/super_skills_server.json)，当前服务端主线支持：

- `behaviorSkillId`
  - 指定这个超级技能在服务端行为上借用哪一个原生技能家族
  - 这是服务端的“行为 donor”

- `mountItemId`
  - 强制覆盖服务端发给客户端的坐骑 item/resource 链
  - 直接影响客户端显示哪只坐骑

- `allowMountedFlight`
  - 允许该超级坐骑在服务端被判定为“可飞行骑宠”

- `ignoreJobRequirement`
- `ignoreRequiredSkills`

### 28.3 `behaviorSkillId` 和客户端 `proxySkillId` 不是一回事，但通常要指向同一 donor 家族
这一点非常容易混淆，必须写清楚：

- 客户端 `custom_skill_routes.json` 用的是：
  - `proxySkillId`
  - `releaseClass`
  - `packetRoute`
  - `borrowDonorVisual`

- 服务端 `super_skills_server.json` 用的是：
  - `behaviorSkillId`
  - `mountItemId`
  - `allowMountedFlight`

它们不是同一个字段，
但大多数时候应该指向同一个原生 donor 家族。

举例：
- 客户端想借 `20001147` 的释放与表现
- 服务端也应该把 `behaviorSkillId` 指到 `20001147`

否则经常会出现：
- 客户端看起来像这个技能
- 服务端却按另一个技能家族处理

### 28.4 当前服务端对“骑兽技能”到底怎么看
在 [PlayerHandler.java](G:/code/dasheng099/src/handling/channel/handler/PlayerHandler.java) 里，服务端会专门检查：

- `chr.getStat().getSkillByJob(1004, chr.getJob())`
- `chr.getStat().getSkillByJob(1050, chr.getJob())`

也就是说，服务端对“骑兽技能”的理解不是只看字符串里有没有 `1004` 文本，
而是：
- 根据职业，求出该职业对应的 `1004` 家族技能

这也解释了之前用户提到的现象：
- 技能列表里看不到某个显式 `20001004`
- 但 `String/Skill.img` 里存在 `0001004` 文本说明

这是两层不同概念：
- 文本说明里有统一的“骑兽技能”
- 实际使用时服务端按 `getSkillByJob(1004, job)` 解析职业对应技能

### 28.5 服务端如何判定 mountItemId
服务端关键链在：

- [MapleStatEffect.java](G:/code/dasheng099/src/server/MapleStatEffect.java)
  - `parseMountInfo`
  - `parseMountInfo_Pure`

- [GameConstants.java](G:/code/dasheng099/src/constants/GameConstants.java)
  - `getMountItem`

当前顺序是：

1. 先看当前激活的超级技能有没有配置 `mountItemId`
2. 如果配置了，优先返回这个配置值
3. 否则再走原版 `GameConstants.getMountItem(skillid, chr)`

这意味着：
- `mountItemId` 是一个高优先级覆盖项
- 它确实会直接改变服务端发给客户端的坐骑资源链

### 28.6 当前服务端如何判定“可飞行骑宠”
在 [MapleStatEffect.java](G:/code/dasheng099/src/server/MapleStatEffect.java) 里，当前逻辑是：

1. 如果当前 mounted super skill 有 `allowMountedFlight=true`
   - 直接允许

2. 否则如果当前 mount itemId 落在飞行坐骑区间
   - 也允许

当前硬编码飞行坐骑区间：
- `1992000 ~ 1992014`

因此：
- `allowMountedFlight` 是“服务端显式放行”
- `1992000~1992014` 是“原生飞行坐骑 itemId 规则”

### 28.7 当前服务端对 mount 技能重复使用的处理
在 [PlayerHandler.java](G:/code/dasheng099/src/handling/channel/handler/PlayerHandler.java) 里，当前已经有一条很关键的逻辑：

- 如果当前已经处于 `MONSTER_RIDING`
- 且再次使用的是同一条 mount 源
- 就会取消骑乘 buff

这说明：
- “再次按一下取消骑乘”这条逻辑，服务端主线里是存在的

以后如果用户再说：
- “现在为什么不能再按一次取消”

要优先怀疑：
- 当前实际走的不是同一条 mount 源
- 或者客户端释放链/behavior donor 不一致
- 或实验版把骑乘与飞行强绑死了

### 28.8 当前关于“原版手感”的知识库结论
用户已经明确表达过希望：
- 骑乘和飞行分开
- 恢复“上 + 跳”
- 不要变成“只要跳跃就飞”

因此知识库里必须记住：

- “所有坐骑都直接跳跃飞行”不是最终目标
- 那只是某次实验版现象
- 后续若继续做坐骑飞行，必须优先遵守“原版手感”目标

### 28.9 关于原版技能链的务实结论
在当前服务端与资源环境下，以下几条要牢牢记住：

- `80001000`
  - 是原生骑乘核心 donor 之一

- `80001004`
  - 是一个非常实用的 donor
  - 能直接召唤坐骑
  - 也容易配合 `mountItemId` 做显示替换

- `20001147`
  - 是飞行骑宠 donor 家族之一
  - 但“复制它 = 立刻能飞”并不成立
  - 还要看：
    - 当前 mount 是否真正召出
    - 当前 map 是否允许
    - 客户端是否有对应资源
    - 是否有可飞行 mount item 链

- `10001026`
  - 更像单独飞行/飞行表现 donor
  - 受地图限制明显
  - 不能简单等同于“飞行骑宠”

### 28.10 坐骑资源链的最低限理解
当前至少要记住这条链：

1. 技能释放
2. 服务端根据 `behaviorSkillId` 决定按哪种原生骑乘/飞行技能家族处理
3. 服务端根据 `mountItemId` 或 `GameConstants.getMountItem(...)` 算出 mount item
4. 客户端根据该 item 链去读：
   - `Character.wz\\TamingMob\\0xxxxxxx.img`
   - 对应的 `TamingMob.wz` 家族数据

因此以后如果用户问：
- “图标是哪个文件”
- “动作是哪个文件”
- “速度是哪个文件”

不要只盯技能 skillId。
真正决定坐骑显示和动作的是：
- 最终 mount item 资源链

### 28.11 `mountItemId` 的务实配置结论
在当前主线里：
- `mountItemId` 写在服务端 [super_skills_server.json](G:/code/dasheng099/super_skills_server.json)
- 它不是写在客户端技能文本 JSON 里的字段

例如：
```json
{
  "skillId": 80001903,
  "behaviorSkillId": 80001004,
  "mountItemId": 1932007,
  "superSpCost": 1,
  "superSpCarrierSkillId": 1001999,
  "ignoreJobRequirement": true,
  "ignoreRequiredSkills": true
}
```

它的含义是：
- 这个超级技能行为上借 `80001004`
- 但显示/资源链强制改成 `1932007` 对应的坐骑

### 28.12 如果“能骑不能飞”，该怎么理解
这通常意味着：

- 骑乘链 OK
- 飞行链没接上

常见原因：
- `allowMountedFlight` 没开
- 最终 mount item 不在 `1992000~1992014`
- donor 配成了只能骑、不能飞的家族
- 原版飞行 prerequisite 没满足

不要把“能召唤坐骑”误判成“飞行骑宠链已经通了”。

---

## 29. 服务端技能重载能力：当前主线是否存在、如何判断

### 29.1 当前主线里，服务端“重载技能”能力是存在的
这件事已经不应再按历史报错来判断，而要按当前源码判断。

当前 `dasheng099` 主线里已经存在：

- [SkillFactory.java](G:/code/dasheng099/src/client/SkillFactory.java)
  - `reload()` / `load()` 兼容逻辑

- [SuperSkillRegistry.java](G:/code/dasheng099/src/client/SuperSkillRegistry.java)
  - `reloadNow()`
  - `getLoadedSkillCount()`
  - `getLoadedPath()`

- [MapleCharacter.java](G:/code/dasheng099/src/client/MapleCharacter.java)
  - `rebindSkillsAfterFactoryReload()`

### 29.2 当前 GM 命令里也已经挂上了
看 [SuperGMCommand.java](G:/code/dasheng099/src/client/messages/commands/SuperGMCommand.java)：

- 已有命令：
  - `*重载技能 - 重新载入技能数据与超级技能配置`

执行内容包括：
- `SkillFactory.reload()`
- `SuperSkillRegistry.reloadNow()`
- 遍历在线角色执行 `rebindSkillsAfterFactoryReload()`
- 输出：
  - skills 数量
  - superSkills 数量
  - onlineChars
  - reboundEntries
  - 当前超级技能配置路径

### 29.3 图形管理端也已经挂了兼容按钮
看 [DaSheng.java](G:/code/dasheng099/src/gui/DaSheng.java)：

- 已有技能重载按钮逻辑
- 还专门做了反射兼容

也就是说，即使某些方法名不存在，也会尝试 fallback：
- `reload() -> load()`
- `reloadNow() -> refreshIfNeeded(true)`
- `getLoadedSkillCount() -> definitions.size()`

### 29.4 这类编译错误在知识库里该怎么理解
用户之前报过：
- `找不到符号 reload()`
- `找不到符号 reloadNow()`
- `找不到符号 rebindSkillsAfterFactoryReload()`
- `找不到符号 getLoadedSkillCount()`
- `找不到符号 getLoadedPath()`

以后看到这类报错，标准结论应该是：

- 不是“这个功能设计上不存在”
- 而是“当前服务端代码没有同步到包含这些方法的版本”

### 29.5 当前这部分知识的使用原则
以后如果用户问：
- “修改配置后要不要重启服务端”

要回答为：

- 当前主线已经支持“重载技能”能力
- 如果当前部署的服务端代码就是 `dasheng099` 这套主线，优先尝试 `重载技能`
- 只有当部署服不是这套代码、或这些方法没同步过去时，才需要回到“重启服务端”方案

---

## 30. `action` 多分支技能与动作名对照表

### 30.1 总规则：`action` 编号是“动作分支索引”，不一定一项对一类武器
这一点非常关键。
不能看到：

- `0`
- `1`
- `2`
- `3`

就直接假设：
- 4 个编号 = 4 种武器

当前资源实际已经证实：

- 有的技能确实 1:1 对应武器分支
- 有的技能不是，它只是“2 个动作名”，但仍然支持 4 个武器家族

### 30.2 `1121008` 的真实结论
`1121008` 在 [112.img.json](G:/code/skill_json/112.img.json) 里：

- `action`
  - `0 = braveslash1`
  - `1 = braveslash2`
  - `2 = braveslash3`
  - `3 = braveslash4`

- 同时它的 `finalAttack`
  - `0 = 30`
  - `1 = 31`
  - `2 = 40`
  - `3 = 41`

对应武器家族为：

- `30 -> 130 -> 单手剑`
- `31 -> 131 -> 单手斧`
- `40 -> 140 -> 双手剑`
- `41 -> 141 -> 双手斧`

所以必须明确记住：
- `1121008` 不是“单双手剑 + 单双手锤”
- 而是“单双手剑 + 单双手斧”

### 30.3 当前已确认的动作名文本表
先给最直接的动作名表：

```text
iceAttack1   -> 冰攻击动作1，寒冰突击第1套动作
iceAttack2   -> 冰攻击动作2，寒冰突击第2套动作

brandish1    -> 轻舞飞扬 / Brandish 第1套挥砍动作
brandish2    -> 轻舞飞扬 / Brandish 第2套挥砍动作

braveslash1  -> 勇猛劈砍，第0分支，对应单手剑（130）
braveslash2  -> 勇猛劈砍，第1分支，对应单手斧（131）
braveslash3  -> 勇猛劈砍，第2分支，对应双手剑（140）
braveslash4  -> 勇猛劈砍，第3分支，对应双手斧（141）

burster2     -> 龙连击，第0分支，对应 143（枪）
burster1     -> 龙连击，第1分支，对应 144（矛）

swingP1      -> 龙挥砍，第0分支，对应 143（枪）
swingP2      -> 龙挥砍，第1分支，对应 144（矛）
```

### 30.4 当前已经全量扫出来的“多 action 技能”列表
在当前 [skill_json](G:/code/skill_json) 这批资源里，`action` 带多个编号分支的技能一共确认到以下这些：

```text
0000097    寒冰突击   -> iceAttack1 / iceAttack2
1111010    轻舞飞扬   -> brandish1 / brandish2
1121008    勇猛劈砍   -> braveslash1 / braveslash2 / braveslash3 / braveslash4
1311001    龙连击     -> burster2 / burster1
1311003    龙挥砍     -> swingP1 / swingP2
10000097   寒冰突击   -> iceAttack1 / iceAttack2
11111004   轻舞飞扬   -> brandish1 / brandish2
20000097   寒冰突击   -> iceAttack1 / iceAttack2
20010097   寒冰突击   -> iceAttack1 / iceAttack2
20020097   寒冰突击   -> iceAttack1 / iceAttack2
30000097   寒冰突击   -> iceAttack1 / iceAttack2
30010097   寒冰突击   -> iceAttack1 / iceAttack2
```

### 30.5 哪些技能能直接从 `action` 推出武器，哪些不能
#### 能直接推的
- `1121008`
  - 4 个 `braveslash`
  - 对应 4 个 `finalAttack` 编号
  - 可以直接一一映射

- `1311001`
  - `burster2 / burster1`
  - 对应 `43 / 44`

- `1311003`
  - `swingP1 / swingP2`
  - 对应 `43 / 44`

#### 不能简单一一推的
- `1111010`
- `11111004`

这两条虽然 `action` 只有：
- `brandish1`
- `brandish2`

但 `finalAttack` 却支持：
- `30 / 31 / 40 / 41`
或其子集

这说明：
- `brandish1/2` 是动作变体
- 不是“每个武器家族一个动作名”

### 30.6 `iceAttack1 / iceAttack2` 当前能确认到哪一步
当前已经确认：

- `0000097`
- `10000097`
- `20000097`
- `20010097`
- `20020097`
- `30000097`
- `30010097`

都使用：
- `iceAttack1`
- `iceAttack2`

并且在当前扫描里，`iceAttack1/2` 只在 [00002000.img.xml](G:/code/dasheng099/wz/Character.wz/00002000.img.xml) 这一套基础动作资源里明确出现。

当前没有证据表明：
- `iceAttack1`
- `iceAttack2`

对应不同武器家族。

因此知识库里要写成：
- 这更像“两个冰攻击动作分支”
- 不是已证实的“多武器映射表”

### 30.7 服务端 `SkillFactory` 里动作枚举也已经收录了这些名字
在 [SkillFactory.java](G:/code/dasheng099/src/client/SkillFactory.java) 里，当前也能看到这些动作枚举：

- `brandish1`
- `brandish2`
- `braveslash1`
- `braveslash2`

这说明这些动作名不只是静态资源字符串，
它们在服务端技能体系里也属于被明确识别的动作名称。

### 30.8 以后如果要自己判断一个技能的 `action` 到底是什么意思，固定流程如下
1. 先看 skill img 里的 `action`
2. 再看同技能是否存在 `finalAttack`
3. 如果 `finalAttack` 有 `30/31/40/41/43/44` 这类值：
   - 再翻成武器家族
4. 如果没有 `finalAttack`
   - 不要擅自说它是“某武器专用动作”
   - 先把它当成“动作变体/动作阶段”

---

## 31. 这轮新增知识对后续工作的直接指导

以后优先按下面的规则行动：

1. 名字、说明、`#mpCon`、`#time` 问题先查本地 `skill_local_cache.bin`，不要先怀疑 UI。
2. 删掉所有超级技能仍然技能栏崩，先查客户端 `Data\\Skill` / `Data\\String`，不要先查数据库。
3. Buff 叠加 / 独立计算不能默认视为已完成，必须先看服务端主线是否真的有对应字段和逻辑。
4. 骑宠 / 飞行 / 飞行骑宠问题先拆成“骑乘链 / 飞行链 / 显示资源链”三层再定位。
5. 问 `Reader` 时，先区分历史 Reader 路线和当前主线本地 skill 包路线。
6. 判断多 `action` 技能时先看 `finalAttack`，不要只凭 `action` 名字猜武器。

这些规则都来自真实源码和真实踩坑记录，后续应优先复用，避免重复绕路。
