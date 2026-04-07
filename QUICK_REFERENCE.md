# SuperSkillWnd - 快速参考

## 项目结构
```
G:\code\c++\SuperSkillWnd\
├── src/
│   ├── core/                   # 核心系统
│   │   ├── GameAddresses.h     # 游戏地址定义
│   │   └── GameFunctions.h     # 游戏函数封装
│   ├── skill/                  # 技能系统
│   │   ├── SkillData.h         # 技能数据结构
│   │   └── SkillManager.h      # 技能管理器
│   ├── ui/                     # UI系统
│   │   ├── SuperSkillWnd.h     # 主窗口类
│   │   └── SuperSkillWnd.cpp   # 主窗口实现
│   ├── hook/                   # Hook系统
│   │   ├── D3D9Hook.h/cpp      # D3D9 Hook
│   │   └── WndProcHook.h/cpp   # 消息Hook
│   └── dllmain.cpp             # DLL入口
├── include/
│   └── Common.h                # 公共头文件
├── build/
│   └── build.bat               # 构建脚本
├── DESIGN_PROPOSALS.md         # 5个框架方案
├── IMPLEMENTATION.md           # 完整实现文档
└── README.md                   # 项目说明
```

## 编译
```bash
cd G:\code\c++\SuperSkillWnd\build
build.bat
```

## 使用
1. 注入 `build\Debug\SuperSkillWnd.dll` 到游戏
2. 按 **F9** 切换显示
3. 查看日志 `C:\SuperSkillWnd.log`

## 关键地址（GameAddresses.h）
```cpp
CWndMan           = 0x00F59D40  // CWnd管理器
SkillWndThis      = 0x00F6A0C0  // SkillWnd单例
CWnd_Constructor1 = 0x00B9BF60  // CWnd构造
CWnd_AddToDirty   = 0x00BA20E0  // 加入dirty链表
DrawIcon          = 0x00401C90  // 画图标
DrawText          = 0x00438250  // 画文字
```

## 关键函数
```cpp
// 创建窗口
SuperSkillWnd::Instance().Create();

// 更新位置（每帧）
SuperSkillWnd::Instance().UpdatePosition();

// 更新渲染（每帧）
SuperSkillWnd::Instance().UpdateRender();

// 切换显示
SuperSkillWnd::Instance().Toggle();

// 添加技能
SkillManager::Instance().AddSkill(tabIndex, skill);

// 使用技能
SkillManager::Instance().UseSkill(skillID);
```

## 调试
- 日志文件：`C:\SuperSkillWnd.log`
- 热键：F9切换显示
- 断点位置：
  - `SuperSkillWnd::Create()`
  - `SuperSkillWnd::CustomDraw()`
  - `SuperSkillWnd::HandleInput()`

## 待完善
1. MinHook集成（真正的Present Hook）
2. 技能拖拽实现
3. 真实技能图标加载
4. 网络通信（技能发包）
5. 配置保存/加载

## 文档
- `DESIGN_PROPOSALS.md` - 5个框架方案对比
- `IMPLEMENTATION.md` - 完整实现文档
- `README.md` - 项目说明
