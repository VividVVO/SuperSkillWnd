# SuperSkillWnd 操作手册

更新时间：2026-04-12

本文件只保留“怎么做”的方法手册：
- 怎么选工程
- 怎么构建、部署、确认版本
- 怎么查日志、源码、dump、x32dbg、IDA、JSON/WZ 数据
- 怎么在查明后把结论沉到知识库

项目事实、历史结论、失败路线、配置语义、业务规则，不再堆在本文件里，统一放到独立知识库。

---

## 1. 使用顺序

1. 先读本文件，确认当前要用哪套方法排查。
2. 再打开对应知识库，不要一上来从头翻大文件。
3. 先用日志、源码、配置、版本确认缩小范围。
4. 缩不动时，再进入 dump / x32dbg / IDA。
5. 结论稳定后，补回对应 `*_knowledge.md`，不要继续把事实型内容堆回 `CLAUDE.md`。

---

## 2. 知识库入口

- 项目综合数据知识库：`G:\code\c++\SuperSkillWnd\.claude\rules\project_general_knowledge.md`
- D3D8 兼容性完整知识库：`G:\code\c++\SuperSkillWnd\.claude\rules\d3d8_compat_knowledge.md`
- 原生窗口层级/遮挡知识库：`G:\code\c++\SuperSkillWnd\.claude\rules\native_window_layering_knowledge.md`
- 完全独立原生窗口与多子窗口路线知识库：`G:\code\c++\SuperSkillWnd\.claude\rules\independent_native_window_multi_child_knowledge.md`
- SkillWnd 官方 second-child route 专属知识库：`G:\code\c++\SuperSkillWnd\.claude\rules\skillwnd_second_child_route_knowledge.md`
- SkillWnd 官方 second-child 生命周期闭环知识库：`G:\code\c++\SuperSkillWnd\.claude\rules\skillwnd_second_child_lifecycle_knowledge.md`
- SkillWnd 官方 9DC220 second-child 主载体迁移知识库：`G:\code\c++\SuperSkillWnd\.claude\rules\skillwnd_second_child_primary_carrier_knowledge.md`
- SkillWnd second-child 运行日志闭环知识库：`G:\code\c++\SuperSkillWnd\.claude\rules\skillwnd_second_child_runtime_log_closure_knowledge.md`
- SkillWnd 9DC220 主载体任务 4/5/6 落地知识库：`G:\code\c++\SuperSkillWnd\.claude\rules\skillwnd_second_child_tasks_4_5_6_knowledge.md`
- 原生窗口 sampler 专属知识库：`G:\code\c++\SuperSkillWnd\.claude\rules\native_window_sampler_knowledge.md`
- 技能气泡文字排版与 glyph 度量知识库：`G:\code\c++\SuperSkillWnd\.claude\rules\skill_tooltip_typography_knowledge.md`

---

## 3. 工程选择方法

### 3.1 默认工程

除非用户明确说：
- `SuperSkillWnd - 副本`
- `只改副本`

否则默认工作目录是：
- 主工程：`G:\code\c++\SuperSkillWnd`

### 3.2 常用路径

- 主工程：`G:\code\c++\SuperSkillWnd`
- 副本工程：`G:\code\c++\SuperSkillWnd - 副本`
- 游戏目录：`G:\code\mxd`
- 客户端日志：`C:\SuperSkillWnd.log`
- 服务端工程：`G:\code\dasheng099`

### 3.3 绝对不要犯的错误

- 不要把主工程和副本工程改混
- 不要只看“我刚编译过”，就断定游戏里跑的是新 DLL
- 不要在没缩小范围前就让用户重复大量测试

---

## 4. 构建、部署、版本确认

### 4.1 构建

当前主线构建脚本：

```powershell
cmd /c build.bat
```

当前关键产物：

- `G:\code\c++\SuperSkillWnd\build\Debug\SS.dll`
- `G:\code\c++\SuperSkillWnd\build\Debug\hook.dll`

### 4.2 部署

当前常用部署目标以实际注入链为准。最近这条线常用的是：

- `G:\code\mxd\hook_v167.dll`

如果用户明确指定别的目标 DLL，按用户指定的来。

### 4.3 版本确认

必须一起确认：

1. 游戏进程启动时间
2. 游戏目录 DLL 修改时间
3. 构建目录 DLL 修改时间
4. `C:\SuperSkillWnd.log` 里的版本 marker / 最近日志

常用命令：

```powershell
Get-Item G:\code\mxd\hook_v167.dll | Select-Object LastWriteTime, Length
Get-Item G:\code\c++\SuperSkillWnd\build\Debug\hook.dll | Select-Object LastWriteTime, Length
Get-FileHash G:\code\mxd\hook_v167.dll -Algorithm SHA256
Get-FileHash G:\code\c++\SuperSkillWnd\build\Debug\hook.dll -Algorithm SHA256
Get-Process MapleStory | Select-Object Id, StartTime, Path
Get-Content C:\SuperSkillWnd.log -Tail 200
```

### 4.4 用户说“没变化”时的固定检查顺序

1. 先确认有没有改错工程目录
2. 再确认有没有把新 DLL 真部署到游戏目录
3. 再确认游戏是不是完全重开了
4. 再确认日志里是不是新版本 marker
5. 以上都成立，才继续看逻辑问题

---

## 5. 日志查询方法

日志文件：

- `C:\SuperSkillWnd.log`

常用命令：

```powershell
Get-Content C:\SuperSkillWnd.log -Tail 200
Select-String -Path C:\SuperSkillWnd.log -Pattern "BtnDrawState","SkillBtnDraw","SuperSkill"
rg -n "BtnDrawState|BtnResolveDraw|BtnDonor|SkillBtnDraw|InputSpoof|SuperSkill" C:\SuperSkillWnd.log
```

常用关键词分组：

- 按钮链：`NativeBtn` `BtnCoreCreate` `BtnDonorCreate` `BtnResolveDraw` `BtnDrawState`
- Present/D3D 绘制：`SkillBtnDraw` `PresentBtnDraw` `PresentCursorDraw` `BtnClip`
- 技能桥接：`SkillBridge` `populate` `assignSlot` `quickSlot` `local-level-up`
- 超级技能 SP：`SuperSkill` `carrier` `points=` `sync state` `direct=`
- 视觉/路由：`SkillRoute` `borrowVisual` `native-route` `observe opcode=0x93`
- 鼠标/输入：`InputSpoof` `suppressMouse` `MouseSuppressFallback`

---

## 6. 源码搜索方法

优先用 `rg`：

```powershell
rg -n "关键字" G:\code\c++\SuperSkillWnd\src
rg -n "关键字" G:\code\dasheng099\src
rg -n "1001555|1001003|1001999" G:\code\c++\SuperSkillWnd
```

源码搜索优先级：

1. 先找日志关键字
2. 再找 skillId / 地址 / 枚举 / 关键函数名
3. 再沿调用链往上追
4. 确认客户端和服务端是否在看同一份配置 / 同一条 skill 链

---

## 7. dump 使用方法

用户常给的 dump 目录通常类似：

- `G:\code\memdump\...`

使用规则：

1. 先确认这是哪一轮运行时的地址，不要混上一轮
2. 先看当前对象地址、wrapper、leaf、pixelBase、pitch、width、height
3. 优先拿原版对照对象一起比
4. dump 只用来缩小范围，不要拿孤立地址硬下结论

---

## 8. x32dbg 使用方法

使用原则：

1. 先靠日志把范围缩小
2. 再让用户抓最小必要数据
3. 不要一上来就要完整 trace

常见抓法：

- 对按钮状态槽地址下硬件访问断点
- 对 draw object / wrapper 下断点
- 记录：
  - `EIP`
  - 附近 `10~20` 条汇编
  - `Call Stack`
  - 关键寄存器

---

## 9. IDA 查询方法

IDA 主要用来查：

- 伪代码
- 汇编
- 函数地址
- 调用链
- 结构偏移

### 9.1 查法

1. 先在本地源码或日志里拿到关键函数名 / 地址 / skillId / 行为名
2. 再去 IDA 定位函数
3. 先看当前函数做什么
4. 再看谁调用它、它又调用谁
5. 需要结构时，再看寄存器和偏移怎么流动

### 9.2 遇到按钮 / 状态机 / 鼠标命中 / dirty 绘制时，优先检查

- `sub_B9F570`
- `sub_B9F6E0`
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

### 9.3 查地址时的固定动作

1. 先看 `GameAddresses.h` 里有没有已有命名
2. 没命名再去 IDA 反查
3. 新确认的地址、偏移、对象边界，要写回对应知识库

---

## 10. JSON / WZ / Skill 数据查询方法

客户端常查：

- `skill\super_skills.json`
- `skill\native_skill_injections.json`
- `skill\custom_skill_routes.json`
- `Skill.img.json`
- `xxx.img.json`

服务端常查：

- `G:\code\dasheng099\super_skills_server.json`
- `StatsHandling.java`
- `MapleCharacter.java`
- `SuperSkillRegistry.java`

查法原则：

1. 先分清客户端在看什么，服务端在看什么
2. 不要只改一侧就断言整条链生效
3. 有 skillId 差异时，先查读档、加点、显示、释放四条链是不是统一

---

## 11. 通用排查流程

### 11.1 客户端 UI / D3D / 鼠标问题

1. 确认当前跑的是哪版 DLL
2. 看日志关键字
3. 看源码绘制路径
4. 必要时进 IDA 看原生调用链
5. 改完后重新构建、部署、重开游戏验证

### 11.2 客户端/服务端联调问题

1. 先分清问题发生在显示、加点、读档、释放还是数值计算
2. 分别查客户端链和服务端链
3. 确认双方 skillId、carrier、配置来源一致
4. 不要把“客户端显示有了”当成“服务端真正支持了”

---

## 12. 知识库维护方法

### 12.1 什么时候写知识库

满足下面任一条件就应该补知识库：

- 已经确认了一个长期有效的根因
- 已经证伪了一条很容易再踩的错误路线
- 已经确认了一组地址、偏移、结构边界、配置语义
- 某条链路需要固定排查顺序

### 12.2 写到哪里

- 通用项目数据：写到 `project_general_knowledge.md`
- 专项问题：写到对应 `*_knowledge.md`
- `CLAUDE.md` 只保留方法，不再堆事实结论

### 12.3 命名建议

新知识库文件统一放：

- `G:\code\c++\SuperSkillWnd\.claude\rules\`

命名建议：

- `xxx_knowledge.md`

名字要能直接看出主题，不要起含糊名字。

---

## 13. 一句话原则

先确认版本，先缩小范围，先分清客户端和服务端，再决定要不要进 IDA / dump / x32dbg；稳定结论写知识库，不再把事实型数据塞回 `CLAUDE.md`。
