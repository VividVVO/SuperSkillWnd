# 超级技能栏项目

## 项目概述
完整复刻MapleStory技能窗口，作为独立的"超级技能栏"，吸附在原生SkillWnd左侧。

## 架构选择
**方案2：轻量级CWnd + 游戏控件**
- 基于游戏原生CWnd系统
- 复用游戏控件和资源
- 自定义技能数据管理
- 支持完整的拖拽、点击、页签功能

## 目录结构
```
SuperSkillWnd/
├── src/
│   ├── core/           # 核心系统
│   │   ├── CWndWrapper.h/cpp       # CWnd封装
│   │   ├── GameAddresses.h         # 游戏地址定义
│   │   └── GameFunctions.h/cpp     # 游戏函数调用
│   ├── skill/          # 技能系统
│   │   ├── SkillData.h/cpp         # 技能数据结构
│   │   ├── SkillManager.h/cpp      # 技能管理器
│   │   └── SkillRenderer.h/cpp     # 技能渲染
│   ├── ui/             # UI系统
│   │   ├── SuperSkillWnd.h/cpp     # 主窗口类
│   │   ├── SkillListView.h/cpp     # 技能列表视图
│   │   └── TabControl.h/cpp        # 页签控件
│   ├── input/          # 输入系统
│   │   ├── InputHandler.h/cpp      # 输入处理
│   │   └── DragDrop.h/cpp          # 拖拽系统
│   ├── hook/           # Hook系统
│   │   ├── D3D9Hook.h/cpp          # D3D9 Hook
│   │   ├── WndProcHook.h/cpp       # 消息Hook
│   │   └── SkillWndHook.h/cpp      # SkillWnd Hook
│   └── dllmain.cpp     # DLL入口
├── include/            # 公共头文件
│   └── Common.h
├── resources/          # 资源文件
│   └── textures/
├── build/              # 构建脚本
│   └── build.bat
└── README.md
```

## 技术栈
- C++17
- Win32 API
- Direct3D9
- Inline Hook (MinHook)

## 编译要求
- Visual Studio 2019+
- Windows SDK 10.0+
- DirectX SDK (June 2010)
