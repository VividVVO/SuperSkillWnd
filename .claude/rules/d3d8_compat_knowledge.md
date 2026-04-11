---
name: D3D8 兼容性完整知识库
description: D3D8 模式检测、hook 安装、overlay 渲染方案、已知问题、vtable 索引、结构体布局、失败路线记录、黑屏修复
type: project
originSessionId: a67572ac-6f53-4e9f-b686-12f30fc2a464
---
# D3D8 兼容性完整知识库

更新时间：2026-04-11（v2 — 含黑屏诊断与修复）

## 1. 背景与需求

用户有两台电脑：
- **电脑 A**：游戏使用 D3D9（正常工作）
- **电脑 B**：游戏使用 D3D8（d3d8.dll + d3d8thk.dll 已加载），所有 D3D9 hook 失效，`g_pDevice` 永远为 null，整个 overlay UI 不工作

目标：运行时自动检测 D3D8/D3D9，D3D8 模式下 overlay 照常显示。

**Why:** 不同部署环境的游戏客户端使用不同 D3D 版本，必须两种都兼容才能让 overlay 在所有用户机器上工作。

**How to apply:** DLL 加载时检测 d3d8.dll 是否已被加载，据此走不同的 hook 路径。D3D9 路径零修改。

---

## 2. 运行时检测机制

**检测方式**（dllmain.cpp 初始化流程）：
```cpp
if (::GetModuleHandleA("d3d8.dll")) {
    g_IsD3D8Mode = true;
    SetupD3D8Hook();
} else {
    SetupD3D9Hook();  // 现有逻辑不变
}
```

**关键全局变量**（dllmain.cpp）：
- `g_IsD3D8Mode` (line ~45) — 运行时标记
- `g_D3D8GameHwnd` — 游戏窗口句柄
- `oD3D8Present` / `oD3D8Reset` — 原始 D3D8 函数指针

---

## 3. D3D8 IDirect3DDevice8 VTable 索引（已修正 2026-04-11，二次验证）

**来源**: d3d8to9 项目 d3d8.hpp 头文件，逐一与 d3d8.h STDMETHOD 声明顺序对比。

**验证方法**: d3d8to9 列表中的编号从 1 开始（IUnknown 之后），加上 IUnknown 的 3 个方法（0=QI, 1=AddRef, 2=Release），得到真实 vtable 索引 = listing_number + 2。

**重大踩坑历史**:
1. 初版多个索引错误（BeginScene=33→34, EndScene=34→35, GetRenderState=49→51, GetTextureStageState=55→62, SetVertexShader=58→76, SetTextureStageState=62→63）
2. 旧版 GetRenderState=49 实际是 GetClipPlane → 保存的 render state 值全是垃圾
3. 旧版 SetTextureStageState=62 实际是 GetTextureStageState → 把 value 当 DWORD* 写，访问 0x4 地址崩溃

**完整索引表**（已验证）：

| Index | Method | 用途 |
|-------|--------|------|
| 0 | QueryInterface | COM 标准 |
| 1 | AddRef | COM 标准 |
| 2 | Release | 释放对象 |
| 3 | TestCooperativeLevel | 设备状态检测 |
| 4 | GetAvailableTextureMem | |
| 5 | ResourceManagerDiscardBytes | |
| 6 | GetDirect3D | |
| 7 | GetDeviceCaps | |
| 8 | GetDisplayMode | |
| 9 | GetCreationParameters | |
| 10 | SetCursorProperties | |
| 11 | SetCursorPosition | |
| 12 | ShowCursor | |
| 13 | CreateAdditionalSwapChain | |
| 14 | Reset | hook 目标：设备重置 |
| 15 | Present | hook 目标：每帧渲染 |
| 16 | GetBackBuffer | |
| 17 | GetRasterStatus | |
| 18 | SetGammaRamp | |
| 19 | GetGammaRamp | |
| 20 | CreateTexture | 纹理创建 |
| 21 | CreateVolumeTexture | |
| 22 | CreateCubeTexture | |
| 23 | CreateVertexBuffer | |
| 24 | CreateIndexBuffer | |
| 25 | CreateRenderTarget | |
| 26 | CreateDepthStencilSurface | |
| 27 | CreateImageSurface | |
| 28 | CopyRects | |
| 29 | UpdateTexture | |
| 30 | GetFrontBuffer | |
| 31 | SetRenderTarget | |
| 32 | GetRenderTarget | |
| 33 | GetDepthStencilSurface | |
| 34 | BeginScene | 开始场景 |
| 35 | EndScene | 结束场景 |
| 36 | Clear | 清除 |
| 37 | SetTransform | 设置变换矩阵 |
| 38 | GetTransform | 获取变换矩阵 |
| 39 | MultiplyTransform | |
| 40 | SetViewport | 设置视口 |
| 41 | GetViewport | 获取视口 |
| 42 | SetMaterial | |
| 43 | GetMaterial | |
| 44 | SetLight | |
| 45 | GetLight | |
| 46 | LightEnable | |
| 47 | GetLightEnable | |
| 48 | SetClipPlane | |
| 49 | GetClipPlane | ⚠ 旧版误当 GetRenderState |
| 50 | SetRenderState | 设置渲染状态 |
| 51 | GetRenderState | 获取渲染状态 |
| 52 | BeginStateBlock | |
| 53 | EndStateBlock | |
| 54 | ApplyStateBlock | |
| 55 | CaptureStateBlock | ⚠ 旧版误当 GetTextureStageState |
| 56 | DeleteStateBlock | |
| 57 | CreateStateBlock | |
| 58 | SetClipStatus | ⚠ 旧版误当 SetVertexShader |
| 59 | GetClipStatus | |
| 60 | GetTexture | 获取纹理 |
| 61 | SetTexture | 设置纹理 |
| 62 | GetTextureStageState | 获取TSS ⚠ 旧版误当 SetTSS |
| 63 | SetTextureStageState | 设置TSS |
| 64 | ValidateDevice | |
| 65 | GetInfo | |
| 66 | SetPaletteEntries | |
| 67 | GetPaletteEntries | |
| 68 | SetCurrentTexturePalette | |
| 69 | GetCurrentTexturePalette | |
| 70 | DrawPrimitive | |
| 71 | DrawIndexedPrimitive | |
| 72 | DrawPrimitiveUP | 用户指针绘制 |
| 73 | DrawIndexedPrimitiveUP | |
| 74 | ProcessVertices | |
| 75 | CreateVertexShader | |
| 76 | SetVertexShader | 设置FVF（D3D8用此传FVF常量） |
| 77 | GetVertexShader | 获取FVF/shader handle |

## 4. D3D8 IDirect3D8 VTable 索引

| Index | Method | 用途 |
|-------|--------|------|
| 0 | QueryInterface | COM 标准 |
| 1 | AddRef | COM 标准 |
| 2 | Release | 释放对象 |
| 8 | GetAdapterDisplayMode | 查询显示格式（用于 BackBufferFormat） |
| 15 | CreateDevice | 创建设备 |

## 5. D3D8 IDirect3DTexture8 VTable 索引

| Index | Method | 用途 |
|-------|--------|------|
| 0 | QueryInterface | |
| 1 | AddRef | |
| 2 | Release | 释放纹理 |
| 16 | LockRect | 锁定纹理写入 |
| 17 | UnlockRect | 解锁纹理 |

---

## 6. D3D8 D3DPRESENT_PARAMETERS 结构体布局

**关键区别**：D3D8 没有 `MultiSampleQuality` 字段（D3D9 在 MultiSampleType 之后新增了它），导致后续字段偏移全不同。

```
Offset  Field                          类型
0x00    BackBufferWidth                DWORD
0x04    BackBufferHeight               DWORD
0x08    BackBufferFormat               D3DFORMAT  ← D3D8 不支持 D3DFMT_UNKNOWN(0)！
0x0C    BackBufferCount                DWORD
0x10    MultiSampleType                D3DMULTISAMPLE_TYPE
0x14    SwapEffect                     D3DSWAPEFFECT (1=DISCARD)
0x18    hDeviceWindow                  HWND
0x1C    Windowed                       BOOL (1=窗口模式)
0x20    EnableAutoDepthStencil         BOOL
0x24    AutoDepthStencilFormat         D3DFORMAT
0x28    Flags                          DWORD
0x2C    FullScreen_RefreshRateInHz     UINT
0x30    FullScreen_PresentationInterval UINT
```

**已踩坑**：`BackBufferFormat=0 (D3DFMT_UNKNOWN)` 在 D3D9 窗口模式下有效，但 **D3D8 直接拒绝**，返回 `D3DERR_INVALIDCALL (0x8876086C)`。

**正确做法**：先调 `IDirect3D8::GetAdapterDisplayMode` (vtable[8]) 获取当前显示格式，或 fallback 到 `D3DFMT_X8R8G8B8 (22)`。

---

## 7. SetupD3D8Hook 流程

1. 获取 `d3d8.dll` 模块和 `Direct3DCreate8` 函数
2. 创建 dummy 窗口 "SSWDummyD3D8"
3. `Direct3DCreate8(220)` 获取 IDirect3D8 对象（SDK version = 220）
4. 查询显示模式格式（GetAdapterDisplayMode vtable[8]），fallback `D3DFMT_X8R8G8B8`
5. 构造 D3D8 D3DPRESENT_PARAMETERS（手工填充 BYTE[64] 缓冲区）
6. `CreateDevice` (vtable[15]) 创建 dummy D3D8 设备（先 HAL，失败试 REF）
7. 从 dummy device vtable 读取 Present(15) 和 Reset(14) 的真实地址
8. Inline hook Present 和 Reset（用 InlineHook.h 的 `GenericInlineHook5`）
9. 如果 inline hook 失败，fallback 到 vtable patch
10. 释放 dummy 设备和 IDirect3D8，销毁 dummy 窗口

**注意**：
- D3D8 没有 `D3DDEVTYPE_NULLREF`，只有 HAL(1) 和 REF(2)
- vtable 类型是 `DWORD*`（x86 32位），不是 `DWORD**`（曾因此编译错误 C2440）

---

## 8. 渲染方案演进

### 8.1 方案 A：共享 HWND（v19.1）— 已失败

D3D9 overlay 设备和游戏 D3D8 设备绑定到**同一个 HWND**。

**结果**：疯狂闪屏。

**根因**：两个设备各自拥有独立 swap chain，各自 `Present` 都会把自己的 backbuffer 翻到窗口前台，互相覆盖。

**结论**：**D3D8 和 D3D9 不能共享同一个 HWND 做 Present**。

### 8.2 方案 B：透明覆盖窗口 + Color Key（v19.2）— 已放弃

创建 `WS_EX_LAYERED | WS_EX_TOPMOST | WS_EX_NOACTIVATE` 的覆盖窗口。

**已知问题**：抗锯齿打孔、全屏不可用、DWM 开销、焦点问题。

### 8.3 方案 C：直接在 D3D8 设备上画（v20.x）— 当前版本

在 hkD3D8Present 中，直接用游戏的 D3D8 设备做所有 overlay 渲染。

**关键设计**：
- 不创建 D3D9 设备
- 不用 ImGui
- 用 `DrawPrimitiveUP` + XYZRHW 顶点直接画
- 用 DWrite（实际是 GDI TextOutW）渲染文字到内存 bitmap → D3D8 纹理 → DrawPrimitiveUP
- 纹理从 DLL 嵌入资源加载（stb_image 解码 PNG → RGBA → D3D8 CreateTexture）

**每帧流程（v20.1 — 含 BeginScene/EndScene 修复）**：
```
游戏调用 D3D8 Present
    ↓
hkD3D8Present 拦截
    ↓
首次调用? → 查找游戏 HWND
    ↓
纹理加载（一次性）
    ↓
面板展开时：
  1. D3D8_SaveRenderState() — 保存 RS/TSS/Texture/VS/Viewport/Transforms
  2. BeginScene() — 重新开启场景（游戏已经调过 EndScene）
  3. D3D8_SetOverlayRenderState() — 设置 alpha blend 等
  4. 同步面板数据 (SkillOverlayBridgeSyncRetroState)
  5. D3D8_RenderOverlayPanel() — 画面板
  6. EndScene()
  7. D3D8_RestoreRenderState() — 恢复所有游戏状态
    ↓
调用原始 D3D8 Present
```

---

## 9. 已踩坑记录

### 9.1 BackBufferFormat = D3DFMT_UNKNOWN (0)
- **现象**：D3D8 dummy device 创建失败，`hr=0x8876086C (D3DERR_INVALIDCALL)`
- **根因**：D3D8 不接受 `D3DFMT_UNKNOWN`，D3D9 接受
- **修复**：查询 GetAdapterDisplayMode 或 fallback `D3DFMT_X8R8G8B8 (22)`

### 9.2 vtable 类型 DWORD** vs DWORD*
- **现象**：编译错误 C2440，DWORD* 到 DWORD 的转换
- **根因**：x86 下 vtable 条目是 `DWORD`（4 字节指针），应声明为 `DWORD*`，不是 `DWORD**`
- **修复**：`DWORD* vtable = *(DWORD**)pDevice`

### 9.3 共享 HWND 闪屏
- **现象**：一黑一黑疯狂闪烁
- **根因**：两个 D3D 设备各自 Present 互相覆盖 swap chain
- **修复**：放弃方案 A，改用方案 C（直接在 D3D8 设备上画）

### 9.4 VTable 索引全错（2026-04-11 修复）
- **现象**：点击按钮后崩溃于 d3d8.514152CB
- **根因**：6 个 vtable 索引全错（来自错误的 D3D9 映射或猜测）
  - BeginScene 33→34, EndScene 34→35
  - GetRenderState 49→51 (49=GetClipPlane!)
  - GetTextureStageState 55→62 (55=CaptureStateBlock!)
  - SetVertexShader 58→76 (58=SetClipStatus!)
  - SetTextureStageState 62→63 (62=GetTextureStageState!)
- **崩溃根因详析**：调 SetTextureStageState(dev, 0, TSS_COLOROP, TOP_MODULATE) 时，实际调到了 GetTextureStageState(dev, 0, TSS_COLOROP, &outValue)。因为 TOP_MODULATE=4，GetTSS 把 4 当指针写入 → 访问 0x00000004 → 崩溃
- **修复**：d3d8_renderer.h D3D8VT namespace 全部索引修正

### 9.5 黑屏 + 控件错位（2026-04-11 诊断修复）
- **现象**：vtable 修复后不再崩溃，但点击按钮后游戏黑屏，左下角技能窗消失，控件错位
- **根因分析（多因）**：
  1. **GetVertexShader 未保存**：savedVS 始终为 0，恢复时把游戏 FVF/shader 清零
  2. **Viewport 未保存/恢复**：overlay 可能改变内部 viewport 状态
  3. **Transform 未保存/恢复**：虽然 XYZRHW 绕过 transform pipeline，但 DrawPrimitiveUP 可能修改内部状态
  4. **没有 BeginScene/EndScene**：游戏已经调过 EndScene，我们在 EndScene 和 Present 之间画，D3D8 可能忽略所有 draw calls
- **修复（v20.1）**：
  1. 添加 GetVertexShader (vtable[77]) 保存/恢复
  2. 添加 GetViewport/SetViewport (vtable[41/40]) 保存/恢复
  3. 添加 GetTransform/SetTransform (vtable[38/37]) 保存/恢复 VIEW/PROJECTION/WORLD
  4. 在 overlay 渲染前后加 BeginScene/EndScene 包装

---

## 10. D3D8 Render State 保存/恢复完整清单（v20.1）

### 保存的 Render States (11 个)：
| RS 枚举 | 值 | 说明 |
|---------|-----|------|
| RS_ZENABLE | 7 | Z 缓冲 |
| RS_ZWRITEENABLE | 14 | Z 写入 |
| RS_ALPHATESTENABLE | 15 | Alpha 测试 |
| RS_SRCBLEND | 19 | 源混合 |
| RS_DESTBLEND | 20 | 目标混合 |
| RS_CULLMODE | 22 | 裁剪模式 |
| RS_ALPHABLENDENABLE | 27 | Alpha 混合 |
| RS_FOGENABLE | 28 | 雾 |
| RS_LIGHTING | 137 | 光照 |
| RS_COLORWRITEENABLE | 168 | 颜色通道写入 |
| RS_BLENDOP | 171 | 混合操作 |

### 保存的 TSS Stage 0 (11 个)：
TSS_COLOROP, TSS_COLORARG1, TSS_COLORARG2, TSS_ALPHAOP, TSS_ALPHAARG1, TSS_ALPHAARG2, TSS_MINFILTER, TSS_MAGFILTER, TSS_MIPFILTER, TSS_ADDRESSU, TSS_ADDRESSV

### 保存的 TSS Stage 1 (2 个)：
TSS_COLOROP, TSS_ALPHAOP

### 其他保存项：
- Texture stage 0 (GetTexture/SetTexture)
- Vertex Shader / FVF (GetVertexShader/SetVertexShader)
- Viewport (GetViewport/SetViewport)
- Transform D3DTS_VIEW=2
- Transform D3DTS_PROJECTION=3
- Transform D3DTS_WORLD=256

---

## 11. Hook 引擎技术细节

### InlineHook.h
- `FollowJmpChain()` — 跟随最多 16 层 JMP 链找到真实入口
- `CalcMinCopyLen()` — 用 hde32 计算安全拷贝长度（≥5 字节）
- `GenericInlineHook5()` — 5 字节 inline hook：复制原始指令到 trampoline，覆写入口为 `JMP hookFunc`
- `InstallInlineHook()` — 完整版：自动 FollowJmpChain + CalculateRelocatedByteCount + hook

### hde32.h
- 表驱动 x86-32 指令长度解码器
- 支持所有标准指令前缀、ModR/M、SIB、displacement、immediate
- 返回 0 表示未知指令

---

## 12. D3D8 纹理管理

### 创建流程
```
stb_image_load_from_memory(PNG) → RGBA buffer
    ↓
IDirect3DDevice8::CreateTexture(w, h, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED)
    ↓
IDirect3DTexture8::LockRect(0, &locked, NULL, 0)
    ↓
RGBA → BGRA 通道交换（D3DFMT_A8R8G8B8 内存布局是 BGRA）
    ↓
IDirect3DTexture8::UnlockRect(0)
```

### 当前加载的纹理（9 个面板资源 + 动态技能 icon + 文字缓存）
- `g_d3d8PanelBg` — 面板背景
- `g_d3d8BtnNormal/Hover/Pressed/Disabled` — 4 个按钮状态
- `g_d3d8CursorNormal/1/2/3` — 4 个鼠标帧
- 技能 icon 纹理 — 按需从 DLL 资源加载
- 文字纹理缓存 — GDI TextOutW → D3D8 纹理，用 std::map<string, D3D8Texture> 缓存

---

## 13. D3D8 文字渲染实现

**命名为 DWrite 但实际是 GDI 方案**：

```
CreateCompatibleDC → CreateDIBSection(ARGB32)
    ↓
SelectObject(hFont) → SetBkMode(TRANSPARENT) → SetTextColor(white)
    ↓
TextOutW(hdc, 0, 0, text, len) → GDI 渲染到 DIB
    ↓
读取 DIB 像素 → 应用 color tint → D3D8_CreateTextureFromRGBA
    ↓
缓存到 D3D8TextCache::cache[key]
```

**文字测量**：`GetTextExtentPoint32W` 获取精确像素宽高。

---

## 14. 关键常量速查

| 常量 | 值 | 说明 |
|------|-----|------|
| D3D8 SDK Version | 220 | Direct3DCreate8 参数 |
| D3DFMT_X8R8G8B8 | 22 | BackBufferFormat fallback |
| D3DFMT_A8R8G8B8 | 21 | 纹理格式 |
| D3DFMT_UNKNOWN | 0 | D3D9 支持，D3D8 不支持 |
| D3DSWAPEFFECT_DISCARD | 1 | SwapEffect |
| D3DDEVTYPE_HAL | 1 | 硬件加速 |
| D3DDEVTYPE_REF | 2 | 软件参考 |
| D3DCREATE_SOFTWARE_VERTEXPROCESSING | 0x20 | 创建标志 |
| D3DPOOL_MANAGED | 1 | 纹理池 |
| D3DERR_INVALIDCALL | 0x8876086C | 参数错误 |
| D3DTS_VIEW | 2 | 视图变换 |
| D3DTS_PROJECTION | 3 | 投影变换 |
| D3DTS_WORLD | 256 | 世界变换 |
| FVF_TLVERTEX | 0x144 | XYZRHW\|DIFFUSE\|TEX1 |
| FVF_SOLIDVERTEX | 0x044 | XYZRHW\|DIFFUSE |

---

## 15. ToggleSuperWnd D3D8 路径

在 dllmain.cpp 的 `ToggleSuperWnd()` 中，D3D8 模式有独立旁路（lines 4481-4483）：
```cpp
if (g_IsD3D8Mode) {
    g_SuperExpanded = !g_SuperExpanded;
    return;  // D3D8 模式不走 ImGui/CreateSuperWnd
}
```

D3D8 模式下面板的 show/hide 纯靠 `g_SuperExpanded` 标志，由 `hkD3D8Present` 每帧检查。

---

## 16. 版本历史

| 版本 | 日期 | 改动 |
|------|------|------|
| v19.1 | 2026-04-10 | 方案 A：共享 HWND → 闪屏，放弃 |
| v19.2 | 2026-04-10 | 方案 B：透明覆盖窗口 → 有硬伤，放弃 |
| v20.0 | 2026-04-11 | 方案 C：D3D8 直接渲染，vtable 索引错误导致崩溃 |
| v20.0-fix | 2026-04-11 | 修正 6 个 vtable 索引，不再崩溃，但黑屏 |
| v20.1 | 2026-04-11 | 修复黑屏：添加 VS/Viewport/Transform 保存恢复 + BeginScene/EndScene |

---

## 17. 待解决问题清单

1. **v20.1 黑屏修复是否生效** — 用户尚未部署验证
2. **面板控件是否正确显示** — 纹理加载日志显示成功，但用户报告 "按钮没显示" + "控件错位"
3. **D3D8 电脑是窗口模式还是全屏** — 决定某些方案是否可行
4. **文字渲染质量** — GDI TextOutW 在小字号下的清晰度
5. **鼠标输入** — D3D8 模式下的按钮点击和鼠标命中检测
6. **性能** — 每帧 save/restore 大量状态的开销

---

## 18. 日志关键字速查

D3D8 模式排查时搜索：
- `[D3D] Detected D3D8 mode` — 模式检测
- `[D3D8] Setup` — hook 安装开始
- `[D3D8] display mode:` — 显示模式查询结果
- `[D3D8] dummy device creation failed` — dummy 设备创建失败
- `[D3D8] Present inline hooked` — Present hook 安装成功
- `[D3D8] Hook setup complete` — 全部 hook 安装完成
- `[D3D8] first Present: hwnd=` — 首帧获取窗口句柄
- `[D3D8-Tex] loaded` — 纹理加载结果
- `[D3D8-Present] SkillWnd:` — 技能窗口指针获取
- `[D3D8-Present] Ready` — 初始化就绪
- `[D3D8] Reset called` — 设备重置事件
- `[Toggle:d3d_btn] expanded=` — 面板开关状态
- `[Toggle] D3D8 mode: panel ON/OFF` — 面板切换
- `[Build] marker=v20.1-*` — 版本确认
- `D3D8 hook FAILED` — 整体失败

---

## 19. D3D8 vs D3D9 关键差异速查

| 特性 | D3D8 | D3D9 |
|------|------|------|
| SetFVF | 不存在，用 SetVertexShader(FVF) | 存在 |
| SetSamplerState | 不存在，通过 SetTextureStageState 设置 | 存在 |
| StateBlock API | CreateStateBlock(56)/ApplyStateBlock(54)/CaptureStateBlock(55)/DeleteStateBlock(56) | 不同 API |
| CreateTexture 参数 | 无 pSharedHandle | 有 pSharedHandle |
| D3DPRESENT_PARAMETERS | 无 MultiSampleQuality | 有 MultiSampleQuality |
| BackBufferFormat | 不接受 D3DFMT_UNKNOWN | 接受 |
| GetVertexShader | vtable[77]，返回 FVF 或 shader handle | 不同位置 |
| TSS 采样器 | TSS_MINFILTER=17, TSS_MAGFILTER=16, TSS_ADDRESSU=13, TSS_ADDRESSV=14 | 独立 SamplerState API |
