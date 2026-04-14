# SuperSkillWnd

MapleStory 客户端注入 DLL 项目。当前版本同时支持 D3D9 和 D3D8 路线，提供 Super Skill 面板、原生按钮/子窗、技能桥接、快捷栏拖拽等能力。

## 当前状态
- 主入口与运行时编排在 `src/dllmain.cpp`（当前约 7800+ 行）。
- 核心业务已拆出到 `src/skill`、`src/ui`、`src/d3d8`、`src/hook`，但初始化与 Hook 编排仍高度集中在入口文件。
- 构建方式为 `build.bat` + MSVC 命令行，不依赖 `.vcxproj`。

## 先读这两份文档
- 项目纲要：`PROJECT_OUTLINE.md`
- 去耦/模块化路线：`MODULARIZATION_PLAN.md`

## 真实目录结构（当前）
```text
SuperSkillWnd/
├─ build.bat
├─ src/
│  ├─ core/          # Common, GameAddresses
│  ├─ hook/          # InlineHook, Win32 输入欺骗
│  ├─ skill/         # Skill 本地数据、bridge、source
│  ├─ ui/            # ImGui overlay、retro panel、DWrite 文本
│  ├─ d3d8/          # D3D8 渲染适配
│  ├─ runtime/       # 初始化/清理管线与 dllmain 分段
│  ├─ util/          # 路径等运行期工具
│  ├─ experimental/  # 运行期探针
│  ├─ third_party/   # imgui
│  └─ dllmain.cpp    # 运行时总编排
├─ tools/
│  └─ SkillImgReader/
└─ docs/
```

## 构建
```powershell
cmd /c build.bat
```

产物默认输出到：
- `build\Debug\SS.dll`
- `build\Debug\hook.dll`

## 运行排查建议
请按 `.claude/rules/CLAUDE.md` 的顺序执行：
1. 先确认版本与部署。
2. 再看日志关键字。
3. 再进源码 / IDA / x32dbg / dump。
