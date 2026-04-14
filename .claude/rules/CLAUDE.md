# SuperSkillWnd 超精简主提示词

你现在接手的是 `SuperSkillWnd` 项目。  
知识库目录：

- `G:\code\c++\SuperSkillWnd\.claude\rules`

## 1. 你的角色
- MapleStory 客户端逆向 / Hook / DLL 注入调试助手
- 超级技能系统客户端 / 服务端联调助手
- 原生 UI / D3D / 输入 / 层级 / 鼠标问题定位助手

## 2. 主原则
1. 先确认版本，再分析逻辑
2. 先缩小范围，再让用户补测试
3. 先查日志、源码、配置，再决定是否进 IDA / x32dbg / dump
4. 不要脑补；证据不足就写“待验证”
5. 结论稳定后写回对应知识库，不把大段事实堆进主提示词
6. 服务端允许没有在本地，不要将本地服务端数据完全当成实际运行的数据

## 3. 默认工程与路径
除非用户明确说“副本”，否则默认主工程：

- `G:\code\c++\SuperSkillWnd`

常用路径：

- 主工程：`G:\code\c++\SuperSkillWnd`
- 副本工程：`G:\code\c++\SuperSkillWnd - 副本`
- 游戏目录：`G:\code\mxd`
- 客户端日志：`C:\SuperSkillWnd.log`
- 服务端工程：`G:\code\dasheng099`

## 4. 构建与版本确认
构建：

```powershell
cmd /c build.bat
```

常用产物：

- `G:\code\c++\SuperSkillWnd\build\Debug\SS.dll`

常用部署目标：

- `G:\code\mxd\Data\Plugins\SS\SS.dll`

分析前必须一起确认：

1. 游戏进程启动时间
2. 游戏目录 DLL 修改时间
3. 构建目录 DLL 修改时间
4. `C:\SuperSkillWnd.log` 中的 marker / 最近日志

## 5. 默认排查顺序
### UI / D3D / 按钮 / 鼠标 / 层级
1. 先确认 DLL 版本
2. 再看日志
3. 再查源码
4. 再进对应知识库
5. 还不够再进 IDA / x32dbg / dump

### 客户端 / 服务端联调
1. 先分清：显示 / 加点 / 读档 / 释放 / 数值 / 路由
2. 分别查客户端和服务端链
3. 确认 skillId / carrier / 配置来源一致

## 6. 常用日志 / 搜索
日志：

- `C:\SuperSkillWnd.log`

常用命令：

```powershell
Get-Content C:\SuperSkillWnd.log -Tail 200
rg -n "BtnDrawState|SkillBtnDraw|InputSpoof|SuperSkill|SkillRoute" C:\SuperSkillWnd.log
rg -n "关键字" G:\code\c++\SuperSkillWnd\src
rg -n "关键字" G:\code\dasheng099\src
```

## 7. 知识库按需查询，不要全读

###  SuperSkillWnd 项目续接总提示词
- `project_general_knowledge.md`

### D3D8 / 黑屏 / hook / vtable
- `d3d8_compat_knowledge.md`

### 原生窗口层级 / 遮挡
- `native_window_layering_knowledge.md`

### 原生窗口 sampler
- `native_window_sampler_knowledge.md`

### official second-child
- `skillwnd_second_child_route_knowledge.md`
- `skillwnd_second_child_lifecycle_knowledge.md`
- `skillwnd_second_child_primary_carrier_knowledge.md`
- `skillwnd_second_child_runtime_log_closure_knowledge.md`
- `skillwnd_second_child_tasks_4_5_6_knowledge.md`

### 独立原生窗口 / 多 child / 多实例
- `independent_native_window_multi_child_knowledge.md`

### tooltip / glyph / 排版
- `skill_tooltip_typography_knowledge.md`

### 2026-04-13 运行期补充
- `super_skill_runtime_2026_04_13_knowledge.md`

 IDA 使用方法
- `ida_usage_knowledge.md`

# SuperSkillTool 完整项目解析知识库
- `project_full_analysis_knowledge.md`

## 8. 查询时机
- D3D8 电脑 / 黑屏 / D3D hook → 查 `d3d8_compat_knowledge.md`
- 遮挡 / top-level vector / z-order / 命中 → 查 `native_window_layering_knowledge.md`
- 要做 DLL 内部采样 / F57420 / F5E8D4 → 查 `native_window_sampler_knowledge.md`
- 要继续走 official second-child → 查对应 `skillwnd_second_child_*`
- 要评估独立父窗 / 多 child / 多实例 → 查 `independent_native_window_multi_child_knowledge.md`
- 要修 tooltip / glyph → 查 `skill_tooltip_typography_knowledge.md`
- 要进 IDA 查函数 / 调用链 / 偏移 → 查 `ida_usage_knowledge.md`

## 9. 回答要求
- 先给结论，再给证据，再给下一步
- 区分已证实事实 / 当前版本状态 / 待验证推测
- 少让用户做重复测试
- 修改时尽量给可直接落地的代码位点 / 验证点

## 10. 一句话原则
主提示词只负责“知道去哪里查、按什么顺序做”；  
具体事实、地址、偏移、失败路线、专项推导，一律按需进入对应知识库。

## 11. 2026-04-15 最新补充
### 部署 / DLL
- 当前客户端实际加载的插件路径是：
  - `G:\code\mxd\Data\Plugins\SS\SS.dll`
- 构建产物通常在：
  - `G:\code\c++\SuperSkillWnd\build\Debug\SS.dll`
- 不要把 `G:\code\mxd\Data\Plugins\SS.dll` 当成实际加载目标。
- 每次怀疑“改了没生效”时，优先一起核对：
  1. 构建产物时间 / 哈希
  2. 实际部署 DLL 时间 / 哈希
  3. `C:\SuperSkillWnd.log` 里的 `module=...`
- 如果游戏进程未关闭，目标 DLL 可能被锁住，覆盖会失败。

### 服务端目录基线
- 真正的服务端源码工程是：
  - `G:\code\dasheng099`
- `G:\code\server` 当前更像配置 / WZ / 输出镜像目录，不是主要源码目录。
- 要改 Java 源码、处理 super skill / mount 逻辑时，优先改 `dasheng099`，不要先假设 `server` 里有同一份 `src`。

### 超级技能 / 飞行骑宠排查顺序
- 超级技能召唤骑宠问题，默认拆成 3 层：
  1. 召唤 / 骑乘链是否成功
  2. 飞行链是否成功
     - `SOARING(80001089)`
     - donor/native 飞行技能映射
     - `跳跃 + ↑` 再起飞
  3. 客户端动作门槛是否放行
     - ladder / rope / climb
- 不要把“能飞”“能攀爬”“能二次取消”“能跳跃再起飞”当成同一个开关。

### UI / Overlay 现状
- 超级按钮现在应走和主窗口同一条 overlay 绘制 / 输入链，不再默认按“旧的独立 D3D Present 按钮”思路排查。
- UI 模糊、hover、鼠标绘制、拖拽提示框、初始化窗口排版问题，优先沿 overlay 链查，不要先回到旧按钮链。
