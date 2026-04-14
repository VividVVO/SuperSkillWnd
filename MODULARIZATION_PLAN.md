# SuperSkillWnd 去耦与模块化改造计划（不改效果）

更新时间：2026-04-13

## 1. 目标
在保持当前效果不变的前提下，把 `src/dllmain.cpp` 的“单文件总控”逐步拆成可维护模块，让新同学能快速定位、快速改动、低风险回归。

## 2. 不变性约束
1. 不改功能表现，不改 UI 外观，不改输入行为，不改发包语义。
2. 每一步都可单独回滚，且可用日志验证等价。
3. 先“抽接口+搬代码”，再“改实现”；禁止一步到位重写。
4. 构建产物和部署链不变，仍走 `build.bat`。

## 3. 现状耦合点
1. `dllmain.cpp` 同时承担 Hook 安装、WndProc、渲染、按钮绘制、second-child 生命周期、技能改写、资源加载。
2. 大量跨职责全局变量集中在入口文件，状态边界不清晰。
3. D3D9 与 D3D8 overlay 适配代码有重复逻辑（输入、光标、面板驱动）。
4. 路径解析逻辑在 `skill_local_data.cpp` 与 `skill_overlay_bridge.cpp` 各有一套。
5. `GameAddresses.h` 是全局常量库，但缺少 typed facade，调用点分散。

## 4. 目标分层
1. `app/orchestrator`：仅负责启动、关闭、模块编排。
2. `runtime/hooks`：仅负责 Hook 定义、安装、失败降级。
3. `runtime/features`：super button、super child、skillwnd hooks 等业务特性。
4. `render/adapters`：D3D8/D3D9 差异适配。
5. `overlay/core`：输入处理、状态同步、ImGui 画面驱动（与渲染后端解耦）。
6. `skill/domain`：技能配置、等级查询、发包改写、快捷栏绑定。
7. `platform/common`：日志、路径、Win32 安全工具、内存读写安全。

## 4.1 当前已完成的低风险拆分
| 拆分项 | 文件 | 状态 |
|---|---|---|
| 安装管线 | `src/runtime/init_pipeline.*` | 已进入构建，承接 Hook 安装顺序和失败日志 |
| 清理管线 | `src/runtime/cleanup_pipeline.*` | 已进入构建，承接 detach 清理顺序 |
| dllmain 分段 | `src/runtime/dllmain_section_*.inl` | 已从单文件视觉上拆段，但仍保持同一编译单元语义 |
| Overlay 输入工具 | `src/ui/overlay_input_utils.*` | 已抽取鼠标/键盘消息分类与坐标转换 |
| Overlay 光标工具 | `src/ui/overlay_cursor_utils.*` | 已抽取光标 suppress/show 逻辑 |
| Overlay 样式工具 | `src/ui/overlay_style_utils.*` | 已抽取 ImGui 样式和字体加载 |
| 运行路径工具 | `src/util/runtime_paths.*` | 已抽取部分运行期路径解析 |

## 5. 计划中的模块边界（建议新增文件）
| 目标模块 | 建议文件 | 主要来源 |
|---|---|---|
| RuntimeContext | `src/runtime/runtime_context.h/.cpp` | `dllmain.cpp` 全局变量与运行开关 |
| HookRegistry | `src/runtime/hook_registry.h/.cpp` | `Setup*Hook` 系列函数 |
| D3D Hooks | `src/runtime/d3d_hooks_d3d9.cpp` `src/runtime/d3d_hooks_d3d8.cpp` | `hkPresent`/`hkReset`/`hkD3D8Present` |
| SkillWnd Hooks | `src/runtime/skillwnd_hooks.cpp` | `hkSkillWnd*` 系列 |
| Super Button | `src/runtime/super_button_runtime.cpp` | `CreateSuperButton`、`hkButton*`、按钮贴图链 |
| Super Child | `src/runtime/super_child_runtime.cpp` | `CreateSuperWnd`、`MoveSuperChild*`、VT 自定义 |
| Overlay Common | `src/ui/overlay_common.cpp` | d3d8/d3d9 overlay 的输入与光标公共逻辑 |
| Path Service | `src/platform/path_service.cpp` | bridge/local_data 里的路径推导 |

## 6. 分阶段改造路线

### 阶段 0：基线固化
1. 记录当前 build marker、关键日志链、DLL hash。
2. 产出基线文档，作为后续每阶段对照。
3. 不改代码逻辑。

### 阶段 1：状态与常量分离
1. 新增 `RuntimeContext`，收拢 `dllmain.cpp` 全局状态和 feature flag。
2. `dllmain.cpp` 仅保留对 context 的访问，不改业务分支。
3. 验收：日志关键字与交互行为一致。

### 阶段 2：Hook 安装器拆分
1. 把 `Setup*Hook` 系列迁出到 `hook_registry`。
2. `InitThread` 只保留安装顺序和失败处理。
3. 验收：Hook 安装成功/失败日志与原顺序一致。

### 阶段 3：渲染钩子拆分
1. 抽离 D3D9 与 D3D8 Hook 处理到独立编译单元。
2. 保留原函数签名与日志内容，避免行为变化。
3. 验收：Present/Reset 触发频率、纹理加载日志一致。

### 阶段 4：按钮与子窗能力拆分
1. `super_button_runtime` 承接按钮资源、状态、绘制与命中逻辑。
2. `super_child_runtime` 承接 second-child 生命周期与定位逻辑。
3. `dllmain.cpp` 仅调模块 API。
4. 验收：按钮点击、hover、toggle、second-child 显示/跟随全部一致。

### 阶段 5：Overlay 共用层抽取
1. 提取 D3D8/D3D9 共有逻辑到 `overlay_common`。
2. 两个适配文件只保留后端差异代码。
3. 验收：鼠标捕获、光标隐藏、拖拽体验一致。

### 阶段 6：路径服务统一
1. 合并 bridge/local_data 中重复路径推导。
2. 用单一 `PathService` 输出 skill/config/cache 路径。
3. 验收：配置读取路径日志一致，skill 数据加载一致。

### 阶段 7：入口瘦身完成
1. `dllmain.cpp` 只保留 `DllMain`、`InitThread`、`Cleanup` 三个流程函数。
2. 其余逻辑全部在模块实现文件。
3. 验收：全链路功能与基线一致，入口文件可读性显著提升。

## 7. 每阶段统一验收清单
1. 构建通过：`cmd /c build.bat`
2. marker 可见：日志含 `[Build] marker=...`
3. Hook 就绪：日志含 `Ready` 与关键 Hook 安装结果。
4. 输入行为：overlay 内点击不穿透，外部点击不误吞。
5. UI 行为：按钮状态、面板显示、拖拽、快捷栏赋值正常。
6. 资源行为：D3D9/D3D8 下纹理与文本渲染一致。

## 8. 回滚策略
1. 每阶段独立提交，禁止跨阶段混改。
2. 只要任一验收项失败，回滚当前阶段提交。
3. 回滚后先对照日志差异，再进入下一轮拆分。

## 9. 建议执行节奏
1. 每阶段控制在 1~2 天内完成并验证。
2. 先处理“结构移动”再处理“逻辑优化”。
3. 先做 `阶段 1~3`，确保基础框架稳定，再推进按钮/子窗拆分。
