#pragma once
//
// GameAddresses.h — 所有游戏函数/全局变量地址（基于CLAUDE001.md逆向分析）
// 纯常量头文件，无依赖
//
#include <windows.h>

// ============================================================================
// 全局变量地址
// ============================================================================
const DWORD ADDR_CWndMan         = 0x00F59D40;  // CWnd管理器单例
const DWORD ADDR_ZOrderList      = 0x00F57410;  // off_F57410 全局 z/order list header
const DWORD ADDR_ZOrderListHead  = 0x00F5741C;  // off_F57410 head cursor
const DWORD ADDR_ZOrderListTail  = 0x00F57420;  // off_F57410 tail cursor / 鼠标命中全局扫描起点
const DWORD ADDR_PendingList     = 0x00F57424;  // off_F57424 全局 list header（语义待运行期闭环）
const DWORD ADDR_PendingListHead = 0x00F57430;  // off_F57424 head cursor
const DWORD ADDR_PendingListTail = 0x00F57434;  // off_F57424 tail cursor
const DWORD ADDR_DirtyList       = 0x00F57438;  // off_F57438 dirty list header
const DWORD ADDR_DirtyListHead   = 0x00F57444;  // off_F57438 head cursor
const DWORD ADDR_DirtyListTail   = 0x00F57448;  // off_F57438 tail cursor
const DWORD ADDR_InteractList    = ADDR_ZOrderListTail;  // 兼容旧名：不是独立 interact list
const DWORD ADDR_CanvasFactory   = 0x00F6A848;  // Canvas/图形工厂(COM)
const DWORD ADDR_SkillWndEx      = 0x00F6A0C0;  // SkillWndEx全局单例指针
const DWORD ADDR_CursorManager   = 0x00F5EB14;  // 原生鼠标状态管理器单例（sub_5F3EC0 this）
const DWORD ADDR_GameHeap        = 0x00F68F50;  // gameMalloc 堆句柄
const DWORD ADDR_SkillDataMgr    = 0x00F59D38;  // SkillDataMgr全局指针

// ============================================================================
// 技能等级查询函数（A级证据，IDA asm 直接确认）
// ============================================================================
// sub_7DA7D0: __thiscall(ECX=SkillDataMgr, push playerObj, push skillId, push cachePtr), retn 0Ch
const DWORD ADDR_7DA7D0 = 0x007DA7D0;  // 基础等级
// sub_7DBC50: __thiscall(ECX=SkillDataMgr, push playerObj, push skillId, push cachePtr, push flags), retn 10h
const DWORD ADDR_7DBC50 = 0x007DBC50;  // 当前等级（含加成）
// sub_5511C0: __thiscall(ECX=skillEntryObj), retn, 无栈参数
const DWORD ADDR_5511C0 = 0x005511C0;  // 最大等级（需先用 sub_7DA4B0 获取 skillEntry）
// sub_7DA4B0: __thiscall(ECX=SkillDataMgr, push skillId), 查 skillEntry 对象
const DWORD ADDR_7DA4B0 = 0x007DA4B0;
// sub_BCE3C0: __thiscall(ECX=SkillWndEx, push rowData), retn 4
const DWORD ADDR_BCE3C0 = 0x00BCE3C0;  // 原生“是否可升级”判断，返回 1/0/-1
// sub_BF43E0: __thiscall(ECX=CWndMan, push skillId), retn 4
const DWORD ADDR_BF43E0 = 0x00BF43E0;  // 原生技能升级请求/发包
// sub_9DB640: __thiscall(ECX=SkillWndEx, push relativeRowIndex), retn 4
const DWORD ADDR_9DB640 = 0x009DB640;  // 原生技能加点入口（SkillWndEx row handler）
const DWORD ADDR_9DBEB0 = 0x009DBEB0;  // 原生技能列表长度/滚动条刷新
const DWORD ADDR_9DBF20 = 0x009DBF20;  // 原生技能行按钮/可升级状态刷新
// CWndMan+0x20C8 = 玩家角色对象指针
const DWORD CWNDMAN_PLAYER_OFF = 0x20C8;

// ============================================================================
// CWnd构造链函数
// ============================================================================
const DWORD ADDR_B9BF60 = 0x00B9BF60;  // CWnd基础构造
const DWORD ADDR_A996B0 = 0x00A996B0;  // CWnd中间层构造(7参数)
const DWORD ADDR_A99330 = 0x00A99330;  // 坐标初始化+创建COM surface
const DWORD ADDR_B9A660 = 0x00B9A660;  // 核心初始化(10参数)
const DWORD ADDR_B9AB50 = 0x00B9AB50;  // 重建/重设surface尺寸(__thiscall, 8参数)
const DWORD ADDR_B9A5D0 = 0x00B9A5D0;  // 标记脏/注册渲染链表
const DWORD ADDR_B9EEA0 = 0x00B9EEA0;  // 输入/layer manager active/focus 同步(__thiscall ecx=dword_F5E8D4, push wnd+4)
const DWORD ADDR_B9F570 = 0x00B9F570;  // 鼠标命中总入口(__userpurge ecx=dword_F5E8D4, ...)
const DWORD ADDR_BA0680 = 0x00BA0680;  // 输入/layer manager remove/recalc(__thiscall ecx=dword_F5E8D4, push wnd+4)
const DWORD ADDR_BA1E80 = 0x00BA1E80;  // 插入全局 z/order 链
const DWORD ADDR_BA20E0 = 0x00BA20E0;  // 加入dirty链表
const DWORD ADDR_8A25A0 = 0x008A25A0;  // vector push_back
const DWORD ADDR_B9E880 = 0x00B9E880;  // CWnd关闭

// ============================================================================
// 渲染函数
// ============================================================================
const DWORD ADDR_435A50 = 0x00435A50;  // 获取渲染surface(__thiscall)
const DWORD ADDR_4016D0 = 0x004016D0;  // 拷贝默认VARIANT到局部(__cdecl)
const DWORD ADDR_40CA00 = 0x0040CA00;  // QueryInterface样式包装，取可绘制对象
const DWORD ADDR_401C90 = 0x00401C90;  // 画图标(COM转发)
const DWORD ADDR_5F3EC0 = 0x005F3EC0;  // 原生鼠标状态切换器：维护 cursorManager+625 当前状态并切换资源
const DWORD ADDR_401990 = 0x00401990;  // 从VARIANT取对象(__thiscall, push 0,0)
const DWORD ADDR_4027F0 = 0x004027F0;  // 将资源对象QueryInterface后写入槽位(__thiscall)
const DWORD ADDR_402F60 = 0x00402F60;  // 宽字符串 -> 游戏字符串对象(__thiscall, retn 4)
const DWORD ADDR_404D90 = 0x00404D90;  // 从资源根解析资源(__thiscall, retn 10h)
const DWORD ADDR_438250 = 0x00438250;  // 画文字(COM转发, __thiscall)
const DWORD ADDR_435BA0 = 0x00435BA0;  // 创建游戏字符串对象
const DWORD ADDR_403A10 = 0x00403A10;  // 释放字符串对象
const DWORD ADDR_5000E520 = 0x5000E520;  // 字形缓存查/建(__thiscall ecx=FontCache, push ch, push outRectOrNull)
const DWORD ADDR_5000E640 = 0x5000E640;  // 文本逐字排版主循环
const DWORD ADDR_5000B960 = 0x5000B960;  // glyph 提交到布局/绘制目标
const DWORD ADDR_5000BBC0 = 0x5000BBC0;  // glyph miss 生成单字
const DWORD ADDR_5000BD20 = 0x5000BD20;  // 16bpp 写 atlas

// ============================================================================
// COM Region操作（函数指针存放地址）
// ============================================================================
const DWORD ADDR_F6707C_PTR = 0x00F6707C;  // region resize
const DWORD ADDR_F67078_PTR = 0x00F67078;  // region copy

// ============================================================================
// VTable地址（case19 的 vtable）
// ============================================================================
const DWORD ADDR_VT1_CASE19 = 0x00E57980;
const DWORD ADDR_VT2_CASE19 = 0x00E57928;
const DWORD ADDR_VT3_CASE19 = 0x00E57924;

// SkillWnd VTable
const DWORD ADDR_SkillWndVT = 0x00E66F50;

// MacroWnd (case32) VTable
const DWORD ADDR_VT1_CASE32 = 0x00E67660;
const DWORD ADDR_VT2_CASE32 = 0x00E67608;
const DWORD ADDR_VT3_CASE32 = 0x00E67600;

// MacroWnd全局指针
const DWORD ADDR_MacroWndGlobal = 0x00F6A120;

// CButton VTable
const DWORD ADDR_CButtonVT1 = 0x00E0C354;
const DWORD ADDR_CButtonVT2 = 0x00E0C300;
const DWORD ADDR_CButtonVT3 = 0x00E0C2FC;

// CButton构造函数
const DWORD ADDR_522EA0 = 0x00522EA0;  // CButton构造 (__thiscall)

// ============================================================================
// 原生按钮/控件创建（复刻BtMacro模式）
// ============================================================================
const DWORD ADDR_66A770 = 0x0066A770;  // 控件创建函数(__thiscall, ECX=控件容器)
const DWORD ADDR_6688B0 = 0x006688B0;  // 控件容器初始化(设parent,x,y)

// 资源路径指针（off_表地址，指向WZ资源路径字符串）
const DWORD ADDR_OFF_BtMacro = 0x00E67208;  // → "/UIWindow2.img/Skill/main/BtMacro"
const DWORD ADDR_OFF_SkillEx_BtMacro = 0x00E67BE8;  // → "/UIWindow2.img/SkillEx/main/BtMacro"

// 我们的自定义控件ID
const DWORD SUPER_BTN_ID = 0x7F0;  // 2032, 自定义ID；点击通过WndProc兜底保障

// MacroWnd 构造
const DWORD ADDR_9DAF50 = 0x009DAF50;  // MacroWnd构造(__thiscall, push yStr, push xPos)

// SkillWndEx消息处理
const DWORD ADDR_9ECFD0 = 0x009ECFD0;  // 消息处理(__thiscall ecx=this, push ctrlID, retn 4)
const DWORD ADDR_A99550 = 0x00A99550;  // 消息消费确认(push ctrlID)
const DWORD ADDR_9DDB30 = 0x009DDB30;  // SkillWndEx真实控件消息分发(__thiscall ecx=this, push ctrlID, retn 4)
const DWORD ADDR_9D95A0 = 0x009D95A0;  // SkillWndEx父窗移动时同步 Macro child (__thiscall, retn 8)
const DWORD ADDR_9D97E0 = 0x009D97E0;  // SkillWndEx 主动关闭 official second-child
const DWORD ADDR_9D98F0 = 0x009D98F0;  // SkillWnd official second-child 鼠标消息处理
const DWORD ADDR_9D9970 = 0x009D9970;  // SkillWndEx close：关闭 MacroWnd/second-child 并从 top-level vector 移除
const DWORD ADDR_9DA4E0 = 0x009DA4E0;  // SkillWndEx second-child wrapper assign helper
const DWORD ADDR_9E1770 = 0x009E1770;  // SkillWndEx刷新 helper (__thiscall, retn)
const DWORD ADDR_56D630 = 0x0056D630;  // 原生 child move API (__thiscall ecx=child, push x, y)
const DWORD ADDR_9DC220 = 0x009DC220;  // SkillWndEx 官方 second-child create/replace 包装链 (__thiscall, push mode)
const DWORD ADDR_9D93A0 = 0x009D93A0;  // SkillWndEx second-child release helper (__thiscall ecx=this+3044, push 0)
const DWORD ADDR_VT_SkillWndSecondChild1 = 0x00E66C00; // 9DB2B0 official 0x84 child VT1
const DWORD ADDR_VT_SkillWndSecondChild2 = 0x00E66BA8; // 9DB2B0 official 0x84 child VT2
const DWORD ADDR_VT_SkillWndSecondChild3 = 0x00E66BA4; // 9DB2B0 official 0x84 child VT3

// donor child（历史路线，52A670 已证明不是长期底壳）
const DWORD ADDR_52A670 = 0x0052A670;  // donor child ctor (__thiscall)
const DWORD ADDR_52AA90 = 0x0052AA90;  // donor child draw (__thiscall ecx=this, push clip, retn 4)

// A996B0-family 轻量 carrier（当前 route-B 主路线）
const DWORD ADDR_976A60 = 0x00976A60;  // 轻量 A996B0-family child ctor (__thiscall)
const DWORD ADDR_9DB2B0 = 0x009DB2B0;  // SkillWnd 第二副窗 donor ctor (__thiscall, a2=模式值)

// ============================================================================
// SkillWnd函数
// ============================================================================
const DWORD ADDR_9DEE30 = 0x009DEE30;  // SkillWndEx绘制(__thiscall ecx=this, push clip), int, retn 4
const DWORD ADDR_9E14D0 = 0x009E14D0;  // SkillWndEx析构/关闭(__thiscall ecx=this), int, retn
const DWORD ADDR_9E17D0 = 0x009E17D0;  // SkillWndEx子控件初始化
const DWORD ADDR_50A6A0 = 0x0050A6A0;  // 子控件构造函数(vtable+48)
const DWORD ADDR_5095A0 = 0x005095A0;  // CButton状态刷新/切换(__thiscall ecx=this, push stateIndex)
const DWORD ADDR_50AEB0 = 0x0050AEB0;  // CButton移动surface(__thiscall ecx=this, push x, y)
const DWORD ADDR_506EE0 = 0x00506EE0;  // CButton当前状态资源 -> 可绘制对象解析(__thiscall ecx=this, push outObj)
const DWORD ADDR_507020 = 0x00507020;  // CButton当前状态图绘制(__thiscall ecx=this, push x, y, layerArg)
const DWORD ADDR_507DF0 = 0x00507DF0;  // CButton测量 helper A（当前状态相关，返回差值）
const DWORD ADDR_507ED0 = 0x00507ED0;  // CButton测量 helper B（当前状态相关，返回差值）
const DWORD ADDR_48CD30 = 0x0048CD30;  // 子控件构造

// gameMalloc (__thiscall, ECX = 0x00F68F50)
const DWORD ADDR_401FA0 = 0x00401FA0;

// ============================================================================
// 新增地址（CWnd创建所需）
// ============================================================================
const DWORD ADDR_F5E5CC    = 0x00F5E5CC;  // 坐标配置表全局对象
const DWORD ADDR_F5E8D4    = 0x00F5E8D4;  // 输入/layer管理器全局
const DWORD ADDR_F692F8    = 0x00F692F8;  // case19窗口全局指针
const DWORD ADDR_F6A84C    = 0x00F6A84C;  // 资源根对象（sub_404D90 的 ecx）
const DWORD ADDR_pvargSrc  = 0x00F58548;  // 默认VARIANT参数源
const DWORD ADDR_F671CC_PTR = 0x00F671CC; // 自定义字符串释放函数指针
const DWORD ADDR_E06B34    = 0x00E06B34;  // 图片对象 QueryInterface IID
// 注意：sub_402F60 在原生调用链里实际更像使用 "UI/UIWindow2..." 形式的宽串。
// strings_index 命中的是从 '/' 开始的子串，但该宽串前面还多了 2 个 WCHAR: "UI"。
const DWORD ADDR_STR_SkillMainBackgrnd = 0x00E67298; // 推定 L"UI/UIWindow2.img/Skill/main/backgrnd"
const DWORD ADDR_STR_SkillMacroBackgrnd = 0x00E66C88; // L"UI/UIWindow2.img/Skill/macro/backgrnd"
const DWORD ADDR_STR_SkillExMainBackgrnd = 0x00E67CD0; // L"UI/UIWindow2.img/SkillEx/main/backgrnd"
const DWORD ADDR_BD08A0    = 0x00BD08A0;  // CWndMan注册函数(__thiscall)
const DWORD ADDR_B9B800    = 0x00B9B800;  // CWnd背景绘制(__thiscall)
const DWORD ADDR_B9F6E0    = 0x00B9F6E0;  // RenderAll
const DWORD ADDR_BBC965    = 0x00BBC965;  // sub_BBC460: call sub_B9F6E0 之后
const DWORD ADDR_BBC96E    = 0x00BBC96E;  // sub_BBC460: post-B9F6E0 续接点（call sub_B9A400）

// ============================================================================
// CWnd对象偏移（DWORD index, 实际字节 = index * 4）
// ============================================================================
const int CWND_OFF_VT1      = 0;    // +0x00 主vtable
const int CWND_OFF_VT2      = 1;    // +0x04 第二vtable
const int CWND_OFF_VT3      = 2;    // +0x08 第三vtable
const int CWND_OFF_REFCNT   = 3;    // +0x0C 引用计数
const int CWND_OFF_WNDID    = 5;    // +0x14 窗口ID
const int CWND_OFF_COM      = 6;    // +0x18 COM Surface对象
const int CWND_OFF_W        = 10;   // +0x28 宽度
const int CWND_OFF_H        = 11;   // +0x2C 高度
const int CWND_OFF_ZORDER   = 32;   // +0x80 z-order/layer
const int CWND_HOME_X_OFF   = 2756; // home X坐标（字节偏移）
const int CWND_HOME_Y_OFF   = 2760; // home Y坐标（字节偏移）

// COM Surface偏移
const int COM_OFF_X = 0x54;  // 屏幕X
const int COM_OFF_Y = 0x58;  // 屏幕Y

// ============================================================================
// SkillWndEx对象偏移（DWORD index）
// ============================================================================
const int SW_OFF_ICON       = 721;  // 图标句柄
const int SW_OFF_DESC_FONT  = 726;  // 描述字体
const int SW_OFF_NAME_FONT  = 729;  // 名称字体

// ============================================================================
// CWndMan内部偏移
// ============================================================================
const DWORD CWNDMAN_TOPLEVEL_OFF = 19060;  // 0x4A74 toplevel窗口vector data指针
const DWORD CWNDMAN_CASE32_OFF   = 18224;  // MacroWnd注册slot偏移（sub_BD0620参数）

// CWnd注册到CWndMan（引用计数+存储指针）
const DWORD ADDR_BD0620 = 0x00BD0620;  // __thiscall(ECX=&slot, push cwnd), retn 4

// ============================================================================
// 快捷栏（Quickslot）赋值相关
// ============================================================================
// 全局指针：键位映射数组（89项×5字节: [type:1][skillId:4]）
const DWORD ADDR_KeyArrayPtr    = 0x00F617A8;  // *(DWORD*)此地址 = 数组基址
// 全局指针：slot→key_index 映射表（DWORD数组，[1]~[8]对应 slot 0~7）
const DWORD ADDR_SlotTablePtr   = 0x00F619D0;  // *(DWORD*)此地址 = 映射表基址
// StatusBar 全局单例
const DWORD ADDR_StatusBar      = 0x00F6A18C;  // *(DWORD*)此地址 = StatusBar this
const DWORD ADDR_StatusBarRefreshInternal      = 0x009F5FE0;  // StatusBar 内部刷新入口：先调 9F4F00/9F4C30，再 B9A5D0(self/child)
const DWORD ADDR_StatusBarRefreshSlotsPrimary  = 0x009F4F00;  // StatusBar 内部槽刷新 A：遍历 +0xAE8 的 6 个槽和 +0xB18 的 3 个槽
const DWORD ADDR_StatusBarRefreshSlotsSecondary= 0x009F4C30;  // StatusBar 内部槽刷新 B：遍历 +0xAE8 的 6 个槽和 +0xB18 的 3 个槽
const DWORD ADDR_StatusBarCleanupTransient     = 0x009FCAE0;  // StatusBar 临时 child / wrapper 清理入口（涉及 +0xB2C / +0xB30）
const DWORD ADDR_856879         = 0x00856879;  // 客户端本地属性重算-直加属性潜能链入口(flat stats potential path)
const DWORD ADDR_85688E         = 0x0085688E;  // 856879 hook continuation when eax != 0
const DWORD ADDR_856B7C         = 0x00856B7C;  // 856879 hook continuation when eax == 0
const DWORD ADDR_853B49         = 0x00853B49;  // 客户端本地属性重算-基础平属性潜能链入口(2/3/4/5/8/9)
const DWORD ADDR_853B61         = 0x00853B61;  // 853B49 hook continuation when eax != 0
const DWORD ADDR_853DED         = 0x00853DED;  // 853B49 hook continuation when eax == 0
const DWORD ADDR_853E5A         = 0x00853E5A;  // 客户端本地属性重算-百分比属性链入口(18/19/20/21/22/23)
const DWORD ADDR_853E73         = 0x00853E73;  // 853E5A hook continuation when ebp != 0
const DWORD ADDR_853F17         = 0x00853F17;  // 853E5A hook continuation when ebp == 0 / synthetic tail
const DWORD ADDR_856BDE         = 0x00856BDE;  // 客户端本地属性重算-百分比次级属性链入口(24/25/26/27/28/29)
const DWORD ADDR_856BF3         = 0x00856BF3;  // 856BDE hook continuation when edx != 0
const DWORD ADDR_856C44         = 0x00856C44;  // 856BDE hook continuation when edx == 0 / synthetic tail
const DWORD ADDR_856C60         = 0x00856C60;  // 客户端角色面板大汇总入口：串起 853B00/856830/856BA0 等子链，并接收多个 output sub-struct
const DWORD ADDR_BE6760         = 0x00BE6760;  // UserLocal 全量属性/面板刷新入口：内部重跑 856C60 并刷新多个 UI
const DWORD ADDR_BF3E60         = 0x00BF3E60;  // UserLocal buff-on 刷新包装：内部重跑 856C60 并刷新 UI
const DWORD ADDR_BF4110         = 0x00BF4110;  // UserLocal buff-off 刷新包装：内部重跑 856C60 并刷新 UI
const DWORD ADDR_AbilityPrimaryCache = 0x00F6D134; // 角色面板主属性三元组缓存
const DWORD ADDR_AbilityExtendedCache = 0x00F6D200; // 角色面板扩展属性三元组缓存
const DWORD ADDR_8538C0         = 0x008538C0;  // 客户端角色面板红字-基础百分比汇总函数(18/19/20/21)
const DWORD ADDR_853B00         = 0x00853B00;  // 客户端角色面板红字-基础平属性汇总函数(2/3/4/5/8/9)
const DWORD ADDR_853E10         = 0x00853E10;  // 客户端角色面板红字-全属性/HP/MP百分比汇总函数(18/19/20/21/22/23)
const DWORD ADDR_856830         = 0x00856830;  // 客户端角色面板红字-单属性/攻防/速度/暴伤等汇总函数(10/11/12/13/14/15/16/17/50/51/52/53)
const DWORD ADDR_856BA0         = 0x00856BA0;  // 客户端角色面板扩展属性 plain 结果拼装：把 stats[0x60..0x74] 累加进 6-field output struct
const DWORD ADDR_AE0A70         = 0x00AE0A70;  // 客户端角色面板红字-技能等级提升汇总函数(34)
const DWORD ADDR_AE0E60         = 0x00AE0E60;  // 客户端能力面板红字/显示候选函数，处理暴击/无视/伤害/BOSS伤害显示
const DWORD ADDR_AE6C21         = 0x00AE6C21;  // AE0E60 的关键 callsite，能力面板红字显示候选调用点
const DWORD ADDR_AE43D5         = 0x00AE43D5;  // 能力面板红字候选链中的技能等级读取点（mov ebp,[esp+18]）
const DWORD ADDR_82F780         = 0x0082F780;  // 能力面板 caller-local diff helper：消费 sibling(this-0xCC) 的 0x30/0x3C/0x48 triplet 家族
const DWORD ADDR_82F870         = 0x0082F870;  // 能力面板 caller-local diff helper：消费 sibling(this-0xCC) 的 0x24/0x30/0x48 triplet 家族
const DWORD ADDR_82F960         = 0x0082F960;  // 能力面板 caller-local diff helper：消费 sibling(this-0xCC) 的 0x30/0x3C/0x48 triplet 家族
const DWORD ADDR_82FA50         = 0x0082FA50;  // 能力面板 caller-local diff helper：消费 sibling(this-0xCC) 的 0x24/0x30/0x48 triplet 家族
const DWORD ADDR_84BE40         = 0x0084BE40;  // 能力面板最终数值消费函数：读取 this+0x120/0x124/0x128，并继续拼装最终结果
const DWORD ADDR_9F7546         = 0x009F7546;  // 84C470 caller-local diff pre-sub 观察点：sum += slot198; sum += local48
const DWORD ADDR_9F754B         = 0x009F754B;  // 84C470 caller-local diff pre-sub continuation
const DWORD ADDR_9F7241         = 0x009F7241;  // 84BE40 caller-local diff pre-sub：sum += slot120; sum += local48
const DWORD ADDR_9F7246         = 0x009F7246;  // 84BE40 caller-local diff pre-sub continuation
const DWORD ADDR_9F7893         = 0x009F7893;  // 84CA90 caller-local diff pre-sub：sum += slot1D4; sum += local48（命中）
const DWORD ADDR_9F7898         = 0x009F7898;  // 84CA90 caller-local diff pre-sub continuation
const DWORD ADDR_9F7C7F         = 0x009F7C7F;  // 84CA90 caller-local diff pre-sub：sum += slot1D4; sum += local48（魔命中）
const DWORD ADDR_9F7C84         = 0x009F7C84;  // 84CA90 caller-local diff pre-sub continuation
const DWORD ADDR_9F8048         = 0x009F8048;  // 84CBD0 caller-local diff pre-sub：sum += slot210; sum += local48（回避）
const DWORD ADDR_9F804D         = 0x009F804D;  // 84CBD0 caller-local diff pre-sub continuation
const DWORD ADDR_9F82A8         = 0x009F82A8;  // 84CBD0 caller-local diff pre-sub：sum += slot240/local48（魔回避）
const DWORD ADDR_9F82AD         = 0x009F82AD;  // 84CBD0 caller-local diff pre-sub continuation
const DWORD ADDR_9F6E6F         = 0x009F6E6F;  // 能力面板攻击力文本样式选择点
const DWORD ADDR_9F6E76         = 0x009F6E76;  // 攻击力文本样式选择 continuation
const DWORD ADDR_9F7A7C         = 0x009F7A7C;  // 能力面板暴击率文本样式选择点
const DWORD ADDR_9F7A83         = 0x009F7A83;  // 暴击率文本样式选择 continuation
const DWORD ADDR_9F8565         = 0x009F8565;  // 能力面板移动速度文本样式选择点
const DWORD ADDR_9F856C         = 0x009F856C;  // 移动速度文本样式选择 continuation
const DWORD ADDR_9F8696         = 0x009F8696;  // 能力面板跳跃力文本样式选择点
const DWORD ADDR_9F869C         = 0x009F869C;  // 跳跃力文本样式选择 continuation
const DWORD ADDR_857BB6         = 0x00857BB6;  // 856C60 内部加值重写点：src(edi+0x91/0x95) -> this+0x15C
const DWORD ADDR_857C29         = 0x00857C29;  // 856C60 内部加值重写点：src(edi+0xA1/0xA5) -> this+0x198
const DWORD ADDR_857C3B         = 0x00857C3B;  // 856C60 内部 0x198 真正写入指令
const DWORD ADDR_857C9C         = 0x00857C9C;  // 856C60 内部加值重写点：src(edi+0xA9/0xAD) -> this+0x1D4
const DWORD ADDR_857D0F         = 0x00857D0F;  // 856C60 内部加值重写点：src(edi+0xB1/0xB5) -> this+0x210
const DWORD ADDR_8569C3         = 0x008569C3;  // 0x198 写点：sum = sub_821D10(this) + [edi+0x44]
const DWORD ADDR_856D57         = 0x00856D57;  // 0x198 初始写点：856C60 入口初始化阶段的默认 key 写入
const DWORD ADDR_85725F         = 0x0085725F;  // 0x198 写点：sum = signext([*edi+0x18]) + old(0x198)
const DWORD ADDR_858AED         = 0x00858AED;  // 0x198 写点：sum = sub_821280(ebp)
const DWORD ADDR_831A50         = 0x00831A50;  // 小 helper：直接用 edx/plainValue 重写 this+0x198
const DWORD ADDR_83AF02         = 0x0083AF02;  // 0x198 写点：sum = old(0x198) + [edi+0x7C5]
const DWORD ADDR_84C470         = 0x0084C470;  // 能力面板最终数值消费函数：直接读取 this+0x198/0x19C/0x1A0，并继续拼装最终结果
const DWORD ADDR_84CA90         = 0x0084CA90;  // 能力面板最终数值消费函数：读取 this+0x1D4/0x1D8/0x1DC 与 this+0x204/0x208/0x20C
const DWORD ADDR_84CBD0         = 0x0084CBD0;  // 能力面板最终数值消费函数：读取 this+0x210/0x214/0x218 与 this+0x240/0x244/0x248
const DWORD ADDR_AE0B23         = 0x00AE0B23;  // 客户端本地属性重算-技能等级提升潜能链入口(34)
const DWORD ADDR_AE0B34         = 0x00AE0B34;  // AE0B23 hook continuation when ecx != 0
const DWORD ADDR_AE0B72         = 0x00AE0B72;  // AE0B23 hook continuation when ecx == 0
const DWORD ADDR_AE0FDC         = 0x00AE0FDC;  // 客户端本地属性重算-伤害/无视/BOSS/暴伤链入口(30/40/43/49/50/51/52/53)
const DWORD ADDR_AE0FE4         = 0x00AE0FE4;  // AE0FDC hook continuation
const DWORD ADDR_A4CA60         = 0x00A4CA60;  // 角色面板/潜能属性文本组装入口，按 index 读取潜能数组并拼红字文本
const DWORD ADDR_4098C0         = 0x004098C0;  // 加密三元组写入用随机 key 生成函数
const DWORD ADDR_F631B8         = 0x00F631B8;  // 加密三元组随机 key 上下文
// 发包函数：CHANGE_KEYMAP（opcode 221, 遍历89项差异发包）
// __thiscall(ECX = *(DWORD*)ADDR_KeyArrayPtr), 无栈参数
const DWORD ADDR_5E6F90         = 0x005E6F90;  // sub_5E6F90
// 通用发包入口：栈参数 [esp+4]=packetData, [esp+8]=packetLen，packet[0..1]=opcode
const DWORD ADDR_43D94D         = 0x0043D94D;  // send packet
const DWORD ADDR_43D510         = 0x0043D510;  // raw send packet callee used by 43D94D first call
const DWORD ADDR_4D6A13         = 0x004D6A13;  // recv packet opcode dispatch prologue
const DWORD ADDR_49C9C0         = 0x0049C9C0;  // 能力面板红字候选链共用 hash lookup：key -> value / return &node->value
const DWORD ADDR_49CA01         = 0x0049CA01;  // 能力面板红字候选链：读取 [edx+0xC] -> 写入 [eax]
const DWORD ADDR_52FD80         = 0x0052FD80;  // 能力面板红字候选链共用 hash insert/update：写入 [vtable,next,key,value] 节点
const DWORD ADDR_52FE14         = 0x0052FE14;  // 能力面板红字候选链：读取 [ecx] -> 写入 [eax+0xC]
const DWORD ADDR_622680         = 0x00622680;  // 能力面板红字候选链共用 hash clone/copy：复制 [vtable,next,key,value] 节点链
const DWORD ADDR_6226CE         = 0x006226CE;  // 能力面板红字候选链：读取 [esi+0xC] -> 写入 [eax+0xC]
const DWORD ADDR_417240         = 0x00417240;  // COutPacket::Encode4 (__thiscall, push value)
const DWORD ADDR_4D63A0         = 0x004D63A0;  // CWvsContext / network session send (__thiscall, push COutPacket*)
const DWORD ADDR_750C20         = 0x00750C20;  // COutPacket ctor/init (__thiscall, push opcode)
const DWORD ADDR_B4C450         = 0x00B4C450;  // game tick getter
const DWORD ADDR_NetClient      = 0x00F5A07C;  // active network/session object used by sub_4D63A0
const DWORD ADDR_4020B0         = 0x004020B0;  // game free (__thiscall ecx=ADDR_GameHeap, push alloc)

// ============================================================================
// 技能释放分类链（证据来自 xdbg trace）
// ============================================================================
// 00B31349: 技能释放分类根节点（完整 skillId 决策树入口）
// 00B3144D: 技能释放高层分类分流块
// 00B31722: 命中特定技能家族后的专门处理分支
// 00B2F370: 技能释放大分支函数（原 SkillWnd 双击会以 ECX=*(0x00F59FC0), push 0,0,0,skillId 调用）
// 00ABAF70: 技能本地表现/特效分发表现函数（special_move 等最终会走到这里）
// 007CE790/007D0000: 深层技能白名单判定（后续播放动画/效果链）
// 004069E0: 坐骑特殊动作白名单（0042C300 的 case 51/52 会先经过这里）
// 00406AB0: 坐骑隐藏动作白名单（0042C300 的 case 51/52 第二层表驱动白名单）
// 007CF370: 飞行骑宠 itemId -> 原生飞行技能 ID 映射
// 00A9AAA0: 飞行家族判定（上+跳/二次起飞链会调用，原生仅放行 0xC7 家族）
const DWORD ADDR_UserLocal      = 0x00F59FC0;  // 原生技能栏双击释放时作为 B2F370 的 this/ECX
const DWORD ADDR_B2F370         = 0x00B2F370;
const DWORD ADDR_B31349         = 0x00B31349;
const DWORD ADDR_B3144D         = 0x00B3144D;
const DWORD ADDR_B31722         = 0x00B31722;
const DWORD ADDR_ABAF70         = 0x00ABAF70;
const DWORD ADDR_7CE790         = 0x007CE790;
const DWORD ADDR_7D0000         = 0x007D0000;
const DWORD ADDR_4069E0         = 0x004069E0;
const DWORD ADDR_406AB0         = 0x00406AB0;
const DWORD ADDR_7CF370         = 0x007CF370;
const DWORD ADDR_7DC1B0         = 0x007DC1B0;
const DWORD ADDR_A9AAA0         = 0x00A9AAA0;
const DWORD ADDR_7D4CA0         = 0x007D4CA0;
const DWORD ADDR_7D4CD0         = 0x007D4CD0;
// sub_856C60 stat finalize cap: speed clamps to maxBase+140, jump clamps to 123.
const DWORD ADDR_858D30         = 0x00858D30;  // speed upper clamp: cmp/jl/mov edx,edi
const DWORD ADDR_858D49         = 0x00858D49;  // jump upper clamp compare
const DWORD ADDR_858D4E         = 0x00858D4E;  // jump upper clamp branch + mov edx,123

// ============================================================================
// 技能列表构建过滤点（sub_7DD420 LABEL_42 入口，技能加入 entries 前最后一刻）
// ============================================================================
const DWORD ADDR_7DD67D         = 0x007DD67D;  // LABEL_42: entries 加入入口
const DWORD ADDR_7DD684         = 0x007DD684;  // LABEL_42 原始指令续接点
const DWORD ADDR_7DD6E8         = 0x007DD6E8;  // loop continue（跳过当前技能）



