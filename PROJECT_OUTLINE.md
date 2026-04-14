# SuperSkillWnd 项目纲要（接手即用）

更新时间：2026-04-13

## 1. 项目目标
在 MapleStory 客户端注入链中提供一套“超级技能栏”能力，核心包括：
1. SkillWnd 旁路 UI（D3D9/D3D8 双路线）。
2. 原生按钮与 second-child 子窗联动。
3. 技能数据桥接、等级读取、发包改写、快捷栏拖拽。
4. 在不破坏原生窗口交互的前提下完成增强。

## 2. 硬约束（来自 CLAUDE.md 的方法约束）
1. 先确认版本、部署、日志，再分析逻辑。
2. 问题稳定后沉淀知识库，不把事实堆回 `CLAUDE.md`。
3. 主工程默认是 `G:\code\c++\SuperSkillWnd`，不要和副本工程混改。
4. 所有改动必须以“不影响现有效果”为第一验收标准。

## 3. 当前真实结构
```text
src/
├─ dllmain.cpp                       # 运行时主编排（Hook 安装、渲染入口、WndProc、生命周期）
├─ core/
│  ├─ Common.h                       # 日志、内存读安全、CWnd 坐标工具、D3D9 画图工具
│  └─ GameAddresses.h                # 逆向地址常量表
├─ hook/
│  ├─ InlineHook.h                   # x86 inline hook
│  └─ win32_input_spoof.*            # IAT 输入欺骗（鼠标抑制）
├─ skill/
│  ├─ skill_overlay_bridge.*         # 技能桥接核心（配置、映射、发包/等级/注入）
│  ├─ skill_overlay_source*          # game/manager 双 source 适配
│  ├─ skill_local_data.*             # 本地技能包加载、tooltip/图标
│  └─ SkillData*.h                   # SkillManager/SkillTab 数据结构
├─ ui/
│  ├─ super_imgui_overlay.*          # D3D9 overlay 适配
│  ├─ super_imgui_overlay_d3d8.*     # D3D8 overlay 适配
│  ├─ retro_skill_panel.*            # 面板渲染与交互
│  ├─ retro_skill_app.*              # UI 行为 Hook 对接层
│  ├─ retro_skill_state.*            # 运行时状态模型
│  ├─ retro_skill_assets.*           # 资源加载
│  ├─ retro_render_backend.*         # D3D8/D3D9 纹理抽象
│  ├─ retro_skill_text_dwrite.*      # DWrite 字体/排版
│  └─ overlay_*_utils.*              # overlay 输入/光标/样式共用工具
├─ d3d8/
│  └─ d3d8_renderer.*                # D3D8 纹理、状态、文本绘制
├─ runtime/
│  ├─ init_pipeline.*                # Hook 安装管线
│  ├─ cleanup_pipeline.*             # 清理管线
│  └─ dllmain_section_*.inl          # dllmain 实现分段
├─ util/
│  └─ runtime_paths.*                # 运行期路径解析
└─ experimental/
   └─ skillwnd_second_child_carrier_probe.cpp
```

## 4. 运行时主链路
1. `DllMain` 启动 `InitThread`，写入 build marker。
2. `InitThread` 初始化 `SkillManager` 与 `SkillOverlayBridge`。
3. 运行时判断 D3D8 或 D3D9。
4. 通过 `SuperRuntimeRunInstallPipeline` 安装 D3D、按钮、SkillWnd、消息、发包、等级查询等 Hook。
5. Present/WndProc 驱动 overlay 渲染与输入分发。
6. `DLL_PROCESS_DETACH` 通过 `SuperRuntimeRunCleanupPipeline` 执行资源释放、overlay 关闭、输入状态恢复。

## 5. 关键模块职责表
| 模块 | 入口文件 | 对外职责 | 当前耦合点 |
|---|---|---|---|
| Runtime 编排 | `src/dllmain.cpp` | 安装 Hook、生命周期管理、跨模块调度 | 逻辑过于集中，状态全局变量过多 |
| Runtime 管线 | `src/runtime/init_pipeline.*` `src/runtime/cleanup_pipeline.*` | 固化安装/清理顺序，保持日志和失败降级一致 | 只做编排，不放业务 |
| Skill 桥接 | `src/skill/skill_overlay_bridge.cpp` | 技能映射、配置加载、发包/等级改写、快捷栏绑定 | 含路径解析与游戏读写，职责偏重 |
| UI 渲染 | `src/ui/retro_skill_panel.cpp` | 面板绘制、tooltip、拖拽交互 | 与 overlay 适配层边界清晰，可继续下沉 |
| Overlay 适配 | `src/ui/super_imgui_overlay*.cpp` | Win32 输入 + ImGui + D3D8/9 帧驱动 | D3D8/D3D9 逻辑存在重复 |
| Overlay 共用工具 | `src/ui/overlay_*_utils.*` | 鼠标/键盘消息分类、光标 suppress、ImGui 样式字体 | 低风险共用层 |
| 输入欺骗 | `src/hook/win32_input_spoof.cpp` | IAT Hook 鼠标状态，避免穿透冲突 | 与运行时开关耦合在 dllmain |
| 地址契约 | `src/core/GameAddresses.h` | 逆向地址/偏移常量 | 缺少 typed facade，调用点分散 |
| 路径工具 | `src/util/runtime_paths.*` | 运行期路径推导 | 后续可统一 bridge/local data 路径逻辑 |

## 6. 新人 30 分钟读码路径
1. 先读 `build.bat`，明确编译单元与最终产物。
2. 读 `src/dllmain.cpp` 的顶部全局状态、`InitThread`、`DllMain`。
3. 读 `src/runtime/init_pipeline.cpp` 和 `src/runtime/cleanup_pipeline.cpp`，看安装/清理顺序。
4. 读 `src/skill/skill_overlay_bridge.h`，看桥接 API 面。
5. 读 `src/ui/retro_skill_state.h`，掌握 UI 状态模型。
6. 读 `src/ui/retro_skill_panel.cpp`，理解交互行为。
7. 读 `src/ui/super_imgui_overlay.cpp` 或 `super_imgui_overlay_d3d8.cpp`，理解输入与渲染接线。
8. 遇到地址或偏移问题再查 `src/core/GameAddresses.h` 与对应知识库。

## 7. 开发日常命令
1. 构建：`cmd /c build.bat`
2. 查看关键日志：`Get-Content C:\SuperSkillWnd.log -Tail 200`
3. 查关键词：`rg -n "SkillBtnDraw|SuperSkill|BtnDrawState|InputSpoof" C:\SuperSkillWnd.log`
4. 源码定位：`rg -n "关键字" G:\code\c++\SuperSkillWnd\src`

## 8. 协作约定（防回归）
1. 每次改动必须记录 build marker 与关键日志链路。
2. 先做“定位式变更”，后做“结构式变更”，不要混在同一提交。
3. 新增事实结论写入 `*_knowledge.md`，不要回填到 `CLAUDE.md`。
4. 任何 Hook 类改动必须附上安装失败与降级路径日志。
