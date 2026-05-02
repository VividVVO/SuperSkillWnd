#include "skill/skill_overlay_bridge.h"

#include "core/Common.h"
#include "core/GameAddresses.h"
#include "skill/skill_local_data.h"
#include "util/runtime_paths.h"

#include <windows.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <vector>

namespace
{
    std::string g_customSkillRoutePathStorage;
    std::string g_superSkillConfigPathStorage;
    std::string g_nativeSkillInjectPathStorage;
    const char* kCustomSkillRoutePath = "";
    const char* kSuperSkillConfigPath = "";
    const char* kNativeSkillInjectPath = "";
    std::vector<BYTE> g_outgoingPacketRewriteBuffer;
    enum CustomSkillPacketRoute
    {
        CustomSkillPacketRoute_None = 0,
        CustomSkillPacketRoute_CloseRange = 1,
        CustomSkillPacketRoute_RangedAttack = 2,
        CustomSkillPacketRoute_MagicAttack = 3,
        CustomSkillPacketRoute_SpecialMove = 4,
        CustomSkillPacketRoute_SkillEffect = 5,
        CustomSkillPacketRoute_CancelBuff = 6,
        CustomSkillPacketRoute_SpecialAttack = 7
    };

    std::string WideToUtf8(const std::wstring& text)
    {
        return ssw::path::WideToUtf8(text);
    }

    std::wstring CombinePath(const std::wstring& left, const wchar_t* right)
    {
        return ssw::path::Combine(left, right);
    }

    std::wstring GetHookDllDirectory()
    {
        return ssw::path::GetHookDllDirectory();
    }

    std::wstring ResolveRootDirectoryFromHook()
    {
        return ssw::path::ResolveRootDirectoryFromHook();
    }

    std::wstring ResolveSkillConfigDir(const std::wstring& rootDir, const std::wstring& dllDir)
    {
        return ssw::path::ResolveSkillConfigDir(rootDir, dllDir);
    }

    void EnsureSkillConfigPathsInitialized()
    {
        if (!g_superSkillConfigPathStorage.empty())
            return;

        const std::wstring dllDir = GetHookDllDirectory();
        const std::wstring rootDir = ResolveRootDirectoryFromHook();
        const std::wstring skillDir = ResolveSkillConfigDir(rootDir, dllDir);

        g_superSkillConfigPathStorage = WideToUtf8(CombinePath(skillDir, L"super_skills.json"));
        g_customSkillRoutePathStorage = WideToUtf8(CombinePath(skillDir, L"custom_skill_routes.json"));
        g_nativeSkillInjectPathStorage = WideToUtf8(CombinePath(skillDir, L"native_skill_injections.json"));

        kSuperSkillConfigPath = g_superSkillConfigPathStorage.c_str();
        kCustomSkillRoutePath = g_customSkillRoutePathStorage.c_str();
        kNativeSkillInjectPath = g_nativeSkillInjectPathStorage.c_str();

        WriteLogFmt("[SkillBridge] config root=%s skillDir=%s",
            WideToUtf8(rootDir).c_str(),
            WideToUtf8(skillDir).c_str());
    }

    enum CustomSkillReleaseClass
    {
        CustomSkillReleaseClass_None = 0,
        CustomSkillReleaseClass_NativeB31722 = 1,
        CustomSkillReleaseClass_NativeClassifierProxy = 2
    };

    struct CustomSkillUseRoute
    {
        int skillId = 0;
        int proxySkillId = 0;
        CustomSkillPacketRoute packetRoute = CustomSkillPacketRoute_None;
        CustomSkillReleaseClass releaseClass = CustomSkillReleaseClass_None;
        bool borrowDonorVisual = false;
        int visualSkillId = 0;
    };

    const char* LocalBehaviorToString(SkillLocalBehaviorKind behavior)
    {
        switch (behavior)
        {
        case SkillLocalBehavior_Attack: return "attack";
        case SkillLocalBehavior_Buff: return "buff";
        case SkillLocalBehavior_Passive: return "passive";
        case SkillLocalBehavior_SummonLike: return "summon_like";
        case SkillLocalBehavior_MorphLike: return "morph_like";
        case SkillLocalBehavior_MountLike: return "mount_like";
        default: return "unknown";
        }
    }

    bool IsBehaviorRouteLikelyCompatible(SkillLocalBehaviorKind behavior, CustomSkillPacketRoute packetRoute)
    {
        switch (behavior)
        {
        case SkillLocalBehavior_Passive:
            return false;
        case SkillLocalBehavior_SummonLike:
        case SkillLocalBehavior_MorphLike:
        case SkillLocalBehavior_MountLike:
            return packetRoute == CustomSkillPacketRoute_SpecialMove ||
                   packetRoute == CustomSkillPacketRoute_SkillEffect ||
                   packetRoute == CustomSkillPacketRoute_CancelBuff;
        case SkillLocalBehavior_Buff:
            return packetRoute == CustomSkillPacketRoute_SpecialMove ||
                   packetRoute == CustomSkillPacketRoute_SkillEffect ||
                   packetRoute == CustomSkillPacketRoute_CancelBuff;
        case SkillLocalBehavior_Attack:
        case SkillLocalBehavior_Unknown:
        default:
            return true;
        }
    }

    bool ShouldUseStableMountSpecialMoveProxy(const CustomSkillUseRoute& route)
    {
        return route.skillId > 0 &&
               route.proxySkillId > 0 &&
               route.skillId != route.proxySkillId &&
               route.releaseClass == CustomSkillReleaseClass_NativeClassifierProxy &&
               route.packetRoute == CustomSkillPacketRoute_SpecialMove &&
               route.skillId / 10000 == 8000 &&
               route.proxySkillId / 10000 == 8000;
    }

    bool TryNormalizeRouteForBehavior(SkillLocalBehaviorKind behavior, CustomSkillPacketRoute& packetRoute)
    {
        switch (behavior)
        {
        case SkillLocalBehavior_Buff:
        case SkillLocalBehavior_MorphLike:
        case SkillLocalBehavior_MountLike:
            if (packetRoute != CustomSkillPacketRoute_SpecialMove &&
                packetRoute != CustomSkillPacketRoute_SkillEffect &&
                packetRoute != CustomSkillPacketRoute_CancelBuff)
            {
                packetRoute = CustomSkillPacketRoute_SpecialMove;
                return true;
            }
            return false;
        default:
            return false;
        }
    }

    bool TryNormalizeProxyForBehavior(
        SkillLocalBehaviorKind behavior,
        CustomSkillReleaseClass releaseClass,
        bool routeWasNormalized,
        int& proxySkillId)
    {
        if (!routeWasNormalized)
            return false;

        if (releaseClass != CustomSkillReleaseClass_NativeClassifierProxy)
            return false;

        switch (behavior)
        {
        case SkillLocalBehavior_Buff:
        case SkillLocalBehavior_MorphLike:
        case SkillLocalBehavior_MountLike:
            if (proxySkillId != 1001003)
            {
                proxySkillId = 1001003;
                return true;
            }
            return false;
        default:
            return false;
        }
    }

    struct ActiveNativeReleaseContext
    {
        int customSkillId = 0;
        int classifierProxySkillId = 0;
        CustomSkillPacketRoute packetRoute = CustomSkillPacketRoute_None;
        CustomSkillReleaseClass releaseClass = CustomSkillReleaseClass_None;
        DWORD armedTick = 0;
        DWORD firstRewriteTick = 0;
        int remainingRewriteBudget = 0;
    };

    struct RecentNativePresentationContext
    {
        int customSkillId = 0;
        int proxySkillId = 0;
        int visualSkillId = 0;
        bool borrowDonorVisual = false;
        DWORD armedTick = 0;
        DWORD generation = 0;       // 每次 Arm 递增
    };

    static DWORD g_presentationGeneration = 0;        // 全局递增计数器
    static DWORD g_lastConsumedGeneration = 0;        // ABAF70 最后消费的 generation
    const DWORD kNativeClassifierProxyGraceMs = 80;
    const int kStableMountSpecialMoveProxySkillId = 80001025;

    enum PassiveValueSpecType
    {
        PassiveValueSpecType_None = 0,
        PassiveValueSpecType_Fixed = 1,
        PassiveValueSpecType_SkillField = 2
    };

    struct PassiveValueSpec
    {
        PassiveValueSpecType type = PassiveValueSpecType_None;
        int fixedValue = 0;
        std::string skillFieldName;
    };

    struct PassiveBonusDefinition
    {
        int sourceSkillId = 0;
        std::vector<int> targetSkillIds;
        PassiveValueSpec damagePercent;
        PassiveValueSpec ignoreDefensePercent;
        PassiveValueSpec attackCount;
        PassiveValueSpec mobCount;
    };

    struct PassiveEffectPatchSnapshot
    {
        bool initialized = false;
        int skillId = 0;
        int level = 0;
        int originalDamageLocal = 0;
        int originalDamage = 0;
        int originalDamageAlt = 0;
        int originalMobCount = 0;
        int originalAttackCount = 0;
        int originalAttackCountAlt = 0;
        int originalIgnoreMobpdpR = 0;
        bool hasDamageLocal = false;
        bool hasDamage = false;
        bool hasDamageAlt = false;
        bool hasMobCount = false;
        bool hasAttackCount = false;
        bool hasAttackCountAlt = false;
        bool hasIgnoreMobpdpR = false;
        int lastDamageBonus = 0;
        int lastMobCountBonus = 0;
        int lastAttackCountBonus = 0;
        int lastIgnoreMobpdpRBonus = 0;
    };

    struct PassiveEffectRuntimeContext
    {
        int skillId = 0;
        int level = 0;
        DWORD lastSeenTick = 0;
    };

    struct SuperSkillDefinition
    {
        enum IndependentDisplayMode
        {
            IndependentDisplayMode_None = 0,
            IndependentDisplayMode_Native = 1,
            IndependentDisplayMode_Overlay = 2,
            IndependentDisplayMode_Both = 3
        };

        int skillId = 0;
        int tabIndex = 1;
        bool passive = false;
        int superSpCost = 1;
        bool hideFromNativeSkillWnd = true;
        bool showInNativeWhenLearned = false;
        bool showInSuperWhenLearned = false;
        int superSpCarrierSkillId = 0;
        bool allowNativeUpgradeFallback = true;
        int behaviorSkillId = 0;
        int mountItemId = 0;
        int mountTamingMobId = 0;
        bool mountedDoubleJumpEnabled = false;
        int mountedDoubleJumpSkillId = 0;
        bool mountedDemonJumpEnabled = false;
        int mountedDemonJumpSkillId = 0;
        bool useNativeMountMovement = true;
        bool hasMountSpeedOverride = false;
        int mountSpeedOverride = 0;
        bool hasMountJumpOverride = false;
        int mountJumpOverride = 0;
        bool hasMountFsOverride = false;
        double mountFsOverride = 0.0;
        bool hasMountSwimOverride = false;
        double mountSwimOverride = 0.0;
        std::vector<int> visibleJobIds;
        std::string visibleJobLabel;
        bool independentBuffEnabled = false;
        int independentSourceSkillId = 0;
        int independentCarrierMaskPosition = 0;
        unsigned int independentCarrierMaskValue = 0;
        int independentNativeMaskPosition = 0;
        unsigned int independentNativeMaskValue = 0;
        PassiveValueSpec independentNativeValueSpec;
        int independentNativeDisplaySkillId = 0;
        std::string localBonusKey;
        PassiveValueSpec localValueSpec;
        std::map<std::string, PassiveValueSpec> clientBonusSpecs;
        IndependentDisplayMode independentDisplayMode = IndependentDisplayMode_Native;
        std::vector<PassiveBonusDefinition> passiveBonuses;
    };

    struct ActiveIndependentBuffRewriteState
    {
        bool active = false;
        int skillId = 0;
        int carrierMaskPosition = 0;
        unsigned int carrierMaskValue = 0;
        bool rewriteToNative = false;
        int nativeMaskPosition = 0;
        unsigned int nativeMaskValue = 0;
        DWORD activatedTick = 0;
    };

    struct HiddenSkillDefinition
    {
        int skillId = 0;
        bool hideFromNativeSkillWnd = true;
        bool hideFromSuperSkillWnd = true;
    };

    struct NativeSkillInjectionDefinition
    {
        int skillId = 0;
        int donorSkillId = 0;
        bool enabled = true;
    };

    struct NativeSkillSlot
    {
        int slotSkillId = 0;
        uintptr_t rowData = 0;
    };

    enum MountedRuntimeSkillKind
    {
        MountedRuntimeSkillKind_DoubleJump = 0,
        MountedRuntimeSkillKind_DemonJump = 1,
        MountedRuntimeSkillKind_Count = 2
    };

    bool IsMountedDemonJumpRuntimeProxySkillId(int skillId)
    {
        switch (skillId)
        {
        case 20021181:
        case 23001002:
        case 33001002:
            return true;
        default:
            return false;
        }
    }

    bool IsMountedDemonJumpRuntimeChildSkillId(int skillId)
    {
        switch (skillId)
        {
        case 30010183:
        case 30010184:
        case 30010186:
            return true;
        default:
            return false;
        }
    }

    bool IsMountedRuntimeSkillLinkedChild(
        int configuredSkillId,
        int runtimeSkillId,
        MountedRuntimeSkillKind kind)
    {
        if (kind != MountedRuntimeSkillKind_DemonJump)
            return false;

        if (configuredSkillId <= 0 || runtimeSkillId <= 0)
            return false;

        if (configuredSkillId != 30010110)
            return false;

        // Native demon jump is a root-context skill (30010110) that resolves
        // onto one of its movement children at runtime.
        if (runtimeSkillId == 30010183 ||
            runtimeSkillId == 30010184 ||
            runtimeSkillId == 30010186)
        {
            return true;
        }

        // Mounted fallback can still transiently probe movement-family proxy
        // skills before the root demon-jump context fully takes over.
        return IsMountedDemonJumpRuntimeProxySkillId(runtimeSkillId);
    }

    std::map<int, CustomSkillUseRoute> g_customRoutesBySkillId;
    std::map<unsigned long long, CustomSkillUseRoute> g_customRoutesByProxyAndRoute;
    std::map<int, SuperSkillDefinition> g_superSkillsBySkillId;
    std::map<int, HiddenSkillDefinition> g_hiddenSkillsBySkillId;
    std::map<int, NativeSkillInjectionDefinition> g_nativeSkillInjectionsBySkillId;
    std::vector<int> g_superSkillIdsByTab[2];
    std::map<unsigned long long, ActiveIndependentBuffRewriteState> g_activeIndependentBuffRewriteStates;
    ActiveNativeReleaseContext g_activeNativeRelease;
    RecentNativePresentationContext g_recentNativePresentation;
    int g_recentMountedMovementOverrideMountItemId = 0;
    int g_recentMountedMovementOverrideSkillId = 0;
    DWORD g_recentMountedMovementOverrideTick = 0;
    volatile LONG g_recentMountedRuntimeSkillRouteArmItemId[MountedRuntimeSkillKind_Count] = {0};
    volatile LONG g_recentMountedRuntimeSkillRouteArmTick[MountedRuntimeSkillKind_Count] = {0};
    const bool kEnableMountedRuntimeSkillRouteArm = true;
    bool g_loggedMissingRouteConfig = false;
    bool g_loggedDuplicateRoutes = false;
    bool g_loggedMissingSuperSkillConfig = false;
    bool g_loggedDuplicateSuperSkills = false;
    bool g_loggedMissingNativeInjectionConfig = false;
    bool g_loggedDuplicateNativeInjections = false;
    DWORD g_lastMissingConfigRetryTick = 0;
    int g_defaultSuperSpCarrierSkillId = 0;
    int g_lastResolvedPlayerJobId = 0;
    bool g_hasLastResolvedPlayerJobId = false;
    int g_lastOverlayConfiguredJobId = -1;
    const DWORD kMissingConfigRetryIntervalMs = 3000;
    const DWORD kNativeReleaseContextTimeoutMs = 1200;
    const DWORD kNativeReleaseFollowupWindowMs = 450;
    const int kNativeReleaseRewriteBudget = 3;
    const DWORD kNativePresentationContextTimeoutMs = 1200;
    const DWORD kMountedMovementOverrideSelectionTimeoutMs = 300000;
    const DWORD kMountedDoubleJumpMountSelectionFallbackTimeoutMs = 5000;
    const DWORD kMountedDoubleJumpRouteArmTimeoutMs = 400;
    const DWORD kIndependentBuffRefreshCancelIgnoreMs = 3000;
    const DWORD kIndependentBuffClientCancelWindowMs = 1500;
    const size_t kNativeSkillRowCloneBytes = 0x800;
    const unsigned short kPotentialOptionPacketOpcode = 1140;
    const unsigned short kPotentialOptionClearPacketOpcode = 1141;
    const int kSuperSkillResetRequestSkillId = 2147001999;
    const int kSuperSkillResetPreviewRequestSkillId = 2147001998;
    const unsigned short kSuperSkillResetPreviewPacketOpcode = 1142;
    const unsigned short kIndependentBuffVirtualGivePacketOpcode = 1143;
    const unsigned short kIndependentBuffVirtualCancelPacketOpcode = 1144;
    const unsigned short kSuperSkillLevelSyncPacketOpcode = 1145;
    const unsigned short kGiveBuffPacketOpcode = 0x25;
    const unsigned short kCancelBuffPacketOpcode = 0x26;
    const unsigned short kClientCancelBuffPacketOpcode = 0x94;
    const unsigned short kServerCloseRangeAttackPacketOpcode = 0x105;
    const unsigned short kServerEnergyAttackPacketOpcode = 0x108;
    const int kBuffMaskIntCount = 8;
    const int kBuffMaskByteCount = kBuffMaskIntCount * sizeof(int);

    const char* GetMountedRuntimeSkillLogTag(MountedRuntimeSkillKind kind)
    {
        return kind == MountedRuntimeSkillKind_DemonJump
                   ? "MountDemonJump"
                   : "MountDoubleJump";
    }

    int GetMountedRuntimeSkillDefaultSkillId(MountedRuntimeSkillKind kind)
    {
        return kind == MountedRuntimeSkillKind_DemonJump ? 30010110 : 3101003;
    }

    bool IsMountedRuntimeSkillEnabled(
        const SuperSkillDefinition& definition,
        MountedRuntimeSkillKind kind)
    {
        switch (kind)
        {
        case MountedRuntimeSkillKind_DemonJump:
            return definition.mountedDemonJumpEnabled;
        case MountedRuntimeSkillKind_DoubleJump:
        default:
            return definition.mountedDoubleJumpEnabled;
        }
    }

    int ResolveMountedRuntimeSkillIdFromDefinition(
        const SuperSkillDefinition& definition,
        MountedRuntimeSkillKind kind)
    {
        if (!IsMountedRuntimeSkillEnabled(definition, kind))
            return 0;

        switch (kind)
        {
        case MountedRuntimeSkillKind_DemonJump:
            return definition.mountedDemonJumpSkillId > 0
                       ? definition.mountedDemonJumpSkillId
                       : GetMountedRuntimeSkillDefaultSkillId(kind);
        case MountedRuntimeSkillKind_DoubleJump:
        default:
            return definition.mountedDoubleJumpSkillId > 0
                       ? definition.mountedDoubleJumpSkillId
                       : GetMountedRuntimeSkillDefaultSkillId(kind);
        }
    }

    void ObserveMountedMovementOverrideSelection(int mountItemId, int skillId)
    {
        if (mountItemId <= 0)
        {
            g_recentMountedMovementOverrideMountItemId = 0;
            g_recentMountedMovementOverrideSkillId = 0;
            g_recentMountedMovementOverrideTick = 0;
            return;
        }

        // A later mount buff packet can briefly arrive without a resolvable display skill.
        // Keep the last positive selection for this mount so movement override doesn't
        // immediately fall back to the first matching definition.
        if (skillId <= 0)
            return;

        g_recentMountedMovementOverrideMountItemId = mountItemId;
        g_recentMountedMovementOverrideSkillId = skillId;
        g_recentMountedMovementOverrideTick = GetTickCount();
    }

    bool TryGetPreferredMountedMovementOverrideSkillId(int mountItemId, int& outSkillId)
    {
        outSkillId = 0;
        if (mountItemId <= 0 ||
            g_recentMountedMovementOverrideMountItemId != mountItemId ||
            g_recentMountedMovementOverrideSkillId <= 0 ||
            g_recentMountedMovementOverrideTick == 0)
        {
            return false;
        }

        const DWORD now = GetTickCount();
        if (now - g_recentMountedMovementOverrideTick > kMountedMovementOverrideSelectionTimeoutMs)
            return false;

        outSkillId = g_recentMountedMovementOverrideSkillId;
        return true;
    }

    bool TryGetRecentMountedMovementOverrideMountItemId(int& outMountItemId, DWORD maxAgeMs)
    {
        outMountItemId = 0;
        if (g_recentMountedMovementOverrideMountItemId <= 0 ||
            g_recentMountedMovementOverrideTick == 0)
        {
            return false;
        }

        const DWORD now = GetTickCount();
        const DWORD allowedAgeMs =
            maxAgeMs > 0 ? maxAgeMs : kMountedDoubleJumpMountSelectionFallbackTimeoutMs;
        if (now - g_recentMountedMovementOverrideTick > allowedAgeMs)
            return false;

        outMountItemId = g_recentMountedMovementOverrideMountItemId;
        return outMountItemId > 0;
    }
    const int kSingleStatGiveBuffReasonOffset = kBuffMaskByteCount;
    const int kSingleStatGiveBuffSkillIdOffset = kSingleStatGiveBuffReasonOffset + 2;
    const int kSingleStatGiveBuffDurationOffset = kSingleStatGiveBuffSkillIdOffset + 4;
    const int kMountGiveBuffPaddingOffset = kBuffMaskByteCount + 2;
    const int kMountGiveBuffItemIdOffset = kMountGiveBuffPaddingOffset + 1;
    const int kMountGiveBuffSkillIdOffset = kMountGiveBuffItemIdOffset + 4;

    uintptr_t g_nativeInjectedEntriesPtr = 0;
    uintptr_t g_nativeOriginalEntriesPtr = 0;
    uintptr_t g_nativeInjectedSkillWnd = 0;
    std::vector<unsigned char> g_nativeInjectedEntriesBlock;
    std::vector<std::vector<unsigned char>> g_nativeInjectedRowBlocks;
    std::vector<std::string> g_nativeInjectedNames;
    const int kIndependentTab0SkillIds[] = { 1000, 1001, 1002, 1003, 1005, 1006, 1007, 1009, 1010, 1016, 1013 };
    const int kIndependentTab1SkillIds[] = { 110, 111, 112, 1037, 1001037, 1036, 1039, 1040, 1096, 1069, 73, 74, 1075, 1076 };
    const int kLocalIndependentPotentialBufferBytes = 0x128;
    const int kLocalIndependentPotentialBufferIntCount = kLocalIndependentPotentialBufferBytes / sizeof(int);
    typedef std::array<int, kLocalIndependentPotentialBufferIntCount> LocalPotentialDeltaBuffer;
    uintptr_t g_localIndependentPotentialIncreaseAddress = 0;
    LocalPotentialDeltaBuffer g_localIndependentPotentialBaseBuffer = {};
    LocalPotentialDeltaBuffer g_localIndependentPotentialDeltaBuffer = {};
    LocalPotentialDeltaBuffer g_localIndependentPotentialMergedBuffer = {};
    LocalPotentialDeltaBuffer g_localIndependentPotentialDisplayBuffer = {};
    std::map<int, LocalPotentialDeltaBuffer> g_activeLocalIndependentPotentialBySkillId;
    struct IndependentBuffOverlayState
    {
        int skillId = 0;
        int iconSkillId = 0;
        int slotIndex = 0;
        DWORD startTick = 0;
        int durationMs = 0;
        bool cancelable = true;
        unsigned long long activationOrder = 0;
    };
    std::map<int, IndependentBuffOverlayState> g_independentBuffOverlayStates;
    std::map<int, IndependentBuffOverlayState> g_independentBuffVirtualStates;
    struct NativeVisibleBuffState
    {
        int position = 0;
        unsigned int value = 0;
        int skillId = 0;
        DWORD startTick = 0;
        int durationMs = 0;
        unsigned long long activationOrder = 0;
    };
    std::map<unsigned long long, NativeVisibleBuffState> g_nativeVisibleBuffStates;
    int g_observedNativeVisibleBuffVisualCount = -1;
    int g_observedNativeVisibleBuffAnchorX = -1;
    uintptr_t g_observedStatusBarPtr = 0;
    unsigned long long g_independentBuffOverlayActivationCounter = 0;

    struct NativeBuffMaskDefinition
    {
        const char* name;
        int position;
        unsigned int value;
        const char* valueField;
        bool stacked;
    };

    const NativeBuffMaskDefinition kNativeBuffMaskDefinitions[] =
    {
        {"WATK", 1, 0x00000001u, "pad", false},
        {"WDEF", 1, 0x00000002u, "pdd", false},
        {"MATK", 1, 0x00000004u, "mad", false},
        {"MDEF", 1, 0x00000008u, "mdd", false},
        {"ACC", 1, 0x00000010u, "acc", false},
        {"AVOID", 1, 0x00000020u, "avoid", false},
        {"SPEED", 1, 0x00000080u, "speed", false},
        {"JUMP", 1, 0x00000100u, "jump", false},
        {"MAGIC_GUARD", 1, 0x00000200u, "x", false},
        {"DARKSIGHT", 1, 0x00000400u, "x", false},
        {"BOOSTER", 1, 0x00000800u, "x", false},
        {"POWERGUARD", 1, 0x00001000u, "x", false},
        {"MAXHP", 1, 0x00002000u, "hp", false},
        {"MAXMP", 1, 0x00004000u, "mp", false},
        {"SOULARROW", 1, 0x00010000u, "x", false},
        {"COMBO", 1, 0x00200000u, "", false},
        {"WK_CHARGE", 1, 0x00400000u, "x", false},
        {"DRAGONBLOOD", 1, 0x00800000u, "x", false},
        {"HOLY_SYMBOL", 1, 0x01000000u, "x", false},
        {"MESOUP", 1, 0x02000000u, "x", false},
        {"SHADOWPARTNER", 1, 0x04000000u, "x", false},
        {"MESOGUARD", 1, 0x10000000u, "x", false},
        {"MORPH", 2, 0x00000002u, "x", false},
        {"RECOVERY", 2, 0x00000004u, "x", false},
        {"MAPLE_WARRIOR", 2, 0x00000008u, "x", false},
        {"STANCE", 2, 0x00000010u, "prop", false},
        {"SHARP_EYES", 2, 0x00000020u, "x", false},
        {"MANA_REFLECTION", 2, 0x00000040u, "x", false},
        {"SPIRIT_CLAW", 2, 0x00000100u, "x", false},
        {"INFINITY", 2, 0x00000200u, "x", false},
        {"HOLY_SHIELD", 2, 0x00000400u, "x", false},
        {"HAMSTRING", 2, 0x00000800u, "x", false},
        {"BLIND", 2, 0x00001000u, "x", false},
        {"CONCENTRATE", 2, 0x00002000u, "x", false},
        {"ECHO_OF_HERO", 2, 0x00008000u, "x", false},
        {"MESO_RATE", 2, 0x00010000u, "x", false},
        {"DROP_RATE", 2, 0x00100000u, "x", false},
        {"EXPRATE", 2, 0x00400000u, "x", false},
        {"BERSERK_FURY", 2, 0x02000000u, "x", false},
        {"DIVINE_BODY", 2, 0x04000000u, "x", false},
        {"SPARK", 2, 0x08000000u, "x", false},
        {"ELEMENT_RESET", 2, 0x80000000u, "x", false},
        {"WIND_WALK", 3, 0x00000001u, "x", false},
        {"SLOW", 3, 0x00001000u, "x", false},
        {"MAGIC_SHIELD", 3, 0x00002000u, "x", false},
        {"MAGIC_RESISTANCE", 3, 0x00004000u, "x", false},
        {"SOUL_STONE", 3, 0x00008000u, "x", false},
        {"SOARING", 3, 0x00010000u, "", false},
        {"LIGHTNING_CHARGE", 3, 0x00040000u, "x", false},
        {"ENRAGE", 3, 0x00080000u, "x", false},
        {"OWL_SPIRIT", 3, 0x00100000u, "y", false},
        {"FINAL_CUT", 3, 0x00400000u, "y", false},
        {"DAMAGE_BUFF", 3, 0x00800000u, "x", false},
        {"ATTACK_BUFF", 3, 0x01000000u, "y", false},
        {"RAINING_MINES", 3, 0x02000000u, "", false},
        {"ENHANCED_MAXHP", 3, 0x04000000u, "x", false},
        {"ENHANCED_MAXMP", 3, 0x08000000u, "x", false},
        {"ENHANCED_WATK", 3, 0x10000000u, "pad", false},
        {"ENHANCED_MATK", 3, 0x20000000u, "mad", false},
        {"ENHANCED_WDEF", 3, 0x40000000u, "pdd", false},
        {"ENHANCED_MDEF", 3, 0x80000000u, "mdd", false},
        {"PERFECT_ARMOR", 4, 0x00000001u, "x", false},
        {"TORNADO", 4, 0x00000008u, "x", false},
        {"CONVERSION", 4, 0x00000100u, "x", false},
        {"REAPER", 4, 0x00000200u, "", false},
        {"INFILTRATE", 4, 0x00000400u, "", false},
        {"MECH_CHANGE", 4, 0x00000800u, "", false},
        {"AURA", 4, 0x00001000u, "", false},
        {"DARK_AURA", 4, 0x00002000u, "x", false},
        {"BLUE_AURA", 4, 0x00004000u, "x", false},
        {"YELLOW_AURA", 4, 0x00008000u, "x", false},
        {"BODY_BOOST", 4, 0x00010000u, "", false},
        {"FELINE_BERSERK", 4, 0x00020000u, "x", false},
        {"DICE_ROLL", 4, 0x00040000u, "", false},
        {"DIVINE_SHIELD", 4, 0x00080000u, "", false},
        {"PIRATES_REVENGE", 4, 0x00100000u, "damRate", false},
        {"TELEPORT_MASTERY", 4, 0x00200000u, "x", false},
        {"COMBAT_ORDERS", 4, 0x00400000u, "x", false},
        {"BEHOLDER", 4, 0x00800000u, "", false},
        {"GIANT_POTION", 4, 0x02000000u, "inflation", false},
        {"ONYX_SHROUD", 4, 0x04000000u, "x", false},
        {"ONYX_WILL", 4, 0x08000000u, "x", false},
        {"BLESS", 4, 0x20000000u, "x", false},
        {"THREATEN_PVP", 5, 0x00000001u, "", false},
        {"ICE_KNIGHT", 5, 0x00000002u, "", false},
        {"STR", 5, 0x00000010u, "str", false},
        {"INT", 5, 0x00000020u, "int", false},
        {"DEX", 5, 0x00000040u, "dex", false},
        {"LUK", 5, 0x00000080u, "luk", false},
        {"PVP_DAMAGE", 5, 0x00004000u, "PVPdamage", false},
        {"PVP_ATTACK", 5, 0x00008000u, "PVPdamage", false},
        {"INVINCIBILITY", 5, 0x00010000u, "x", false},
        {"SNATCH", 5, 0x00080000u, "", false},
        {"FROZEN", 5, 0x00100000u, "", false},
        {"ICE_SKILL", 5, 0x00400000u, "", false},
        {"HOLY_MAGIC_SHELL", 6, 0x00000001u, "x", false},
        {"DARK_METAMORPHOSIS", 6, 0x00000080u, "", false},
        {"WATER_SHIELD", 6, 0x00000100u, "x", false},
        {"DAMAGE_R", 6, 0x00000200u, "damage", false},
        {"SPIRIT_LINK", 6, 0x00000200u, "", false},
        {"ELEMENTAL_STATUS_R", 6, 0x00000010u, "ter", false},
        {"ABNORMAL_STATUS_R", 6, 0x00000020u, "asr", false},
        {"BARREL_ROLL", 6, 0x00001000u, "", false},
        {"CRITICAL_RATE", 6, 0x00008000u, "cr", false},
        {"VIRTUE_EFFECT", 6, 0x00004000u, "", false},
        {"NO_SLIP", 6, 0x00040000u, "", false},
        {"FAMILIAR_SHADOW", 6, 0x00080000u, "", false},
        {"SIDEKICK_PASSIVE", 6, 0x00100000u, "", false},
        {"DEFENCE_BOOST_R", 6, 0x04000000u, "pdd", false},
        {"ENERGY_CHARGE", 8, 0x01000000u, "", false},
        {"DASH_SPEED", 8, 0x02000000u, "speed", false},
        {"DASH_JUMP", 8, 0x04000000u, "jump", false},
        {"MONSTER_RIDING", 8, 0x08000000u, "", false},
        {"SPEED_INFUSION", 8, 0x10000000u, "x", false},
        {"HOMING_BEACON", 8, 0x20000000u, "", false},
        {"DEFAULT_BUFFSTAT", 8, 0x40000000u, "", false},
        {"DEFAULT_BUFFSTAT2", 8, 0x80000000u, "", false}
    };

    const char* kIndependentClientBonusKeys[] =
    {
        "str", "dex", "int", "luk", "allStat",
        "strPercent", "dexPercent", "intPercent", "lukPercent", "allStatPercent",
        "watk", "matk", "wdef", "mdef", "acc", "avoid", "speed", "jump",
        "accPercent", "avoidPercent", "watkPercent", "matkPercent", "wdefPercent", "mdefPercent",
        "maxHp", "maxMp", "maxHpPercent", "maxMpPercent",
        "damagePercent", "bossDamagePercent", "ignoreDefensePercent",
        "criticalRate", "criticalMinDamage", "criticalMaxDamage",
        "asr", "ter", "allSkillLevel", "attackSpeedStage"
    };

    // 游戏等级刷新控制
    DWORD g_lastRefreshTick = 0;
    DWORD g_fastRefreshUntilTick = 0;
    DWORD g_superSkillResetLevelSyncUntilTick = 0;
    const DWORD kRefreshIntervalMs = 150;
    const DWORD kPendingRefreshIntervalMs = 30;
    const DWORD kPendingRefreshWindowMs = 2000;
    const DWORD kSuperSkillResetLevelSyncWindowMs = 5000;
    DWORD g_lastUnmappedRouteLogTick = 0;
    unsigned short g_lastUnmappedRouteOpcode = 0;
    bool g_initialGameLevelLoaded = false;  // 是否已经成功从游戏加载过等级

    std::map<int, bool> g_overlayLearnedVisibilityBySkillId;
    std::map<int, int> g_observedCurrentLevelsBySkillId;
    std::map<int, int> g_observedBaseLevelsBySkillId;
    std::map<int, int> g_observedActualCurrentLevelsBySkillId;
    std::map<int, int> g_observedActualBaseLevelsBySkillId;
    std::map<int, int> g_persistentNonNativeSuperSkillLevelsBySkillId;
    std::map<uintptr_t, PassiveEffectPatchSnapshot> g_passiveEffectPatchSnapshotsByEffectPtr;
    std::map<uintptr_t, PassiveEffectRuntimeContext> g_passiveEffectRuntimeContextsByEffectPtr;
    std::map<int, DWORD> g_passiveEffectDamageWriteTickBySkillId;
    std::map<int, DWORD> g_passiveEffectDamageGetterTickBySkillId;
    std::map<int, DWORD> g_passiveEffectAttackCountGetterTickBySkillId;
    std::map<int, DWORD> g_recentPassiveAttackProbeTickBySkillId;
    const DWORD kPassiveEffectDamageWriteSuppressPacketRewriteMs = 5000;
    const DWORD kRecentPassiveAttackProbeWindowMs = 1500;
    volatile LONG g_superSkillResetPreviewRevision = 0;
    volatile LONG g_superSkillResetPreviewSpentSp = 0;
    volatile LONG g_superSkillResetPreviewCostMeso = 0;
    volatile LONG g_superSkillResetPreviewCurrentMeso = 0;
    volatile LONG g_superSkillResetPreviewHasCurrentMeso = 0;
    volatile LONG g_superSkillResetPreviewReceiveHookReady = 0;
    DWORD g_lastObservedLevelContext = 0;
    DWORD g_lastObservedSkillDataMgr = 0;
    std::map<int, DWORD> g_recentIndependentBuffClientCancelTickBySkillId;
    std::map<int, DWORD> g_recentIndependentBuffClientUseTickBySkillId;
    std::map<int, DWORD> g_recentIndependentBuffActivationTickBySkillId;
    DWORD g_independentBuffOwnerUserLocal = 0;
    DWORD g_independentBuffOwnerNetClient = 0;
    DWORD g_independentBuffSceneDetachSinceTick = 0;
    DWORD g_independentBuffOwnerMissingSinceTick = 0;
    const DWORD kIndependentBuffSceneDetachClearDelayMs = 3000;
    const DWORD kIndependentBuffOwnerMissingClearDelayMs = 300;
    const int kIndependentBuffMaxReasonableSkillId = 100000000;
    int g_observedNativeCursorState = -1;
    int g_observedSceneFadeAlpha = 0;
    int g_observedSceneFadeCandidateImageObj = 0;
    DWORD g_observedSceneFadeCandidateLastSeenTick = 0;

    const uintptr_t kOff_SkillWnd_ListView = 699 * sizeof(uint32_t);
    const uintptr_t kOff_SkillWnd_PageObj = 701 * sizeof(uint32_t);
    const uintptr_t kOff_SkillWnd_Entries = 720 * sizeof(uint32_t);
    const uintptr_t kOff_ListView_Count = 80;
    const uintptr_t kOff_PageObj_FirstVisibleIndex = 56;
    const int kMaxReasonableSkillCount = 256;

    typedef void (__thiscall *FnSkillWndPlusByRow)(void* skillWndThis, int relativeRowIndex);

    struct NativeSkillRowInfo
    {
        uintptr_t rowData = 0;
        int rowIndex = -1;
        bool found = false;
    };

    struct PendingSuperSkillUpgradePacketRewrite
    {
        int proxySkillId = 0;
        int targetSkillId = 0;
        DWORD armedTick = 0;
    };

    struct PendingOptimisticSuperSkillLevelHold
    {
        int expectedLevel = 0;
        DWORD expireTick = 0;
    };

    struct NativeDistributeSpPacket
    {
        DWORD writeOffset = 0;
        unsigned int* data = nullptr;
        DWORD size = 0;
        DWORD reserved0 = 0;
        DWORD reserved1 = 0;
    };

    PendingSuperSkillUpgradePacketRewrite g_pendingSuperSkillUpgradePacketRewrite;
    const DWORD kPendingSuperSkillUpgradeRewriteWindowMs = 1200;
    std::map<int, PendingOptimisticSuperSkillLevelHold> g_pendingOptimisticSuperSkillLevelHoldBySkillId;
    const DWORD kPendingOptimisticSuperSkillLevelHoldWindowMs = 1800;

    void RefreshSkillNativeState(SkillItem& item);
    bool ReadTextFile(const char* path, std::string& out);
    bool ParseJsonInt(const std::string& json, const char* key, int& outVal);
    bool ParseJsonDouble(const std::string& json, const char* key, double& outVal);
    bool ParseJsonString(const std::string& json, const char* key, std::string& outVal);
    bool FindArrayElement(const std::string& json, const char* arrayKey, int index, size_t& outBegin, size_t& outEnd);
    SkillItem* FindManagerSkillItem(SkillManager* manager, int skillId);
    bool ParseJsonBool(const std::string& json, const char* key, bool& outVal);
    bool ParseJsonIntArray(const std::string& json, const char* key, std::vector<int>& outValues);
    bool FindSuperSkillDefinition(int skillId, SuperSkillDefinition& outDefinition);
    bool FindHiddenSkillDefinition(int skillId, HiddenSkillDefinition& outDefinition);
    bool FindNativeSkillInjectionDefinition(int skillId, NativeSkillInjectionDefinition& outDefinition);
    bool IsKnownSuperSkillCarrierSkillId(int skillId);
    void NormalizeVisibleJobIds(std::vector<int>& visibleJobIds);
    bool FindMountedRuntimeSkillDefinitionByMountItemId(
        int mountItemId,
        MountedRuntimeSkillKind kind,
        SuperSkillDefinition& outDefinition);
    bool FindMountedDoubleJumpDefinitionByMountItemId(int mountItemId, SuperSkillDefinition& outDefinition);
    bool FindMountedDemonJumpDefinitionByMountItemId(int mountItemId, SuperSkillDefinition& outDefinition);
    bool FindMountedMovementOverrideDefinition(int mountItemId, int tamingMobId, SuperSkillDefinition& outDefinition);
    bool IsActiveNativeReleaseContextFresh();
    bool TryApplyMountedRuntimeSkillStableProxyOverride(
        CustomSkillUseRoute& route,
        bool armRouteIntent);
    bool ShouldKeepPassiveMountedDemonJumpRoute(const CustomSkillUseRoute& route);
    void EnsureMountedDemonJumpSyntheticRoutes();
    bool TryResolveMountedRuntimeProxyCustomSkillId(
        int runtimeSkillId,
        MountedRuntimeSkillKind kind,
        int& outCustomSkillId,
        int* mountItemIdOut = nullptr,
        DWORD maxAgeMs = 1200);
    bool TryResolveActiveMountedRuntimeSkillMountItemId(
        int routeSkillId,
        MountedRuntimeSkillKind kind,
        int& outMountItemId,
        const char** sourceOut = nullptr);
    int GetObservedBaseSkillLevel(int skillId);
    int GetObservedCurrentSkillLevel(int skillId);
    bool HasObservedBaseSkillLevel(int skillId);
    bool HasObservedCurrentSkillLevel(int skillId);
    int GetObservedActualBaseSkillLevel(int skillId);
    int GetObservedActualCurrentSkillLevel(int skillId);
    bool HasObservedActualBaseSkillLevel(int skillId);
    bool HasObservedActualCurrentSkillLevel(int skillId);
    int GetRuntimeAppliedSkillLevel(int skillId);
    bool IsTrackedNonNativeSuperSkill(int skillId, SkillItem** outItem = nullptr);
    bool IsSuperSkillResetLevelSyncWindowActive();
    bool ShouldSuppressNonNativeSuperSkillLevelFallback(int skillId);
    void ClearTrackedNonNativeSuperSkillLevel(int skillId, const char* reason);
    void ApplyAuthoritativeSuperSkillLevelSync(int skillId, int level, const char* reason);
    void RecordPersistentSuperSkillLevel(int skillId, int level, const char* reason);
    bool TryGetPersistentSuperSkillLevel(int skillId, int& outLevel);
    void ClearPersistentSuperSkillLevels(const char* reason);
    bool ClearIndependentBuffRuntimeStateForDefinition(const SuperSkillDefinition& definition, const char* reason);
    void ClearAllPendingOptimisticSuperSkillLevelHolds(const char* reason);
    void ClearPendingOptimisticSuperSkillLevelHold(int skillId);
    bool TryGetFreshPendingOptimisticSuperSkillLevelHold(int skillId, PendingOptimisticSuperSkillLevelHold& outHold);
    void ApplyOptimisticSuperSkillUpgradeObservation(int skillId, int carrierSkillId, int nextSkillLevel, int nextCarrierPoints);
    void ClearPendingSuperSkillUpgradePacketRewrite();
    bool IsPendingSuperSkillUpgradePacketRewriteFresh();
    void LoadSuperSkillRegistry();
    void ClearSuperSkillRegistry();
    void LoadNativeSkillInjectionRegistry();
    void ClearNativeSkillInjectionRegistry();
    uintptr_t GameLookupSkillEntryPointer(int skillId);
    void ApplyConfiguredPassiveBonusTooltipAugments(RetroSkillRuntimeState& state);

    // ========================================================================
    // 游戏技能等级查询（调用游戏原生 CALL）
    // 证据级别：A — IDA asm 直接确认所有调用约定
    //
    // sub_7DBC50: __thiscall(ECX=SkillDataMgr)
    //   push flags(0), push cachePtr(0), push skillId, push playerObj
    //   retn 10h (4个栈参数)
    //   返回值: signed int = 当前等级（含加成）
    //
    // sub_7DA7D0: __thiscall(ECX=SkillDataMgr)
    //   push cachePtr(0), push skillId, push playerObj
    //   retn 0Ch (3个栈参数)
    //   返回值: signed int = 基础等级
    //
    // sub_7DA4B0: __thiscall(ECX=SkillDataMgr, push skillId)
    //   返回 skillEntry 对象指针
    //
    // sub_5511C0: __thiscall(ECX=skillEntry)
    //   返回 unsigned int = 最大等级
    // ========================================================================

    DWORD GetSkillContext()
    {
        if (SafeIsBadReadPtr((void*)ADDR_CWndMan, 4))
        {
            return g_lastObservedLevelContext;
        }
        DWORD cwndMan = 0;
        __try
        {
            cwndMan = *(DWORD*)ADDR_CWndMan;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return g_lastObservedLevelContext;
        }
        if (!cwndMan || SafeIsBadReadPtr((void*)((uintptr_t)cwndMan + CWNDMAN_PLAYER_OFF), 4))
            return g_lastObservedLevelContext;

        DWORD context = 0;
        __try
        {
            context = *(DWORD*)((uintptr_t)cwndMan + CWNDMAN_PLAYER_OFF);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return g_lastObservedLevelContext;
        }

        if (!context || SafeIsBadReadPtr((void*)context, 4))
            return g_lastObservedLevelContext;
        return context;
    }

    DWORD TryGetLiveSkillContext()
    {
        if (SafeIsBadReadPtr((void*)ADDR_CWndMan, 4))
            return 0;

        DWORD cwndMan = 0;
        __try
        {
            cwndMan = *(DWORD*)ADDR_CWndMan;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return 0;
        }

        if (!cwndMan || SafeIsBadReadPtr((void*)((uintptr_t)cwndMan + CWNDMAN_PLAYER_OFF), 4))
            return 0;

        DWORD context = 0;
        __try
        {
            context = *(DWORD*)((uintptr_t)cwndMan + CWNDMAN_PLAYER_OFF);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return 0;
        }

        if (!context || SafeIsBadReadPtr((void*)context, 4))
            return 0;
        return context;
    }

    DWORD GetSkillDataMgr()
    {
        DWORD mgr = 0;
        __try
        {
            mgr = *(DWORD*)ADDR_SkillDataMgr;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            mgr = 0;
        }
        if (!mgr || SafeIsBadReadPtr((void*)mgr, 4))
            return g_lastObservedSkillDataMgr;
        return mgr;
    }

    DWORD TryGetLiveSkillDataMgr()
    {
        DWORD mgr = 0;
        __try
        {
            mgr = *(DWORD*)ADDR_SkillDataMgr;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            mgr = 0;
        }

        if (!mgr || SafeIsBadReadPtr((void*)mgr, 4))
            return 0;
        return mgr;
    }

    DWORD TryGetLiveStatusBar()
    {
        if (SafeIsBadReadPtr((void*)ADDR_StatusBar, 4))
            return 0;

        DWORD statusBar = 0;
        __try
        {
            statusBar = *(DWORD*)ADDR_StatusBar;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return 0;
        }

        if (!statusBar || SafeIsBadReadPtr((void*)statusBar, 4))
            return 0;
        return statusBar;
    }

    template <typename T>
    bool SafeReadValue(uintptr_t address, T& outValue)
    {
        if (!address || SafeIsBadReadPtr((void*)address, sizeof(T)))
            return false;

        __try
        {
            outValue = *(T*)address;
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    template <typename T>
    bool SafeWriteValue(uintptr_t address, const T& value)
    {
        if (!address || SafeIsBadReadPtr((void*)address, sizeof(T)))
            return false;

        __try
        {
            *(T*)address = value;
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    uintptr_t GetLiveSkillWndThis()
    {
        DWORD globalSkillWnd = *(DWORD*)ADDR_SkillWndEx;
        if (!globalSkillWnd || SafeIsBadReadPtr((void*)globalSkillWnd, 0x20))
            return 0;
        return globalSkillWnd;
    }

    DWORD TryGetCurrentIndependentBuffOwnerUserLocal();

    unsigned int RotL32(unsigned int value, unsigned int count)
    {
        count &= 31u;
        return (value << count) | (value >> ((32u - count) & 31u));
    }

    unsigned int RotR32(unsigned int value, unsigned int count)
    {
        count &= 31u;
        return (value >> count) | (value << ((32u - count) & 31u));
    }

    int GenerateIndependentBuffCipherKey()
    {
        typedef int (__fastcall *tGenerateCipherKeyFn)(void *seedPtr, void *edxUnused);
        tGenerateCipherKeyFn generateCipherKey = reinterpret_cast<tGenerateCipherKeyFn>(ADDR_4098C0);
        if (!generateCipherKey)
            return 0;
        return generateCipherKey(reinterpret_cast<void*>(ADDR_F631B8), nullptr);
    }

    bool ReadEncryptedTripletValueLocal(DWORD *base, size_t keyIndex, int *outValue)
    {
        if (!base || !outValue)
            return false;

        const size_t maxIndex = keyIndex + 2;
        if (SafeIsBadReadPtr(base, (maxIndex + 1) * sizeof(DWORD)))
            return false;

        const unsigned int key = static_cast<unsigned int>(base[keyIndex]);
        const unsigned int enc = static_cast<unsigned int>(base[keyIndex + 1]);
        const unsigned int check = static_cast<unsigned int>(base[keyIndex + 2]);
        if (enc + RotR32(key ^ 0xBAADF00Du, 5) != check)
            return false;

        *outValue = static_cast<int>(key ^ RotL32(enc, 5));
        return true;
    }

    bool WriteEncryptedTripletValueLocal(DWORD *base, size_t keyIndex, int plainValue)
    {
        if (!base)
            return false;

        const size_t maxIndex = keyIndex + 2;
        if (SafeIsBadWritePtr(base, (maxIndex + 1) * sizeof(DWORD)))
            return false;

        const int generatedKey = GenerateIndependentBuffCipherKey();
        if (generatedKey == 0)
            return false;

        const unsigned int key = static_cast<unsigned int>(generatedKey);
        const unsigned int enc = RotR32(static_cast<unsigned int>(plainValue) ^ key, 5);
        const unsigned int check = enc + RotR32(key ^ 0xBAADF00Du, 5);
        base[keyIndex] = key;
        base[keyIndex + 1] = enc;
        base[keyIndex + 2] = check;
        return true;
    }

    bool TryReadEncryptedShortValue(uintptr_t encodedAddress, short& outValue)
    {
        outValue = 0;

        unsigned char enc0 = 0;
        unsigned char enc1 = 0;
        unsigned char key0 = 0;
        unsigned char key1 = 0;
        unsigned int check = 0;
        if (!SafeReadValue(encodedAddress + 0, enc0) ||
            !SafeReadValue(encodedAddress + 1, enc1) ||
            !SafeReadValue(encodedAddress + 2, key0) ||
            !SafeReadValue(encodedAddress + 3, key1) ||
            !SafeReadValue(encodedAddress + 4, check))
        {
            return false;
        }

        unsigned int rolling = 0xBAADF00Du;
        unsigned short plainValue = 0;
        unsigned char* plainBytes = reinterpret_cast<unsigned char*>(&plainValue);
        const unsigned char encBytes[2] = { enc0, enc1 };
        const unsigned char keyBytes[2] = { key0, key1 };

        for (int i = 0; i < 2; ++i)
        {
            plainBytes[i] = static_cast<unsigned char>(encBytes[i] ^ keyBytes[i]);
            rolling = static_cast<unsigned int>(keyBytes[i]) + RotR32(rolling ^ encBytes[i], 5);
        }

        if (rolling != check)
            return false;

        outValue = static_cast<short>(plainValue);
        return true;
    }

    bool TryReadCurrentJobIdFromContext(uintptr_t context, int& outJobId)
    {
        outJobId = 0;
        if (!context)
            return false;

        short decodedJobId = 0;
        if (!TryReadEncryptedShortValue(context + 33, decodedJobId))
            return false;

        const int jobId = static_cast<int>(decodedJobId);
        if (jobId < 0 || jobId > 9999)
            return false;

        outJobId = jobId;
        return true;
    }

    bool TryReadCurrentPlayerJobId(int& outJobId)
    {
        outJobId = 0;

        const DWORD liveContext = TryGetLiveSkillContext();
        if (TryReadCurrentJobIdFromContext(liveContext, outJobId))
        {
            g_lastResolvedPlayerJobId = outJobId;
            g_hasLastResolvedPlayerJobId = true;
            return true;
        }

        DWORD userLocal = 0;
        uintptr_t characterStat = 0;
        if (SafeReadValue(ADDR_UserLocal, userLocal) &&
            userLocal &&
            SafeReadValue(static_cast<uintptr_t>(userLocal) + 8392u, characterStat) &&
            TryReadCurrentJobIdFromContext(characterStat, outJobId))
        {
            g_lastResolvedPlayerJobId = outJobId;
            g_hasLastResolvedPlayerJobId = true;
            return true;
        }

        return false;
    }

    bool TryGetCurrentOrCachedPlayerJobId(int& outJobId)
    {
        if (TryReadCurrentPlayerJobId(outJobId))
            return true;
        if (!g_hasLastResolvedPlayerJobId)
        {
            outJobId = 0;
            return false;
        }

        outJobId = g_lastResolvedPlayerJobId;
        return true;
    }

    void AppendCandidateIndexText(std::string& text, size_t keyIndex, int decodedValue, size_t& count, size_t maxCount)
    {
        if (count >= maxCount)
            return;
        if (!text.empty())
            text += ",";
        char buf[32] = {};
        std::snprintf(buf, sizeof(buf), "%u:%d", static_cast<unsigned int>(keyIndex), decodedValue);
        text += buf;
        ++count;
    }

    void RememberRecentPassiveAttackProbe(int skillId)
    {
        if (skillId <= 0)
            return;
        g_recentPassiveAttackProbeTickBySkillId[skillId] = GetTickCount();
    }

    bool TryGetRecentPassiveAttackProbeSkillId(int& outSkillId)
    {
        outSkillId = 0;
        if (g_recentPassiveAttackProbeTickBySkillId.empty())
            return false;

        const DWORD nowTick = GetTickCount();
        DWORD newestTick = 0;
        for (std::map<int, DWORD>::iterator it = g_recentPassiveAttackProbeTickBySkillId.begin();
             it != g_recentPassiveAttackProbeTickBySkillId.end();)
        {
            if (nowTick - it->second > kRecentPassiveAttackProbeWindowMs)
            {
                it = g_recentPassiveAttackProbeTickBySkillId.erase(it);
                continue;
            }

            if (it->second >= newestTick)
            {
                newestTick = it->second;
                outSkillId = it->first;
            }
            ++it;
        }

        return outSkillId > 0;
    }

    bool TryResolveLiveAbilityTripletMapping(int localOffset, DWORD*& outBase, size_t& outKeyIndex)
    {
        outBase = nullptr;
        outKeyIndex = 0;

        switch (localOffset)
        {
        case 0x08: outBase = reinterpret_cast<DWORD*>(ADDR_AbilityPrimaryCache); outKeyIndex = 9; return true;
        case 0x0C: outBase = reinterpret_cast<DWORD*>(ADDR_AbilityPrimaryCache); outKeyIndex = 12; return true;
        case 0x10: outBase = reinterpret_cast<DWORD*>(ADDR_AbilityPrimaryCache); outKeyIndex = 15; return true;
        case 0x14: outBase = reinterpret_cast<DWORD*>(ADDR_AbilityPrimaryCache); outKeyIndex = 18; return true;
        case 0x20: outBase = reinterpret_cast<DWORD*>(ADDR_AbilityPrimaryCache); outKeyIndex = 24; return true;
        case 0x24: outBase = reinterpret_cast<DWORD*>(ADDR_AbilityPrimaryCache); outKeyIndex = 27; return true;
        case 0x28: outBase = reinterpret_cast<DWORD*>(ADDR_AbilityExtendedCache); outKeyIndex = 117; return true;
        case 0x2C: outBase = reinterpret_cast<DWORD*>(ADDR_AbilityExtendedCache); outKeyIndex = 132; return true;
        case 0x30: outBase = reinterpret_cast<DWORD*>(ADDR_AbilityExtendedCache); outKeyIndex = 159; return true;
        case 0x34: outBase = reinterpret_cast<DWORD*>(ADDR_AbilityExtendedCache); outKeyIndex = 171; return true;
        case 0x38: outBase = reinterpret_cast<DWORD*>(ADDR_AbilityExtendedCache); outKeyIndex = 57; return true;
        case 0x3C: outBase = reinterpret_cast<DWORD*>(ADDR_AbilityExtendedCache); outKeyIndex = 87; return true;
        case 0x40: outBase = reinterpret_cast<DWORD*>(ADDR_AbilityExtendedCache); outKeyIndex = 72; return true;
        case 0x44: outBase = reinterpret_cast<DWORD*>(ADDR_AbilityExtendedCache); outKeyIndex = 102; return true;
        case 0xC8: outBase = reinterpret_cast<DWORD*>(ADDR_AbilityExtendedCache); outKeyIndex = 1712; return true;
        case 0xCC: outBase = reinterpret_cast<DWORD*>(ADDR_AbilityExtendedCache); outKeyIndex = 1724; return true;
        case 0xD0: outBase = reinterpret_cast<DWORD*>(ADDR_AbilityExtendedCache); outKeyIndex = 1736; return true;
        case 0xD4: outBase = reinterpret_cast<DWORD*>(ADDR_AbilityExtendedCache); outKeyIndex = 1748; return true;
        default:
            return false;
        }
    }

    void ApplyImmediateLiveAbilityCancelDelta(const LocalPotentialDeltaBuffer& values)
    {
        for (size_t index = 0; index < values.size(); ++index)
        {
            const int delta = values[index];
            if (delta == 0)
                continue;

            const int localOffset = static_cast<int>(index * sizeof(int));
            DWORD* base = nullptr;
            size_t keyIndex = 0;
            if (!TryResolveLiveAbilityTripletMapping(localOffset, base, keyIndex))
                continue;

            int currentValue = 0;
            if (!ReadEncryptedTripletValueLocal(base, keyIndex, &currentValue))
                continue;

            int nextValue = currentValue - delta;
            if (nextValue < 0)
                nextValue = 0;

            if (!WriteEncryptedTripletValueLocal(base, keyIndex, nextValue))
                continue;

            WriteLogFmt("[IndependentBuffOverlay] immediate live revert offset=0x%X delta=%d value=%d->%d",
                localOffset,
                delta,
                currentValue,
                nextValue);
        }
    }

    void ForceRefreshIndependentBuffUi(const char* reason)
    {
        const DWORD userLocal = TryGetCurrentIndependentBuffOwnerUserLocal();
        if (userLocal && !SafeIsBadReadPtr(reinterpret_cast<void*>(userLocal), 0x40))
        {
            __try
            {
                typedef void(__thiscall* tUserLocalBuffOffRefreshFn)(uintptr_t thisPtr);
                ((tUserLocalBuffOffRefreshFn)ADDR_BF4110)(userLocal);
            }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
                WriteLogFmt("[IndependentBuffOverlay] refresh userlocal BF4110 EXCEPTION reason=%s code=0x%08X",
                    reason ? reason : "unknown",
                    GetExceptionCode());

                __try
                {
                    typedef void(__thiscall* tUserLocalBuffOnRefreshFn)(uintptr_t thisPtr);
                    ((tUserLocalBuffOnRefreshFn)ADDR_BF3E60)(userLocal);
                }
                __except (EXCEPTION_EXECUTE_HANDLER)
                {
                    WriteLogFmt("[IndependentBuffOverlay] refresh userlocal BF3E60 EXCEPTION reason=%s code=0x%08X",
                        reason ? reason : "unknown",
                        GetExceptionCode());
                }
            }
        }

        DWORD statusBar = 0;
        SafeReadValue(ADDR_StatusBar, statusBar);
        if (statusBar && !SafeIsBadReadPtr(reinterpret_cast<void*>(statusBar), 0x40))
        {
            __try
            {
                typedef void(__thiscall* tStatusBarRefreshInternalFn)(uintptr_t thisPtr);
                ((tStatusBarRefreshInternalFn)ADDR_StatusBarRefreshInternal)(statusBar);
            }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
                WriteLogFmt("[IndependentBuffOverlay] refresh status bar EXCEPTION reason=%s code=0x%08X",
                    reason ? reason : "unknown",
                    GetExceptionCode());
            }

            __try
            {
                DWORD fnRefresh = ADDR_B9A5D0;
                __asm
                {
                    push 0
                    mov ecx, statusBar
                    call fnRefresh
                }
            }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
                WriteLogFmt("[IndependentBuffOverlay] dirty status bar EXCEPTION reason=%s code=0x%08X",
                    reason ? reason : "unknown",
                    GetExceptionCode());
            }
        }

        const uintptr_t skillWndThis = GetLiveSkillWndThis();
        if (skillWndThis && !SafeIsBadReadPtr(reinterpret_cast<void*>(skillWndThis), 0x40))
        {
            __try
            {
                typedef int(__thiscall* tSkillWndRefreshFn)(uintptr_t thisPtr);
                ((tSkillWndRefreshFn)ADDR_9E1770)(skillWndThis);
            }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
                WriteLogFmt("[IndependentBuffOverlay] refresh skill wnd EXCEPTION reason=%s code=0x%08X",
                    reason ? reason : "unknown",
                    GetExceptionCode());
            }
        }

        const HWND hwnd = GetForegroundWindow();
        DWORD hwndPid = 0;
        if (hwnd)
            GetWindowThreadProcessId(hwnd, &hwndPid);
        if (hwnd && hwndPid == GetCurrentProcessId())
        {
            InvalidateRect(hwnd, nullptr, FALSE);
            UpdateWindow(hwnd);
        }
    }

    // 查询当前技能等级（含加成）
    int GameGetSkillLevel(int skillId)
    {
        DWORD context = GetSkillContext();
        DWORD mgr = GetSkillDataMgr();
        const bool suppressResetFallback = ShouldSuppressNonNativeSuperSkillLevelFallback(skillId);
        if (!context || !mgr)
        {
            if (suppressResetFallback)
            {
                ClearTrackedNonNativeSuperSkillLevel(skillId, "game-current-noctx-reset-sync");
                return 0;
            }

            std::map<int, int>::const_iterator observedIt = g_observedCurrentLevelsBySkillId.find(skillId);
            if (observedIt != g_observedCurrentLevelsBySkillId.end())
                return observedIt->second;

            int persistentLevel = 0;
            return TryGetPersistentSuperSkillLevel(skillId, persistentLevel) ? persistentLevel : 0;
        }

        int result = 0;
        unsigned int* rowCache = reinterpret_cast<unsigned int*>(GameLookupSkillEntryPointer(skillId));
        DWORD fnAddr = ADDR_7DBC50;
        __try {
            __asm {
                push 1           // a5: includeBonus = 1
                lea eax, [rowCache]
                push eax         // a4: cachePtr = &rowCache
                push skillId     // a3: skillId
                push context     // a2: context
                mov ecx, mgr    // this = SkillDataMgr
                call fnAddr
                mov result, eax
            }
        } __except(EXCEPTION_EXECUTE_HANDLER) {
            result = 0;
        }
        if (result > 0)
        {
            RecordPersistentSuperSkillLevel(skillId, result, "game-current");
            if (IsKnownSuperSkillCarrierSkillId(skillId))
            {
                std::map<int, int>::iterator observedIt = g_observedCurrentLevelsBySkillId.find(skillId);
                if (observedIt == g_observedCurrentLevelsBySkillId.end())
                {
                    g_observedCurrentLevelsBySkillId[skillId] = result;
                }
                else if (result > observedIt->second && result != 1)
                {
                    observedIt->second = result;
                }
            }
            else
            {
                g_observedCurrentLevelsBySkillId[skillId] = result;
            }
        }
        else
        {
            if (suppressResetFallback)
            {
                ClearTrackedNonNativeSuperSkillLevel(skillId, "game-current-reset-sync");
            }
            else
            {
                std::map<int, int>::const_iterator observedIt = g_observedCurrentLevelsBySkillId.find(skillId);
                if (observedIt != g_observedCurrentLevelsBySkillId.end())
                    result = observedIt->second;
                else
                {
                    int persistentLevel = 0;
                    if (TryGetPersistentSuperSkillLevel(skillId, persistentLevel))
                        result = persistentLevel;
                }
            }
        }
        return result;
    }

    // 查询基础等级
    int GameGetBaseSkillLevel(int skillId)
    {
        DWORD context = GetSkillContext();
        DWORD mgr = GetSkillDataMgr();
        const bool suppressResetFallback = ShouldSuppressNonNativeSuperSkillLevelFallback(skillId);
        if (!context || !mgr)
        {
            if (suppressResetFallback)
            {
                ClearTrackedNonNativeSuperSkillLevel(skillId, "game-base-noctx-reset-sync");
                return 0;
            }

            std::map<int, int>::const_iterator observedIt = g_observedBaseLevelsBySkillId.find(skillId);
            if (observedIt != g_observedBaseLevelsBySkillId.end())
                return observedIt->second;

            int persistentLevel = 0;
            return TryGetPersistentSuperSkillLevel(skillId, persistentLevel) ? persistentLevel : 0;
        }

        int result = 0;
        unsigned int* rowCache = reinterpret_cast<unsigned int*>(GameLookupSkillEntryPointer(skillId));
        DWORD fnAddr = ADDR_7DA7D0;
        __try {
            __asm {
                lea eax, [rowCache]
                push eax         // a4: cachePtr = &rowCache
                push skillId     // a3: skillId
                push context     // a2: context
                mov ecx, mgr
                call fnAddr
                mov result, eax
            }
        } __except(EXCEPTION_EXECUTE_HANDLER) {
            result = 0;
        }
        if (result > 0)
        {
            RecordPersistentSuperSkillLevel(skillId, result, "game-base");
            if (IsKnownSuperSkillCarrierSkillId(skillId))
            {
                std::map<int, int>::iterator observedIt = g_observedBaseLevelsBySkillId.find(skillId);
                if (observedIt == g_observedBaseLevelsBySkillId.end())
                {
                    g_observedBaseLevelsBySkillId[skillId] = result;
                }
                else if (result > observedIt->second && result != 1)
                {
                    observedIt->second = result;
                }
            }
            else
            {
                g_observedBaseLevelsBySkillId[skillId] = result;
            }
        }
        else
        {
            if (suppressResetFallback)
            {
                ClearTrackedNonNativeSuperSkillLevel(skillId, "game-base-reset-sync");
            }
            else
            {
                std::map<int, int>::const_iterator observedIt = g_observedBaseLevelsBySkillId.find(skillId);
                if (observedIt != g_observedBaseLevelsBySkillId.end())
                    result = observedIt->second;
                else
                {
                    int persistentLevel = 0;
                    if (TryGetPersistentSuperSkillLevel(skillId, persistentLevel))
                        result = persistentLevel;
                }
            }
        }
        return result;
    }

    // 查询最大等级（通过 skillEntry 对象）
    uintptr_t GameLookupSkillEntryPointer(int skillId)
    {
        if (skillId <= 0)
            return 0;

        DWORD mgr = GetSkillDataMgr();
        if (!mgr)
            return 0;

        DWORD skillEntry = 0;
        DWORD fnLookup = ADDR_7DA4B0;
        __try {
            __asm {
                push skillId
                mov ecx, mgr
                call fnLookup
                mov skillEntry, eax
            }
        } __except(EXCEPTION_EXECUTE_HANDLER) {
            skillEntry = 0;
        }

        if (!skillEntry || IsBadReadPtr((void*)skillEntry, 0x40))
            return 0;

        return (uintptr_t)skillEntry;
    }

    int GameGetMaxSkillLevel(int skillId)
    {
        const uintptr_t skillEntry = GameLookupSkillEntryPointer(skillId);
        if (!skillEntry)
            return 0;

        int result = 0;
        DWORD fnMaxLevel = ADDR_5511C0;
        __try {
            __asm {
                mov ecx, skillEntry
                call fnMaxLevel
                mov result, eax
            }
        } __except(EXCEPTION_EXECUTE_HANDLER) {
            result = 0;
        }
        return result;
    }

    int GameGetSkillLevelDirectFromEntry(int skillId)
    {
        const uintptr_t skillEntry = GameLookupSkillEntryPointer(skillId);
        if (!skillEntry)
            return 0;

        int result = 0;
        DWORD fnLevel = ADDR_5511C0;
        __try {
            __asm {
                mov ecx, skillEntry
                call fnLevel
                mov result, eax
            }
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            result = 0;
        }

        if (result <= 0)
        {
            __try {
                // sub_5511C0 falls back to this[134] when the extra interface is absent.
                // Reading the backing field directly gives us one more chance to recover
                // the real bookkeeping value for hidden carrier skills.
                const int rawLevel = *(int*)(skillEntry + 134 * sizeof(DWORD));
                if (rawLevel > result)
                    result = rawLevel;
            } __except (EXCEPTION_EXECUTE_HANDLER) {
            }
        }

        return result > 0 ? result : 0;
    }

    // 刷新所有技能的游戏等级到 SkillManager
    // 仅当游戏已加载玩家角色时有效
    void RefreshSkillLevelsFromGame(SkillManager* manager)
    {
        if (!manager)
            return;

        DWORD context = GetSkillContext();
        DWORD mgr = GetSkillDataMgr();
        if (!context || !mgr)
            return;  // 玩家角色或 SkillDataMgr 尚未加载

        bool anyUpdated = false;
        for (int t = 0; t < manager->tabCount; ++t)
        {
            SkillTab& tab = manager->tabs[t];
            for (int i = 0; i < tab.count; ++i)
            {
                SkillItem& item = tab.skills[i];
                int gameLevel = GameGetSkillLevel(item.skillID);
                int gameMaxLevel = GameGetMaxSkillLevel(item.skillID);

                // 更新 maxLevel（游戏值优先）
                if (gameMaxLevel > 0)
                    item.maxLevel = gameMaxLevel;

                int newLevel = gameLevel;
                if (newLevel > item.maxLevel)
                    newLevel = item.maxLevel;
                if (newLevel < 0)
                    newLevel = 0;

                // 对非原生超级技能，本地等级由 plus 独立管理，
                // 只在游戏等级更高时同步（避免覆盖本地升级结果）。
                // 注意：非原生超级技能在游戏原生查询里经常长期返回 0，
                // 不能在 reset 请求刚发出时就把这些 0 当作权威结果回写到本地。
                if (item.isSuperSkill && !item.hasNativeUpgradeState)
                {
                    if (newLevel > item.level)
                    {
                        anyUpdated = true;
                        item.level = newLevel;
                    }
                }
                else if (item.level != newLevel)
                {
                    anyUpdated = true;
                    item.level = newLevel;
                }

                RefreshSkillNativeState(item);
            }
        }

        if (!g_initialGameLevelLoaded)
        {
            g_initialGameLevelLoaded = true;
            WriteLogFmt("[SkillBridge] initial game levels loaded successfully");
        }

        if (anyUpdated)
            WriteLogFmt("[SkillBridge] game level refresh: updated skill levels");
    }

    // Quick slot bindings (8 slots)
    QuickSlotBinding g_quickSlots[SKILL_BAR_TOTAL_SLOTS];
    struct PendingQuickSlotRestore
    {
        bool pending = false;
        int skillId = 0;
        int retryCount = 0;
        DWORD nextRetryTick = 0;
    };
    PendingQuickSlotRestore g_pendingQuickSlotRestores[SKILL_BAR_TOTAL_SLOTS];
    const DWORD kQuickSlotRetryIntervalMs = 500;
    const int kQuickSlotRetryMaxAttempts = 120;

    struct BridgeState
    {
        SkillOverlaySource managerSource;
        SkillOverlaySource gameSource;
        SkillOverlaySource* activeSource = nullptr;
        uintptr_t skillWndThis = 0;
        bool loggedFallback = false;
        const char* lastLoggedSource = nullptr;
        const char* savePath = nullptr;
    };

    BridgeState g_bridge;

    SkillManager* GetBridgeManager()
    {
        if (!g_bridge.managerSource.userData)
            return nullptr;

        struct MSS { SkillManager* m; };
        MSS* mss = static_cast<MSS*>(g_bridge.managerSource.userData);
        return mss ? mss->m : nullptr;
    }

    int ResolveAvailableSuperSkillPointsForCarrier(int carrierSkillId)
    {
        if (carrierSkillId <= 0)
            return 0;

        // Super SP carrier is a hidden bookkeeping skill. For this kind of skill,
        // native "current level" can collapse to a learned-flag-like value (often 1),
        // while the raw/base level still tracks the real server-side point count.
        const int baseLevel = GameGetBaseSkillLevel(carrierSkillId);
        const int currentLevel = GameGetSkillLevel(carrierSkillId);
        const int observedBaseLevel = GetObservedBaseSkillLevel(carrierSkillId);
        const int observedCurrentLevel = GetObservedCurrentSkillLevel(carrierSkillId);
        const bool hasObservedBaseLevel = HasObservedBaseSkillLevel(carrierSkillId);
        const bool hasObservedCurrentLevel = HasObservedCurrentSkillLevel(carrierSkillId);

        const int preferredBaseLevel = hasObservedBaseLevel ? observedBaseLevel : baseLevel;
        const int preferredCurrentLevel = hasObservedCurrentLevel ? observedCurrentLevel : currentLevel;

        if (preferredBaseLevel > 0 || hasObservedBaseLevel)
        {
            if (preferredCurrentLevel > 0 && preferredCurrentLevel != preferredBaseLevel)
            {
                static DWORD s_lastCarrierMismatchLogTick = 0;
                const DWORD nowTick = GetTickCount();
                if (nowTick - s_lastCarrierMismatchLogTick > 1000)
                {
                    s_lastCarrierMismatchLogTick = nowTick;
                    WriteLogFmt("[SuperSkill] carrier level mismatch skillId=%d base=%d current=%d observedBase=%d observedCurrent=%d (preferring base)",
                        carrierSkillId,
                        baseLevel,
                        currentLevel,
                        observedBaseLevel,
                        observedCurrentLevel);
                }
            }
            return preferredBaseLevel >= 0 ? preferredBaseLevel : 0;
        }

        if ((currentLevel == 1 || observedCurrentLevel == 1) && !hasObservedBaseLevel)
        {
            static DWORD s_lastCarrierLearnedFlagLogTick = 0;
            const DWORD nowTick = GetTickCount();
            if (nowTick - s_lastCarrierLearnedFlagLogTick > 1000)
            {
                s_lastCarrierLearnedFlagLogTick = nowTick;
                WriteLogFmt("[SuperSkill] carrier learned-flag fallback skillId=%d base=%d current=%d observedBase=%d observedCurrent=%d",
                    carrierSkillId,
                    baseLevel,
                    currentLevel,
                    observedBaseLevel,
                    observedCurrentLevel);
            }
        }

        if (hasObservedCurrentLevel)
            return preferredCurrentLevel >= 0 ? preferredCurrentLevel : 0;
        return currentLevel > 0 ? currentLevel : 0;
    }

    int ResolveSuperSkillCarrierSkillId(const SkillItem& item)
    {
        if (item.superSpCarrierSkillId > 0)
            return item.superSpCarrierSkillId;
        return g_defaultSuperSpCarrierSkillId;
    }

    int ResolveSuperSkillCarrierSkillId(const SkillEntry& entry)
    {
        if (entry.superSpCarrierSkillId > 0)
            return entry.superSpCarrierSkillId;
        return g_defaultSuperSpCarrierSkillId;
    }

    int ResolveAnySuperSkillCarrierSkillId()
    {
        if (g_defaultSuperSpCarrierSkillId > 0)
            return g_defaultSuperSpCarrierSkillId;

        for (std::map<int, SuperSkillDefinition>::const_iterator it = g_superSkillsBySkillId.begin();
             it != g_superSkillsBySkillId.end();
             ++it)
        {
            if (it->second.superSpCarrierSkillId > 0)
                return it->second.superSpCarrierSkillId;
        }

        return 0;
    }

    bool IsKnownSuperSkillCarrierSkillId(int skillId)
    {
        if (skillId <= 0)
            return false;
        if (g_defaultSuperSpCarrierSkillId > 0 && skillId == g_defaultSuperSpCarrierSkillId)
            return true;

        for (std::map<int, SuperSkillDefinition>::const_iterator it = g_superSkillsBySkillId.begin();
             it != g_superSkillsBySkillId.end();
             ++it)
        {
            if (it->second.superSpCarrierSkillId > 0 && it->second.superSpCarrierSkillId == skillId)
                return true;
        }
        return false;
    }

    void NormalizeVisibleJobIds(std::vector<int>& visibleJobIds)
    {
        visibleJobIds.erase(
            std::remove_if(
                visibleJobIds.begin(),
                visibleJobIds.end(),
                [](int jobId) { return jobId < 0; }),
            visibleJobIds.end());
        std::sort(visibleJobIds.begin(), visibleJobIds.end());
        visibleJobIds.erase(
            std::unique(visibleJobIds.begin(), visibleJobIds.end()),
            visibleJobIds.end());
    }

    bool DoesSuperSkillMatchCurrentJob(const SuperSkillDefinition& definition)
    {
        if (definition.visibleJobIds.empty())
            return true;

        int currentJobId = 0;
        if (!TryGetCurrentOrCachedPlayerJobId(currentJobId))
            return true;

        return std::find(
                   definition.visibleJobIds.begin(),
                   definition.visibleJobIds.end(),
                   currentJobId) != definition.visibleJobIds.end();
    }

    bool FindMountedRuntimeSkillDefinitionByMountItemId(
        int mountItemId,
        MountedRuntimeSkillKind kind,
        SuperSkillDefinition& outDefinition)
    {
        if (mountItemId <= 0)
            return false;

        int preferredSkillId = 0;
        if (TryGetPreferredMountedMovementOverrideSkillId(mountItemId, preferredSkillId) &&
            preferredSkillId > 0)
        {
            SuperSkillDefinition preferredDefinition = {};
            if (FindSuperSkillDefinition(preferredSkillId, preferredDefinition) &&
                preferredDefinition.mountItemId == mountItemId &&
                ResolveMountedRuntimeSkillIdFromDefinition(preferredDefinition, kind) > 0 &&
                DoesSuperSkillMatchCurrentJob(preferredDefinition))
            {
                outDefinition = preferredDefinition;
                return true;
            }
        }

        bool hasFallback = false;
        SuperSkillDefinition fallbackDefinition = {};
        for (std::map<int, SuperSkillDefinition>::const_iterator it = g_superSkillsBySkillId.begin();
             it != g_superSkillsBySkillId.end();
             ++it)
        {
            const SuperSkillDefinition& definition = it->second;
            if (definition.mountItemId != mountItemId ||
                ResolveMountedRuntimeSkillIdFromDefinition(definition, kind) <= 0)
            {
                continue;
            }
            if (!DoesSuperSkillMatchCurrentJob(definition))
                continue;
            if (GameGetSkillLevel(definition.skillId) > 0)
            {
                outDefinition = definition;
                return true;
            }
            if (!hasFallback)
            {
                fallbackDefinition = definition;
                hasFallback = true;
            }
        }

        if (!hasFallback)
            return false;

        outDefinition = fallbackDefinition;
        return true;
    }

    bool FindMountedDoubleJumpDefinitionByMountItemId(int mountItemId, SuperSkillDefinition& outDefinition)
    {
        return FindMountedRuntimeSkillDefinitionByMountItemId(
            mountItemId,
            MountedRuntimeSkillKind_DoubleJump,
            outDefinition);
    }

    bool FindMountedDemonJumpDefinitionByMountItemId(int mountItemId, SuperSkillDefinition& outDefinition)
    {
        return FindMountedRuntimeSkillDefinitionByMountItemId(
            mountItemId,
            MountedRuntimeSkillKind_DemonJump,
            outDefinition);
    }

    bool DefinitionHasMountMovementIdentity(const SuperSkillDefinition& definition)
    {
        return definition.mountItemId > 0 || definition.mountTamingMobId > 0;
    }

    bool DefinitionHasMountMovementOverride(const SuperSkillDefinition& definition)
    {
        return DefinitionHasMountMovementIdentity(definition) &&
               (definition.hasMountSpeedOverride ||
                definition.hasMountJumpOverride ||
                definition.hasMountFsOverride ||
                definition.hasMountSwimOverride);
    }

    bool DefinitionParticipatesInMountMovementSelection(const SuperSkillDefinition& definition)
    {
        return DefinitionHasMountMovementIdentity(definition) &&
               (definition.useNativeMountMovement ||
                DefinitionHasMountMovementOverride(definition));
    }

    bool DoesMountedMovementDefinitionMatch(
        const SuperSkillDefinition& definition,
        int mountItemId,
        int tamingMobId)
    {
        bool matched = false;

        if (definition.mountItemId > 0)
        {
            if (mountItemId <= 0 || definition.mountItemId != mountItemId)
                return false;
            matched = true;
        }

        if (definition.mountTamingMobId > 0 && tamingMobId > 0)
        {
            if (definition.mountTamingMobId == tamingMobId)
            {
                matched = true;
            }
            else if (!matched)
            {
                return false;
            }
        }

        return matched;
    }

    void PopulateMountedMovementOverrideFromDefinition(
        const SuperSkillDefinition& definition,
        int tamingMobId,
        MountedMovementOverride& outOverride)
    {
        outOverride = MountedMovementOverride();
        outOverride.matched = true;
        outOverride.useNativeMovement = definition.useNativeMountMovement;
        outOverride.sourceSkillId = definition.skillId;
        outOverride.mountItemId = definition.mountItemId;
        outOverride.tamingMobId = definition.mountTamingMobId > 0 ? definition.mountTamingMobId : tamingMobId;
        if (definition.useNativeMountMovement)
        {
            // Explicit "unlock-only" mode: keep mount identity/selection so
            // double-jump and soaring routing still know which skill owns the
            // mount, but do not overwrite the movement values read from the
            // client img/cache.
            return;
        }

        // Mount movement fields are optional. If a config omits mountSpeed /
        // mountJump / mountFs / mountSwim, the corresponding has* flag stays
        // false and runtime should keep the original movement values loaded
        // from the client img/cache instead of forcing a replacement value.
        outOverride.hasSpeed = definition.hasMountSpeedOverride;
        outOverride.speed = definition.mountSpeedOverride;
        outOverride.hasJump = definition.hasMountJumpOverride;
        outOverride.jump = definition.mountJumpOverride;
        outOverride.hasFs = definition.hasMountFsOverride;
        outOverride.fs = definition.mountFsOverride;
        outOverride.hasSwim = definition.hasMountSwimOverride;
        outOverride.swim = definition.mountSwimOverride;

        if (outOverride.hasSwim &&
            outOverride.swim > 0.0 &&
            !outOverride.hasFs)
        {
            // Some mounted flight paths still surface the legacy fs slot even when
            // the configured flying/swim speed only came from the swim override.
            // Respect explicit mountFs values from config; only synthesize fs when
            // the definition omitted it entirely.
            outOverride.hasFs = true;
            outOverride.fs = outOverride.swim;
        }
    }

    bool TryResolveMountedMovementOverrideDefinitionForSkill(
        int skillId,
        int mountItemId,
        int tamingMobId,
        SuperSkillDefinition& outDefinition)
    {
        if (skillId <= 0)
            return false;

        SuperSkillDefinition definition = {};
        if (!FindSuperSkillDefinition(skillId, definition) ||
            !DefinitionParticipatesInMountMovementSelection(definition))
        {
            return false;
        }

        if (!DoesMountedMovementDefinitionMatch(definition, mountItemId, tamingMobId))
            return false;

        outDefinition = definition;
        return true;
    }

    bool TryResolveMountedMovementSelectionSkillId(
        int mountItemId,
        int candidateSkillId,
        int tamingMobId,
        int& outSkillId)
    {
        outSkillId = 0;
        if (mountItemId <= 0)
            return false;

        SuperSkillDefinition definition = {};
        if (candidateSkillId > 0 &&
            TryResolveMountedMovementOverrideDefinitionForSkill(
                candidateSkillId,
                mountItemId,
                tamingMobId,
                definition))
        {
            outSkillId = candidateSkillId;
            return true;
        }

        if (IsActiveNativeReleaseContextFresh() &&
            g_activeNativeRelease.customSkillId > 0 &&
            TryResolveMountedMovementOverrideDefinitionForSkill(
                g_activeNativeRelease.customSkillId,
                mountItemId,
                tamingMobId,
                definition))
        {
            outSkillId = g_activeNativeRelease.customSkillId;
            return true;
        }

        int preferredSkillId = 0;
        if (TryGetPreferredMountedMovementOverrideSkillId(mountItemId, preferredSkillId) &&
            TryResolveMountedMovementOverrideDefinitionForSkill(
                preferredSkillId,
                mountItemId,
                tamingMobId,
                definition))
        {
            outSkillId = preferredSkillId;
            return true;
        }

        return false;
    }

    bool FindMountedMovementOverrideDefinition(int mountItemId, int tamingMobId, SuperSkillDefinition& outDefinition)
    {
        if (mountItemId <= 0 && tamingMobId <= 0)
            return false;

        int preferredSkillId = 0;
        bool hasJobFallback = false;
        bool hasAnyFallback = false;
        SuperSkillDefinition jobFallbackDefinition = {};
        SuperSkillDefinition anyFallbackDefinition = {};

        if (TryResolveMountedMovementSelectionSkillId(
                mountItemId,
                0,
                tamingMobId,
                preferredSkillId) &&
            TryResolveMountedMovementOverrideDefinitionForSkill(
                preferredSkillId,
                mountItemId,
                tamingMobId,
                outDefinition))
        {
            return true;
        }

        for (std::map<int, SuperSkillDefinition>::const_iterator it = g_superSkillsBySkillId.begin();
             it != g_superSkillsBySkillId.end();
             ++it)
        {
            const SuperSkillDefinition& definition = it->second;
            if (!DefinitionParticipatesInMountMovementSelection(definition))
                continue;
            if (!DoesMountedMovementDefinitionMatch(definition, mountItemId, tamingMobId))
                continue;

            const bool jobMatches = DoesSuperSkillMatchCurrentJob(definition);
            const bool learned = GameGetSkillLevel(definition.skillId) > 0;
            if (jobMatches && learned)
            {
                outDefinition = definition;
                return true;
            }

            if (jobMatches && !hasJobFallback)
            {
                jobFallbackDefinition = definition;
                hasJobFallback = true;
            }

            if (!hasAnyFallback)
            {
                anyFallbackDefinition = definition;
                hasAnyFallback = true;
            }
        }

        if (IsActiveNativeReleaseContextFresh() &&
            g_activeNativeRelease.customSkillId > 0)
        {
            if (TryResolveMountedMovementOverrideDefinitionForSkill(
                    g_activeNativeRelease.customSkillId,
                    mountItemId,
                    tamingMobId,
                    outDefinition))
            {
                return true;
            }
        }

        if (hasJobFallback)
        {
            outDefinition = jobFallbackDefinition;
            return true;
        }

        if (hasAnyFallback)
        {
            outDefinition = anyFallbackDefinition;
            return true;
        }

        return false;
    }

    std::string BuildSuperSkillDisplayName(const std::string& skillName, const SuperSkillDefinition& definition)
    {
        if (definition.visibleJobLabel.empty())
            return skillName;

        std::string displayName = skillName;
        if (!displayName.empty())
            displayName += " ";
        displayName += definition.visibleJobLabel;
        return displayName;
    }

    bool ShouldShowSuperSkillInOverlay(int skillId, const SuperSkillDefinition& definition)
    {
        if (!DoesSuperSkillMatchCurrentJob(definition))
            return false;
        if (!definition.showInSuperWhenLearned)
            return true;
        return GameGetSkillLevel(skillId) > 0;
    }

    void RebuildOverlayLearnedVisibilitySnapshot()
    {
        g_overlayLearnedVisibilityBySkillId.clear();
        for (std::map<int, SuperSkillDefinition>::const_iterator it = g_superSkillsBySkillId.begin();
             it != g_superSkillsBySkillId.end();
             ++it)
        {
            const SuperSkillDefinition& def = it->second;
            if (!def.showInSuperWhenLearned || def.skillId <= 0)
                continue;
            g_overlayLearnedVisibilityBySkillId[def.skillId] = (GameGetSkillLevel(def.skillId) > 0);
        }
    }

    bool UpdateOverlayLearnedVisibilitySnapshot()
    {
        bool changed = false;

        for (std::map<int, SuperSkillDefinition>::const_iterator it = g_superSkillsBySkillId.begin();
             it != g_superSkillsBySkillId.end();
             ++it)
        {
            const SuperSkillDefinition& def = it->second;
            if (!def.showInSuperWhenLearned || def.skillId <= 0)
                continue;

            const bool learned = (GameGetSkillLevel(def.skillId) > 0);
            std::map<int, bool>::iterator visIt = g_overlayLearnedVisibilityBySkillId.find(def.skillId);
            if (visIt == g_overlayLearnedVisibilityBySkillId.end())
            {
                g_overlayLearnedVisibilityBySkillId[def.skillId] = learned;
                changed = true;
                continue;
            }

            if (visIt->second != learned)
            {
                visIt->second = learned;
                changed = true;
            }
        }

        for (std::map<int, bool>::iterator it = g_overlayLearnedVisibilityBySkillId.begin();
             it != g_overlayLearnedVisibilityBySkillId.end(); )
        {
            SuperSkillDefinition def = {};
            if (!FindSuperSkillDefinition(it->first, def) || !def.showInSuperWhenLearned)
            {
                it = g_overlayLearnedVisibilityBySkillId.erase(it);
                changed = true;
                continue;
            }
            ++it;
        }

        return changed;
    }

    void QueuePendingQuickSlotRestore(int slotIndex, int skillId, const char* reason)
    {
        if (slotIndex < 0 || slotIndex >= SKILL_BAR_TOTAL_SLOTS || skillId <= 0)
            return;

        PendingQuickSlotRestore& pending = g_pendingQuickSlotRestores[slotIndex];
        pending.pending = true;
        pending.skillId = skillId;
        pending.retryCount = 0;
        pending.nextRetryTick = GetTickCount() + kQuickSlotRetryIntervalMs;
        WriteLogFmt("[SkillBridge] queue quickSlot restore slot=%d skillId=%d reason=%s",
            slotIndex, skillId, reason ? reason : "unknown");
    }

    void ClearPendingQuickSlotRestore(int slotIndex)
    {
        if (slotIndex < 0 || slotIndex >= SKILL_BAR_TOTAL_SLOTS)
            return;
        g_pendingQuickSlotRestores[slotIndex] = PendingQuickSlotRestore{};
    }

    void TryRestorePendingQuickSlots()
    {
        const DWORD now = GetTickCount();
        for (int slotIndex = 0; slotIndex < SKILL_BAR_TOTAL_SLOTS; ++slotIndex)
        {
            PendingQuickSlotRestore& pending = g_pendingQuickSlotRestores[slotIndex];
            if (!pending.pending)
                continue;
            if (pending.nextRetryTick && now < pending.nextRetryTick)
                continue;

            if (g_quickSlots[slotIndex].skillId != pending.skillId)
            {
                ClearPendingQuickSlotRestore(slotIndex);
                continue;
            }

            const bool nativeOk = ::SkillOverlayBridgeAssignSkillToQuickSlot(slotIndex, pending.skillId);
            if (nativeOk)
            {
                WriteLogFmt("[SkillBridge] retry quickSlot[%d] skillId=%d native=OK tries=%d",
                    slotIndex, pending.skillId, pending.retryCount + 1);
                ClearPendingQuickSlotRestore(slotIndex);
                continue;
            }

            ++pending.retryCount;
            if (pending.retryCount >= kQuickSlotRetryMaxAttempts)
            {
                WriteLogFmt("[SkillBridge] retry quickSlot[%d] skillId=%d native=FAIL giveup tries=%d",
                    slotIndex, pending.skillId, pending.retryCount);
                ClearPendingQuickSlotRestore(slotIndex);
                continue;
            }

            pending.nextRetryTick = now + kQuickSlotRetryIntervalMs;
        }
    }

    bool ShouldHideSuperSkillInNativeList(int skillId, const SuperSkillDefinition& definition)
    {
        if (!DoesSuperSkillMatchCurrentJob(definition))
            return true;

        if (!definition.hideFromNativeSkillWnd)
            return false;

        if (definition.showInNativeWhenLearned)
            return GameGetSkillLevel(skillId) <= 0;

        return true;
    }

    void ApplySuperSkillStateToEntries(std::vector<SkillEntry>& skills, RetroSkillRuntimeState& state)
    {
        int resolvedCarrierSkillId = state.superSkillCarrierSkillId;
        int resolvedCarrierPoints = state.superSkillPoints;

        for (size_t i = 0; i < skills.size(); ++i)
        {
            SkillEntry& entry = skills[i];
            if (!entry.isSuperSkill)
                continue;

            const int carrierSkillId = ResolveSuperSkillCarrierSkillId(entry);
            const int availablePoints = ResolveAvailableSuperSkillPointsForCarrier(carrierSkillId);
            const bool canUseSuperSp = (carrierSkillId > 0 && availablePoints >= entry.superSpCost);

            if (carrierSkillId > 0 && (resolvedCarrierSkillId <= 0 || carrierSkillId == resolvedCarrierSkillId))
            {
                resolvedCarrierSkillId = carrierSkillId;
                resolvedCarrierPoints = availablePoints;
            }

            entry.canUpgrade = entry.enabled &&
                (entry.level < entry.maxLevel) &&
                (entry.allowNativeUpgradeFallback || canUseSuperSp);
            entry.showDisabledIcon = !entry.enabled || (!entry.isLearned && !entry.canUpgrade);
            entry.canUse = entry.enabled && entry.isLearned && !entry.isPassive && !entry.isOnCooldown;
            entry.canDrag = entry.enabled && entry.isLearned && !entry.isPassive;
        }

        state.superSkillCarrierSkillId = resolvedCarrierSkillId;
        state.superSkillPoints = resolvedCarrierPoints;
        state.hasSuperSkillData = state.hasSuperSkillData || !g_superSkillsBySkillId.empty();
    }

    bool ResolvePassiveValueForLevel(
        int sourceSkillId,
        int sourceSkillLevel,
        const PassiveValueSpec& spec,
        int& outValue)
    {
        outValue = 0;
        if (sourceSkillId <= 0 || sourceSkillLevel <= 0)
            return false;

        switch (spec.type)
        {
        case PassiveValueSpecType_Fixed:
            outValue = spec.fixedValue;
            return true;
        case PassiveValueSpecType_SkillField:
            if (!spec.skillFieldName.empty() &&
                SkillLocalDataGetLevelValueInt(sourceSkillId, sourceSkillLevel, spec.skillFieldName.c_str(), outValue))
            {
                return true;
            }
            return false;
        default:
            return false;
        }
    }

    std::string ResolveSkillNameForTooltip(int skillId)
    {
        std::string skillName;
        if (SkillLocalDataGetName(skillId, skillName) && !skillName.empty())
            return skillName;

        char buf[16] = {};
        sprintf_s(buf, "#%07d", skillId);
        return buf;
    }

    void AppendTooltipLine(std::string& text, const std::string& line)
    {
        if (line.empty())
            return;

        if (!text.empty())
            text += "\n";
        text += line;
    }

    std::string BuildPassiveBonusDeltaText(int damagePercent, int ignoreDefensePercent, int attackCount, int mobCount)
    {
        std::string text;
        char buf[64] = {};

        if (damagePercent != 0)
        {
            sprintf_s(buf, "伤害+%d%%", damagePercent);
            if (!text.empty())
                text += " ";
            text += buf;
        }
        if (ignoreDefensePercent != 0)
        {
            sprintf_s(buf, "无视防御+%d%%", ignoreDefensePercent);
            if (!text.empty())
                text += " ";
            text += buf;
        }
        if (attackCount != 0)
        {
            sprintf_s(buf, "攻击次数+%d", attackCount);
            if (!text.empty())
                text += " ";
            text += buf;
        }
        if (mobCount != 0)
        {
            sprintf_s(buf, "攻击怪物+%d", mobCount);
            if (!text.empty())
                text += " ";
            text += buf;
        }

        return text;
    }

    std::string BuildPassiveBonusTargetLabel(const std::vector<int>& targetSkillIds)
    {
        if (targetSkillIds.empty())
            return std::string();

        std::string text;
        const size_t limit = targetSkillIds.size() > 4 ? 4 : targetSkillIds.size();
        for (size_t i = 0; i < limit; ++i)
        {
            if (!text.empty())
                text += "、";
            text += ResolveSkillNameForTooltip(targetSkillIds[i]);
        }

        if (targetSkillIds.size() > limit)
            text += " 等";
        return text;
    }

    bool TryBuildPassiveBonusSummaryLine(
        const PassiveBonusDefinition& bonus,
        int sourceSkillLevel,
        std::string& outLine)
    {
        outLine.clear();
        if (bonus.sourceSkillId <= 0 || sourceSkillLevel <= 0)
            return false;

        int damagePercent = 0;
        int ignoreDefensePercent = 0;
        int attackCount = 0;
        int mobCount = 0;
        const bool hasDamage = ResolvePassiveValueForLevel(bonus.sourceSkillId, sourceSkillLevel, bonus.damagePercent, damagePercent);
        const bool hasIgnore = ResolvePassiveValueForLevel(bonus.sourceSkillId, sourceSkillLevel, bonus.ignoreDefensePercent, ignoreDefensePercent);
        const bool hasAttackCount = ResolvePassiveValueForLevel(bonus.sourceSkillId, sourceSkillLevel, bonus.attackCount, attackCount);
        const bool hasMobCount = ResolvePassiveValueForLevel(bonus.sourceSkillId, sourceSkillLevel, bonus.mobCount, mobCount);
        if (!hasDamage && !hasIgnore && !hasAttackCount && !hasMobCount)
            return false;

        const std::string deltaText = BuildPassiveBonusDeltaText(damagePercent, ignoreDefensePercent, attackCount, mobCount);
        if (deltaText.empty())
            return false;

        outLine = "[超级被动] ";
        const std::string targetLabel = BuildPassiveBonusTargetLabel(bonus.targetSkillIds);
        if (!targetLabel.empty())
        {
            outLine += targetLabel;
            outLine += ": ";
        }
        outLine += deltaText;
        return true;
    }

    struct PassiveBonusAggregate
    {
        int damagePercent = 0;
        int ignoreDefensePercent = 0;
        int attackCount = 0;
        int mobCount = 0;
    };

    void ApplyPassiveBonusAggregate(
        const PassiveBonusDefinition& bonus,
        int sourceSkillLevel,
        PassiveBonusAggregate& aggregate)
    {
        int value = 0;
        if (ResolvePassiveValueForLevel(bonus.sourceSkillId, sourceSkillLevel, bonus.damagePercent, value))
            aggregate.damagePercent += value;
        if (ResolvePassiveValueForLevel(bonus.sourceSkillId, sourceSkillLevel, bonus.ignoreDefensePercent, value))
            aggregate.ignoreDefensePercent += value;
        if (ResolvePassiveValueForLevel(bonus.sourceSkillId, sourceSkillLevel, bonus.attackCount, value))
            aggregate.attackCount += value;
        if (ResolvePassiveValueForLevel(bonus.sourceSkillId, sourceSkillLevel, bonus.mobCount, value))
            aggregate.mobCount += value;
    }

    std::string BuildPassiveAggregateTooltipLine(const PassiveBonusAggregate& aggregate)
    {
        const std::string deltaText = BuildPassiveBonusDeltaText(
            aggregate.damagePercent,
            aggregate.ignoreDefensePercent,
            aggregate.attackCount,
            aggregate.mobCount);
        if (deltaText.empty())
            return std::string();

        std::string line = "[超级被动加成] ";
        line += deltaText;
        return line;
    }

    void ApplyConfiguredPassiveBonusTooltipAugments(RetroSkillRuntimeState& state)
    {
        (void)state;
    }

    const char* PacketRouteToString(CustomSkillPacketRoute route)
    {
        switch (route)
        {
        case CustomSkillPacketRoute_CloseRange:  return "close_range";
        case CustomSkillPacketRoute_RangedAttack:return "ranged_attack";
        case CustomSkillPacketRoute_MagicAttack: return "magic_attack";
        case CustomSkillPacketRoute_SpecialMove: return "special_move";
        case CustomSkillPacketRoute_SkillEffect: return "skill_effect";
        case CustomSkillPacketRoute_CancelBuff:  return "cancel_buff";
        case CustomSkillPacketRoute_SpecialAttack:return "special_attack";
        default:                                 return "none";
        }
    }

    const char* ReleaseClassToString(CustomSkillReleaseClass releaseClass)
    {
        switch (releaseClass)
        {
        case CustomSkillReleaseClass_NativeB31722: return "native_b31722";
        case CustomSkillReleaseClass_NativeClassifierProxy: return "native_classifier_proxy";
        default:                                   return "none";
        }
    }

    CustomSkillPacketRoute ParsePacketRouteName(const std::string& value)
    {
        std::string text = value;
        std::transform(text.begin(), text.end(), text.begin(), [](unsigned char ch) { return (char)tolower(ch); });

        if (text == "close_range" || text == "close_range_attack" || text == "melee")
            return CustomSkillPacketRoute_CloseRange;
        if (text == "ranged_attack" || text == "ranged" || text == "bow")
            return CustomSkillPacketRoute_RangedAttack;
        if (text == "magic_attack" || text == "magic" || text == "spell")
            return CustomSkillPacketRoute_MagicAttack;
        if (text == "special_move" || text == "buff" || text == "special")
            return CustomSkillPacketRoute_SpecialMove;
        if (text == "skill_effect" || text == "effect")
            return CustomSkillPacketRoute_SkillEffect;
        if (text == "cancel_buff" || text == "buff_cancel")
            return CustomSkillPacketRoute_CancelBuff;
        if (text == "special_attack")
            return CustomSkillPacketRoute_SpecialAttack;
        if (text == "passive_energy")
            return CustomSkillPacketRoute_CloseRange;
        return CustomSkillPacketRoute_None;
    }

    CustomSkillReleaseClass ParseReleaseClassName(const std::string& value)
    {
        std::string text = value;
        std::transform(text.begin(), text.end(), text.begin(), [](unsigned char ch) { return (char)tolower(ch); });

        if (text == "native_b31722" || text == "b31722" || text == "branch_b31722")
            return CustomSkillReleaseClass_NativeB31722;
        if (text == "native_classifier_proxy" || text == "classifier_proxy" ||
            text == "native_inherit_classifier" || text == "inherit_classifier")
            return CustomSkillReleaseClass_NativeClassifierProxy;
        return CustomSkillReleaseClass_None;
    }

    SuperSkillDefinition::IndependentDisplayMode ParseIndependentDisplayMode(const std::string& value)
    {
        std::string text = value;
        std::transform(text.begin(), text.end(), text.begin(), [](unsigned char ch) { return (char)tolower(ch); });
        if (text == "overlay" || text == "custom")
            return SuperSkillDefinition::IndependentDisplayMode_Overlay;
        if (text == "both" || text == "native_overlay")
            return SuperSkillDefinition::IndependentDisplayMode_Both;
        if (text == "none" || text == "off")
            return SuperSkillDefinition::IndependentDisplayMode_None;
        return SuperSkillDefinition::IndependentDisplayMode_Native;
    }

    unsigned long long MakeProxyRouteKey(int proxySkillId, CustomSkillPacketRoute route)
    {
        return (static_cast<unsigned long long>(static_cast<unsigned int>(route)) << 32) |
               static_cast<unsigned int>(proxySkillId);
    }

    unsigned long long MakeIndependentBuffMaskKey(int position, unsigned int value)
    {
        return (static_cast<unsigned long long>(static_cast<unsigned int>(position)) << 32) |
               static_cast<unsigned int>(value);
    }

    std::string NormalizeBuffStatIdentifier(const std::string& value)
    {
        std::string normalized;
        normalized.reserve(value.size());
        for (size_t i = 0; i < value.size(); ++i)
        {
            const unsigned char ch = static_cast<unsigned char>(value[i]);
            if ((ch >= 'a' && ch <= 'z') ||
                (ch >= 'A' && ch <= 'Z') ||
                (ch >= '0' && ch <= '9') ||
                ch == '_')
            {
                normalized.push_back(static_cast<char>(toupper(ch)));
            }
        }

        if (normalized == "PDD")
            return "WDEF";
        if (normalized == "MDD")
            return "MDEF";
        if (normalized == "PAD")
            return "WATK";
        if (normalized == "MAD")
            return "MATK";
        if (normalized == "EVA")
            return "AVOID";
        if (normalized == "ACCR")
            return "ACCPERCENT";
        if (normalized == "EVAR")
            return "AVOIDPERCENT";
        if (normalized == "PADR")
            return "WATKPERCENT";
        if (normalized == "MADR")
            return "MATKPERCENT";
        if (normalized == "PDDR")
            return "WDEFPERCENT";
        if (normalized == "MDDR")
            return "MDEFPERCENT";
        if (normalized == "ALLSKILL" || normalized == "ALLSKILLLEVEL")
            return "ALLSKILLLEVEL";
        if (normalized == "INCDAMR")
            return "DAMAGEPERCENT";
        if (normalized == "BOSSDAMAGE" || normalized == "BOSSDAMAGEPERCENT" || normalized == "BOSSDAMR")
            return "BOSSDAMAGEPERCENT";
        if (normalized == "IGNORETARGETDE")
            return "IGNOREDEFENSEPERCENT";
        if (normalized == "CRITICALDAMAGEMIN")
            return "CRITICALMINDAMAGE";
        if (normalized == "CRITICALDAMAGEMAX")
            return "CRITICALMAXDAMAGE";
        return normalized;
    }

    const NativeBuffMaskDefinition* FindNativeBuffMaskDefinition(const std::string& rawName)
    {
        const std::string normalized = NormalizeBuffStatIdentifier(rawName);
        if (normalized.empty())
            return nullptr;

        for (size_t i = 0; i < ARRAYSIZE(kNativeBuffMaskDefinitions); ++i)
        {
            if (normalized == kNativeBuffMaskDefinitions[i].name)
                return &kNativeBuffMaskDefinitions[i];
        }
        return nullptr;
    }

    bool TryResolveNativeBuffMaskDefinition(
        const std::string& rawName,
        int& outPosition,
        unsigned int& outValue,
        std::string& outValueField,
        bool& outIsStacked)
    {
        outPosition = 0;
        outValue = 0;
        outValueField.clear();
        outIsStacked = false;

        const NativeBuffMaskDefinition* definition = FindNativeBuffMaskDefinition(rawName);
        if (!definition)
            return false;

        outPosition = definition->position;
        outValue = definition->value;
        outValueField = definition->valueField ? definition->valueField : "";
        outIsStacked = definition->stacked;
        return true;
    }

    bool DefinitionUsesNativeDisplay(const SuperSkillDefinition& definition)
    {
        return definition.independentDisplayMode == SuperSkillDefinition::IndependentDisplayMode_Native ||
               definition.independentDisplayMode == SuperSkillDefinition::IndependentDisplayMode_Both;
    }

    bool DefinitionUsesOverlayDisplay(const SuperSkillDefinition& definition)
    {
        return definition.independentDisplayMode == SuperSkillDefinition::IndependentDisplayMode_Overlay ||
               definition.independentDisplayMode == SuperSkillDefinition::IndependentDisplayMode_Both;
    }

    int ResolveLocalPotentialOffsetForBonusKey(const std::string& rawKey)
    {
        const std::string key = NormalizeBuffStatIdentifier(rawKey);
        if (key == "STR")
            return 0x08;
        if (key == "DEX")
            return 0x0C;
        if (key == "INT")
            return 0x10;
        if (key == "LUK")
            return 0x14;
        if (key == "MAXHP")
            return 0x20;
        if (key == "MAXMP")
            return 0x24;
        if (key == "ACC")
            return 0x28;
        if (key == "AVOID")
            return 0x2C;
        if (key == "SPEED")
            return 0x30;
        if (key == "JUMP")
            return 0x34;
        if (key == "WATK")
            return 0x38;
        if (key == "MATK")
            return 0x3C;
        if (key == "WDEF")
            return 0x40;
        if (key == "MDEF")
            return 0x44;
        if (key == "STRPERCENT")
            return 0x48;
        if (key == "DEXPERCENT")
            return 0x4C;
        if (key == "INTPERCENT")
            return 0x50;
        if (key == "LUKPERCENT")
            return 0x54;
        if (key == "MAXHPPERCENT")
            return 0x58;
        if (key == "MAXMPPERCENT")
            return 0x5C;
        if (key == "ACCPERCENT")
            return 0x60;
        if (key == "AVOIDPERCENT")
            return 0x64;
        if (key == "WATKPERCENT")
            return 0x68;
        if (key == "MATKPERCENT")
            return 0x6C;
        if (key == "WDEFPERCENT")
            return 0x70;
        if (key == "MDEFPERCENT")
            return 0x74;
        if (key == "CRITICALRATE")
            return 0x78;
        if (key == "ALLSKILLLEVEL")
            return 0x88;
        if (key == "IGNOREDEFENSEPERCENT")
            return 0xA0;
        if (key == "DAMAGEPERCENT")
            return 0xAC;
        if (key == "BOSSDAMAGEPERCENT")
            return 0xC4;
        if (key == "CRITICALMINDAMAGE")
            return 0xC8;
        if (key == "CRITICALMAXDAMAGE")
            return 0xCC;
        if (key == "TER")
            return 0xD0;
        if (key == "ASR")
            return 0xD4;
        return -1;
    }

    int ResolveLocalPotentialOffsetForOptionId(int optionId)
    {
        switch (optionId)
        {
        case 2: return 0x08;
        case 3: return 0x0C;
        case 4: return 0x10;
        case 5: return 0x14;
        case 8: return 0x20;
        case 9: return 0x24;
        case 10: return 0x28;
        case 11: return 0x2C;
        case 12: return 0x30;
        case 13: return 0x34;
        case 14: return 0x38;
        case 15: return 0x3C;
        case 16: return 0x40;
        case 17: return 0x44;
        case 18: return 0x48;
        case 19: return 0x4C;
        case 20: return 0x50;
        case 21: return 0x54;
        case 22: return 0x58;
        case 23: return 0x5C;
        case 24: return 0x60;
        case 25: return 0x64;
        case 26: return 0x68;
        case 27: return 0x6C;
        case 28: return 0x70;
        case 29: return 0x74;
        case 30: return 0x78;
        case 34: return 0x88;
        case 40: return 0xA0;
        case 43: return 0xAC;
        case 49: return 0xC4;
        case 50: return 0xC8;
        case 51: return 0xCC;
        case 52: return 0xD0;
        case 53: return 0xD4;
        default: return -1;
        }
    }

    const char* LocalPotentialOffsetToString(int offset)
    {
        switch (offset)
        {
        case 0x08: return "STR";
        case 0x0C: return "DEX";
        case 0x10: return "INT";
        case 0x14: return "LUK";
        case 0x20: return "MAXHP";
        case 0x24: return "MAXMP";
        case 0x28: return "ACC";
        case 0x2C: return "AVOID";
        case 0x30: return "SPEED";
        case 0x34: return "JUMP";
        case 0x38: return "WATK";
        case 0x3C: return "MATK";
        case 0x40: return "WDEF";
        case 0x44: return "MDEF";
        case 0x48: return "STRPERCENT";
        case 0x4C: return "DEXPERCENT";
        case 0x50: return "INTPERCENT";
        case 0x54: return "LUKPERCENT";
        case 0x58: return "MAXHPPERCENT";
        case 0x5C: return "MAXMPPERCENT";
        case 0x60: return "ACCPERCENT";
        case 0x64: return "AVOIDPERCENT";
        case 0x68: return "WATKPERCENT";
        case 0x6C: return "MATKPERCENT";
        case 0x70: return "WDEFPERCENT";
        case 0x74: return "MDEFPERCENT";
        case 0x78: return "CRITICALRATE";
        case 0x88: return "ALLSKILLLEVEL";
        case 0xA0: return "IGNOREDEFENSEPERCENT";
        case 0xAC: return "DAMAGEPERCENT";
        case 0xC4: return "BOSSDAMAGEPERCENT";
        case 0xC8: return "CRITICALMINDAMAGE";
        case 0xCC: return "CRITICALMAXDAMAGE";
        case 0xD0: return "TER";
        case 0xD4: return "ASR";
        default: return "UNKNOWN";
        }
    }

    bool ParseJsonString(const std::string& json, const char* key, std::string& outVal)
    {
        outVal.clear();
        std::string token = std::string("\"") + key + "\"";
        size_t pos = json.find(token);
        if (pos == std::string::npos) return false;
        pos = json.find(':', pos + token.size());
        if (pos == std::string::npos) return false;
        ++pos;
        while (pos < json.size() && (json[pos] == ' ' || json[pos] == '\t' || json[pos] == '\r' || json[pos] == '\n')) ++pos;
        if (pos >= json.size() || json[pos] != '"') return false;
        ++pos;
        while (pos < json.size())
        {
            char ch = json[pos++];
            if (ch == '\\')
            {
                if (pos >= json.size()) break;
                char esc = json[pos++];
                switch (esc)
                {
                case '\\': outVal.push_back('\\'); break;
                case '"':  outVal.push_back('"'); break;
                case 'n':  outVal.push_back('\n'); break;
                case 'r':  outVal.push_back('\r'); break;
                case 't':  outVal.push_back('\t'); break;
                default:   outVal.push_back(esc); break;
                }
                continue;
            }
            if (ch == '"')
                return true;
            outVal.push_back(ch);
        }
        outVal.clear();
        return false;
    }

    bool ParseJsonBool(const std::string& json, const char* key, bool& outVal)
    {
        std::string token = std::string("\"") + key + "\"";
        size_t pos = json.find(token);
        if (pos == std::string::npos) return false;
        pos = json.find(':', pos + token.size());
        if (pos == std::string::npos) return false;
        ++pos;
        while (pos < json.size() && (json[pos] == ' ' || json[pos] == '\t' || json[pos] == '\r' || json[pos] == '\n')) ++pos;
        if (pos >= json.size()) return false;

        if (json.compare(pos, 4, "true") == 0) {
            outVal = true;
            return true;
        }
        if (json.compare(pos, 5, "false") == 0) {
            outVal = false;
            return true;
        }
        return false;
    }

    bool ParseJsonDouble(const std::string& json, const char* key, double& outVal)
    {
        std::string token = std::string("\"") + key + "\"";
        size_t pos = json.find(token);
        if (pos == std::string::npos) return false;
        pos = json.find(':', pos + token.size());
        if (pos == std::string::npos) return false;
        ++pos;
        while (pos < json.size() && (json[pos] == ' ' || json[pos] == '\t' || json[pos] == '\r' || json[pos] == '\n')) ++pos;
        if (pos >= json.size()) return false;

        const char* start = json.c_str() + pos;
        char* endPtr = nullptr;
        const double parsedValue = strtod(start, &endPtr);
        if (endPtr == start)
            return false;

        outVal = parsedValue;
        return true;
    }

    bool ParseJsonIntArray(const std::string& json, const char* key, std::vector<int>& outValues)
    {
        outValues.clear();

        std::string token = std::string("\"") + key + "\"";
        size_t pos = json.find(token);
        if (pos == std::string::npos)
            return false;

        pos = json.find(':', pos + token.size());
        if (pos == std::string::npos)
            return false;

        pos = json.find('[', pos);
        if (pos == std::string::npos)
            return false;

        size_t end = pos + 1;
        int depth = 1;
        bool inString = false;
        while (end < json.size())
        {
            const char ch = json[end];
            if (inString)
            {
                if (ch == '\\')
                {
                    end += 2;
                    continue;
                }
                if (ch == '"')
                    inString = false;
            }
            else
            {
                if (ch == '"')
                    inString = true;
                else if (ch == '[')
                    ++depth;
                else if (ch == ']')
                {
                    --depth;
                    if (depth == 0)
                        break;
                }
            }
            ++end;
        }

        if (end >= json.size() || depth != 0)
            return false;

        size_t cursor = pos + 1;
        while (cursor < end)
        {
            while (cursor < end &&
                   (json[cursor] == ' ' || json[cursor] == '\t' || json[cursor] == '\r' ||
                    json[cursor] == '\n' || json[cursor] == ','))
            {
                ++cursor;
            }

            bool negative = false;
            if (cursor < end && json[cursor] == '-')
            {
                negative = true;
                ++cursor;
            }

            if (cursor >= end || json[cursor] < '0' || json[cursor] > '9')
            {
                ++cursor;
                continue;
            }

            int value = 0;
            while (cursor < end && json[cursor] >= '0' && json[cursor] <= '9')
            {
                value = value * 10 + (json[cursor] - '0');
                ++cursor;
            }

            outValues.push_back(negative ? -value : value);
        }

        return !outValues.empty();
    }

    bool TryParseIntegerText(const std::string& text, int& outValue)
    {
        outValue = 0;
        if (text.empty())
            return false;

        char* endPtr = nullptr;
        const long parsed = strtol(text.c_str(), &endPtr, 10);
        if (!endPtr || endPtr == text.c_str() || *endPtr != '\0')
            return false;

        outValue = static_cast<int>(parsed);
        return true;
    }

    bool ExtractJsonObjectText(const std::string& json, const char* key, std::string& outObjectJson)
    {
        outObjectJson.clear();
        if (!key || !*key)
            return false;

        const std::string token = std::string("\"") + key + "\"";
        size_t pos = json.find(token);
        if (pos == std::string::npos)
            return false;

        pos = json.find(':', pos + token.size());
        if (pos == std::string::npos)
            return false;
        ++pos;
        while (pos < json.size() && (json[pos] == ' ' || json[pos] == '\t' || json[pos] == '\r' || json[pos] == '\n'))
            ++pos;
        if (pos >= json.size() || json[pos] != '{')
            return false;

        const size_t begin = pos;
        int depth = 0;
        bool inString = false;
        bool escape = false;
        for (; pos < json.size(); ++pos)
        {
            const char ch = json[pos];
            if (inString)
            {
                if (escape)
                {
                    escape = false;
                }
                else if (ch == '\\')
                {
                    escape = true;
                }
                else if (ch == '"')
                {
                    inString = false;
                }
                continue;
            }

            if (ch == '"')
            {
                inString = true;
                continue;
            }
            if (ch == '{')
            {
                ++depth;
                continue;
            }
            if (ch == '}')
            {
                --depth;
                if (depth == 0)
                {
                    outObjectJson.assign(json.begin() + begin, json.begin() + pos + 1);
                    return true;
                }
            }
        }

        return false;
    }

    void LoadVisibleJobRulesFromJson(const std::string& json, SuperSkillDefinition& definition)
    {
        definition.visibleJobIds.clear();
        definition.visibleJobLabel.clear();

        ParseJsonIntArray(json, "visibleJobIds", definition.visibleJobIds);
        if (definition.visibleJobIds.empty())
        {
            int singleJobId = 0;
            if (ParseJsonInt(json, "visibleJobId", singleJobId) &&
                singleJobId >= 0)
            {
                definition.visibleJobIds.push_back(singleJobId);
            }
        }

        ParseJsonString(json, "visibleJobLabel", definition.visibleJobLabel);
        NormalizeVisibleJobIds(definition.visibleJobIds);
    }

    bool ParsePassiveValueSpec(const std::string& json, const char* key, PassiveValueSpec& outSpec)
    {
        outSpec = PassiveValueSpec{};

        int intValue = 0;
        if (ParseJsonInt(json, key, intValue))
        {
            outSpec.type = PassiveValueSpecType_Fixed;
            outSpec.fixedValue = intValue;
            return true;
        }

        std::string stringValue;
        if (!ParseJsonString(json, key, stringValue) || stringValue.empty())
            return false;

        if (TryParseIntegerText(stringValue, intValue))
        {
            outSpec.type = PassiveValueSpecType_Fixed;
            outSpec.fixedValue = intValue;
            return true;
        }

        outSpec.type = PassiveValueSpecType_SkillField;
        outSpec.skillFieldName = stringValue;
        return true;
    }

    bool TryReadPassiveValueSpec(
        const std::string& json,
        const char* primaryKey,
        const char* legacyKey,
        PassiveValueSpec& outSpec)
    {
        if (primaryKey && ParsePassiveValueSpec(json, primaryKey, outSpec))
            return true;
        if (legacyKey && ParsePassiveValueSpec(json, legacyKey, outSpec))
            return true;
        outSpec = PassiveValueSpec{};
        return false;
    }

    bool TryReadPassiveValueSpecAny(
        const std::string& json,
        const char* const* keys,
        size_t keyCount,
        PassiveValueSpec& outSpec)
    {
        for (size_t i = 0; i < keyCount; ++i)
        {
            if (keys[i] && ParsePassiveValueSpec(json, keys[i], outSpec))
                return true;
        }
        outSpec = PassiveValueSpec{};
        return false;
    }

    bool ParsePassiveBonusDefinition(
        const std::string& json,
        int defaultSourceSkillId,
        PassiveBonusDefinition& outBonus)
    {
        outBonus = PassiveBonusDefinition{};
        outBonus.sourceSkillId = defaultSourceSkillId;

        ParseJsonInt(json, "sourceSkillId", outBonus.sourceSkillId);

        ParseJsonIntArray(json, "psdSkillIds", outBonus.targetSkillIds);
        if (outBonus.targetSkillIds.empty())
            ParseJsonIntArray(json, "targetSkillIds", outBonus.targetSkillIds);
        if (outBonus.targetSkillIds.empty())
            ParseJsonIntArray(json, "skillIds", outBonus.targetSkillIds);
        if (outBonus.targetSkillIds.empty())
        {
            int targetSkillId = 0;
            if (ParseJsonInt(json, "psdSkillId", targetSkillId) && targetSkillId > 0)
                outBonus.targetSkillIds.push_back(targetSkillId);
            else if (ParseJsonInt(json, "targetSkillId", targetSkillId) && targetSkillId > 0)
                outBonus.targetSkillIds.push_back(targetSkillId);
        }

        const char* const damageKeys[] =
        {
            "damagePercent", "damage", "damageIncrease", "damR", "damRate", "dam"
        };
        const char* const ignoreKeys[] =
        {
            "ignoreMobpdpR", "ignoreMob", "ignoreDefensePercent",
            "ignoreDefenseIncrease", "ignoreTargetDEF", "ignoreDefense", "pdr"
        };
        const char* const attackCountKeys[] =
        {
            "attackCount", "hitCount"
        };
        const char* const mobCountKeys[] =
        {
            "mobCount", "targetCount"
        };

        TryReadPassiveValueSpecAny(json, damageKeys, ARRAYSIZE(damageKeys), outBonus.damagePercent);
        TryReadPassiveValueSpecAny(json, ignoreKeys, ARRAYSIZE(ignoreKeys), outBonus.ignoreDefensePercent);
        TryReadPassiveValueSpecAny(json, attackCountKeys, ARRAYSIZE(attackCountKeys), outBonus.attackCount);
        TryReadPassiveValueSpecAny(json, mobCountKeys, ARRAYSIZE(mobCountKeys), outBonus.mobCount);

        if (outBonus.sourceSkillId <= 0 || outBonus.targetSkillIds.empty())
            return false;

        return
            outBonus.damagePercent.type != PassiveValueSpecType_None ||
            outBonus.ignoreDefensePercent.type != PassiveValueSpecType_None ||
            outBonus.attackCount.type != PassiveValueSpecType_None ||
            outBonus.mobCount.type != PassiveValueSpecType_None;
    }

    void LoadClientIndependentBonusSpecsFromJson(
        const std::string& skillJson,
        std::map<std::string, PassiveValueSpec>& outSpecs)
    {
        outSpecs.clear();

        std::string objectJson;
        if (!ExtractJsonObjectText(skillJson, "independentStatBonuses", objectJson) &&
            !ExtractJsonObjectText(skillJson, "clientStatBonuses", objectJson))
        {
            return;
        }

        for (size_t i = 0; i < ARRAYSIZE(kIndependentClientBonusKeys); ++i)
        {
            PassiveValueSpec spec;
            if (ParsePassiveValueSpec(objectJson, kIndependentClientBonusKeys[i], spec))
                outSpecs[kIndependentClientBonusKeys[i]] = spec;
        }
    }

    void LoadPassiveBonusesFromJson(
        const std::string& skillJson,
        int defaultSourceSkillId,
        SuperSkillDefinition& definition)
    {
        definition.passiveBonuses.clear();

        bool loadedArray = false;
        for (int index = 0;; ++index)
        {
            size_t begin = 0;
            size_t end = 0;
            if (!FindArrayElement(skillJson, "passiveBonuses", index, begin, end))
                break;

            loadedArray = true;
            const std::string bonusJson = skillJson.substr(begin, end - begin);
            PassiveBonusDefinition bonus;
            if (ParsePassiveBonusDefinition(bonusJson, defaultSourceSkillId, bonus))
                definition.passiveBonuses.push_back(bonus);
        }

        if (!loadedArray)
        {
            PassiveBonusDefinition legacyBonus;
            if (ParsePassiveBonusDefinition(skillJson, defaultSourceSkillId, legacyBonus))
                definition.passiveBonuses.push_back(legacyBonus);
        }
    }

    int ParseSuperSkillTabName(const std::string& value)
    {
        std::string text = value;
        std::transform(text.begin(), text.end(), text.begin(), [](unsigned char ch) { return (char)tolower(ch); });
        if (text == "passive" || text == "passive_tab" || text == "tab0")
            return 0;
        if (text == "active" || text == "active_tab" || text == "tab1" || text == "buff")
            return 1;
        return -1;
    }

    void ClearSuperSkillRegistry()
    {
        g_superSkillsBySkillId.clear();
        g_hiddenSkillsBySkillId.clear();
        g_superSkillIdsByTab[0].clear();
        g_superSkillIdsByTab[1].clear();
        g_overlayLearnedVisibilityBySkillId.clear();
        g_activeIndependentBuffRewriteStates.clear();
        g_activeLocalIndependentPotentialBySkillId.clear();
        g_localIndependentPotentialDeltaBuffer.fill(0);
        g_localIndependentPotentialMergedBuffer.fill(0);
        g_independentBuffOverlayStates.clear();
        g_recentIndependentBuffClientCancelTickBySkillId.clear();
        g_recentIndependentBuffClientUseTickBySkillId.clear();
        g_recentIndependentBuffActivationTickBySkillId.clear();
        g_independentBuffOverlayActivationCounter = 0;
        g_independentBuffOwnerUserLocal = 0;
        g_independentBuffOwnerNetClient = 0;
        g_independentBuffSceneDetachSinceTick = 0;
        g_independentBuffOwnerMissingSinceTick = 0;
        g_defaultSuperSpCarrierSkillId = 0;
        g_lastOverlayConfiguredJobId = -1;
    }

    bool FindSuperSkillDefinition(int skillId, SuperSkillDefinition& outDefinition)
    {
        std::map<int, SuperSkillDefinition>::const_iterator it = g_superSkillsBySkillId.find(skillId);
        if (it == g_superSkillsBySkillId.end())
            return false;
        outDefinition = it->second;
        return true;
    }

    bool FindHiddenSkillDefinition(int skillId, HiddenSkillDefinition& outDefinition)
    {
        std::map<int, HiddenSkillDefinition>::const_iterator it = g_hiddenSkillsBySkillId.find(skillId);
        if (it == g_hiddenSkillsBySkillId.end())
            return false;
        outDefinition = it->second;
        return true;
    }

    void ClearNativeSkillInjectionRegistry()
    {
        g_nativeSkillInjectionsBySkillId.clear();
        g_nativeInjectedEntriesPtr = 0;
        g_nativeOriginalEntriesPtr = 0;
        g_nativeInjectedSkillWnd = 0;
        g_nativeInjectedEntriesBlock.clear();
        g_nativeInjectedRowBlocks.clear();
        g_nativeInjectedNames.clear();
    }

    bool FindNativeSkillInjectionDefinition(int skillId, NativeSkillInjectionDefinition& outDefinition)
    {
        std::map<int, NativeSkillInjectionDefinition>::const_iterator it = g_nativeSkillInjectionsBySkillId.find(skillId);
        if (it == g_nativeSkillInjectionsBySkillId.end())
            return false;
        outDefinition = it->second;
        return true;
    }

    void LoadSuperSkillRegistry()
    {
        EnsureSkillConfigPathsInitialized();
        ClearSuperSkillRegistry();
        SkillLocalDataInitialize();

        std::string json;
        if (!ReadTextFile(kSuperSkillConfigPath, json))
        {
            if (!g_loggedMissingSuperSkillConfig)
            {
                g_loggedMissingSuperSkillConfig = true;
                WriteLogFmt("[SuperSkill] WARN: missing super skill config: %s", kSuperSkillConfigPath);
            }
            return;
        }
        g_loggedMissingSuperSkillConfig = false;

        ParseJsonInt(json, "defaultSuperSpCarrierSkillId", g_defaultSuperSpCarrierSkillId);

        int hiddenLoadedCount = 0;
        for (int index = 0;; ++index)
        {
            size_t begin = 0;
            size_t end = 0;
            if (!FindArrayElement(json, "hiddenSkills", index, begin, end))
                break;

            const std::string hiddenJson = json.substr(begin, end - begin);
            HiddenSkillDefinition hiddenDefinition = {};
            hiddenDefinition.hideFromNativeSkillWnd = true;
            hiddenDefinition.hideFromSuperSkillWnd = true;

            if (!ParseJsonInt(hiddenJson, "skillId", hiddenDefinition.skillId) || hiddenDefinition.skillId <= 0)
                continue;

            ParseJsonBool(hiddenJson, "hideFromNativeSkillWnd", hiddenDefinition.hideFromNativeSkillWnd);
            ParseJsonBool(hiddenJson, "hideFromSuperSkillWnd", hiddenDefinition.hideFromSuperSkillWnd);

            if (!hiddenDefinition.hideFromNativeSkillWnd && !hiddenDefinition.hideFromSuperSkillWnd)
                continue;

            g_hiddenSkillsBySkillId[hiddenDefinition.skillId] = hiddenDefinition;
            ++hiddenLoadedCount;
        }

        int loadedCount = 0;
        for (int index = 0;; ++index)
        {
            size_t begin = 0;
            size_t end = 0;
            if (!FindArrayElement(json, "skills", index, begin, end))
                break;

            const std::string skillJson = json.substr(begin, end - begin);
            SuperSkillDefinition definition = {};
            definition.superSpCost = 1;
            definition.hideFromNativeSkillWnd = true;
            definition.allowNativeUpgradeFallback = true;

            if (!ParseJsonInt(skillJson, "skillId", definition.skillId) || definition.skillId <= 0)
                continue;

            std::string tabName;
            const bool hasExplicitTab = ParseJsonString(skillJson, "tab", tabName);
            if (hasExplicitTab)
            {
                const int parsedTab = ParseSuperSkillTabName(tabName);
                if (parsedTab >= 0)
                    definition.tabIndex = parsedTab;
            }

            const bool hasExplicitPassive = ParseJsonBool(skillJson, "passive", definition.passive);
            ParseJsonInt(skillJson, "superSpCost", definition.superSpCost);
            ParseJsonBool(skillJson, "hideFromNativeSkillWnd", definition.hideFromNativeSkillWnd);
            ParseJsonBool(skillJson, "showInNativeWhenLearned", definition.showInNativeWhenLearned);
            ParseJsonBool(skillJson, "showInSuperWhenLearned", definition.showInSuperWhenLearned);
            ParseJsonInt(skillJson, "superSpCarrierSkillId", definition.superSpCarrierSkillId);
            ParseJsonBool(skillJson, "allowNativeUpgradeFallback", definition.allowNativeUpgradeFallback);
            ParseJsonInt(skillJson, "behaviorSkillId", definition.behaviorSkillId);
            ParseJsonInt(skillJson, "mountItemId", definition.mountItemId);
            ParseJsonInt(skillJson, "mountTamingMobId", definition.mountTamingMobId);
            ParseJsonBool(skillJson, "useNativeMountMovement", definition.useNativeMountMovement);
            if (ParseJsonInt(skillJson, "mountSpeed", definition.mountSpeedOverride))
                definition.hasMountSpeedOverride = true;
            if (ParseJsonInt(skillJson, "mountJump", definition.mountJumpOverride))
                definition.hasMountJumpOverride = true;
            if (ParseJsonDouble(skillJson, "mountFs", definition.mountFsOverride))
                definition.hasMountFsOverride = true;
            if (ParseJsonDouble(skillJson, "mountSwim", definition.mountSwimOverride))
                definition.hasMountSwimOverride = true;
            ParseJsonBool(skillJson, "mountedDoubleJumpEnabled", definition.mountedDoubleJumpEnabled);
            ParseJsonInt(skillJson, "mountedDoubleJumpSkillId", definition.mountedDoubleJumpSkillId);
            ParseJsonBool(skillJson, "mountedDemonJumpEnabled", definition.mountedDemonJumpEnabled);
            ParseJsonInt(skillJson, "mountedDemonJumpSkillId", definition.mountedDemonJumpSkillId);
            LoadVisibleJobRulesFromJson(skillJson, definition);
            ParseJsonBool(skillJson, "independentBuffEnabled", definition.independentBuffEnabled);
            ParseJsonInt(skillJson, "independentSourceSkillId", definition.independentSourceSkillId);
            if (definition.independentSourceSkillId <= 0)
                ParseJsonInt(skillJson, "sourceSkillId", definition.independentSourceSkillId);
            ParseJsonInt(skillJson, "independentNativeDisplaySkillId", definition.independentNativeDisplaySkillId);
            if (definition.independentNativeDisplaySkillId <= 0)
                ParseJsonInt(skillJson, "iconSkillId", definition.independentNativeDisplaySkillId);
            ParseJsonInt(skillJson, "independentCarrierMaskPosition", definition.independentCarrierMaskPosition);
            ParseJsonInt(skillJson, "independentNativeMaskPosition", definition.independentNativeMaskPosition);
            {
                int maskValue = 0;
                if (ParseJsonInt(skillJson, "independentCarrierMaskValue", maskValue))
                    definition.independentCarrierMaskValue = static_cast<unsigned int>(maskValue);
                if (ParseJsonInt(skillJson, "independentNativeMaskValue", maskValue))
                    definition.independentNativeMaskValue = static_cast<unsigned int>(maskValue);
            }
            {
                std::string buffStatName;
                bool isStacked = false;
                int position = 0;
                unsigned int value = 0;
                std::string valueField;

                if ((definition.independentCarrierMaskPosition <= 0 || definition.independentCarrierMaskValue == 0) &&
                    (ParseJsonString(skillJson, "independentCarrierBuffStat", buffStatName) ||
                     ParseJsonString(skillJson, "carrierBuffStat", buffStatName)))
                {
                    if (TryResolveNativeBuffMaskDefinition(buffStatName, position, value, valueField, isStacked) && !isStacked)
                    {
                        definition.independentCarrierMaskPosition = position;
                        definition.independentCarrierMaskValue = value;
                    }
                    else if (!buffStatName.empty())
                    {
                        WriteLogFmt("[SuperSkill] independent carrier stat unsupported custom=%d stat=%s",
                            definition.skillId,
                            buffStatName.c_str());
                    }
                }

                if ((definition.independentNativeMaskPosition <= 0 || definition.independentNativeMaskValue == 0) &&
                    (ParseJsonString(skillJson, "clientNativeBuffStat", buffStatName) ||
                     ParseJsonString(skillJson, "nativeBuffStat", buffStatName)))
                {
                    if (TryResolveNativeBuffMaskDefinition(buffStatName, position, value, valueField, isStacked) && !isStacked)
                    {
                        definition.independentNativeMaskPosition = position;
                        definition.independentNativeMaskValue = value;
                        if (definition.independentNativeValueSpec.type == PassiveValueSpecType_None && !valueField.empty())
                        {
                            definition.independentNativeValueSpec.type = PassiveValueSpecType_SkillField;
                            definition.independentNativeValueSpec.skillFieldName = valueField;
                        }
                        definition.independentBuffEnabled = true;
                    }
                    else if (!buffStatName.empty())
                    {
                        WriteLogFmt("[SuperSkill] independent native stat unsupported custom=%d stat=%s",
                            definition.skillId,
                            buffStatName.c_str());
                    }
                }

                TryReadPassiveValueSpec(skillJson, "clientNativeValue", "clientNativeValueField", definition.independentNativeValueSpec);
                if (definition.independentNativeValueSpec.type == PassiveValueSpecType_None)
                    TryReadPassiveValueSpec(skillJson, "independentNativeValue", "independentNativeValueField", definition.independentNativeValueSpec);

                ParseJsonString(skillJson, "clientLocalBonusKey", definition.localBonusKey);
                TryReadPassiveValueSpec(skillJson, "clientLocalValue", "clientLocalValueField", definition.localValueSpec);
                LoadClientIndependentBonusSpecsFromJson(skillJson, definition.clientBonusSpecs);

                std::string displayModeText;
                if (ParseJsonString(skillJson, "clientBuffDisplayMode", displayModeText) ||
                    ParseJsonString(skillJson, "independentDisplayMode", displayModeText))
                {
                    definition.independentDisplayMode = ParseIndependentDisplayMode(displayModeText);
                }
            }
            LoadPassiveBonusesFromJson(skillJson, definition.skillId, definition);

            if (definition.tabIndex != 0 && definition.tabIndex != 1)
                definition.tabIndex = definition.passive ? 0 : 1;
            if (definition.tabIndex == 0)
                definition.passive = true;
            if (definition.superSpCost <= 0)
                definition.superSpCost = 1;
            if (definition.superSpCarrierSkillId <= 0)
                definition.superSpCarrierSkillId = g_defaultSuperSpCarrierSkillId;
            if (definition.mountedDoubleJumpEnabled && definition.mountedDoubleJumpSkillId <= 0)
                definition.mountedDoubleJumpSkillId = 3101003;
            if (definition.mountedDemonJumpEnabled && definition.mountedDemonJumpSkillId <= 0)
                definition.mountedDemonJumpSkillId = 30010110;
            if (definition.independentSourceSkillId <= 0)
                definition.independentSourceSkillId = definition.behaviorSkillId > 0 ? definition.behaviorSkillId : definition.skillId;
            if (definition.independentNativeDisplaySkillId <= 0)
                definition.independentNativeDisplaySkillId = definition.skillId;
            if (definition.independentDisplayMode == SuperSkillDefinition::IndependentDisplayMode_Native &&
                (definition.independentNativeMaskPosition <= 0 || definition.independentNativeMaskValue == 0))
            {
                definition.independentDisplayMode = SuperSkillDefinition::IndependentDisplayMode_Overlay;
            }
            if (definition.localBonusKey.empty())
            {
                if (definition.independentNativeMaskPosition == 3 && definition.independentNativeMaskValue == 0x40000000u)
                    definition.localBonusKey = "wdef";
                else if (definition.independentNativeMaskPosition == 1 && definition.independentNativeMaskValue == 0x00000002u)
                    definition.localBonusKey = "wdef";
                else if (definition.independentNativeMaskPosition == 3 && definition.independentNativeMaskValue == 0x10000000u)
                    definition.localBonusKey = "watk";
                else if (definition.independentNativeMaskPosition == 3 && definition.independentNativeMaskValue == 0x20000000u)
                    definition.localBonusKey = "matk";
                else if (definition.independentNativeMaskPosition == 1 && definition.independentNativeMaskValue == 0x00000010u)
                    definition.localBonusKey = "acc";
                else if (definition.independentNativeMaskPosition == 1 && definition.independentNativeMaskValue == 0x00000020u)
                    definition.localBonusKey = "avoid";
                else if (definition.independentNativeMaskPosition == 1 && definition.independentNativeMaskValue == 0x00000080u)
                    definition.localBonusKey = "speed";
                else if (definition.independentNativeMaskPosition == 1 && definition.independentNativeMaskValue == 0x00000100u)
                    definition.localBonusKey = "jump";
            }
            if (definition.localValueSpec.type == PassiveValueSpecType_None &&
                definition.independentNativeValueSpec.type != PassiveValueSpecType_None)
            {
                definition.localValueSpec = definition.independentNativeValueSpec;
            }
            if (!definition.localBonusKey.empty() &&
                definition.localValueSpec.type != PassiveValueSpecType_None)
            {
                if (definition.clientBonusSpecs.find(definition.localBonusKey) == definition.clientBonusSpecs.end())
                {
                    definition.clientBonusSpecs[definition.localBonusKey] = definition.localValueSpec;
                }
                else
                {
                    WriteLogFmt(
                        "[IndependentBuffConfig] keep explicit bonus skillId=%d key=%s over legacy localValueField",
                        definition.skillId,
                        definition.localBonusKey.c_str());
                }
            }
            if (definition.independentCarrierMaskPosition > 0 &&
                definition.independentCarrierMaskValue != 0 &&
                definition.independentNativeMaskPosition > 0 &&
                definition.independentNativeMaskValue != 0)
            {
                definition.independentBuffEnabled = true;
            }

            SkillLocalBehaviorKind localBehavior = SkillLocalBehavior_Unknown;
            if (!hasExplicitTab &&
                !hasExplicitPassive &&
                SkillLocalDataGetBehaviorKind(definition.skillId, localBehavior) &&
                localBehavior == SkillLocalBehavior_Passive &&
                (!definition.passive || definition.tabIndex != 0))
            {
                definition.passive = true;
                definition.tabIndex = 0;
                WriteLogFmt("[SuperSkill] normalize custom=%d localBehavior=passive -> passive_tab",
                    definition.skillId);
            }

            if (g_superSkillsBySkillId.find(definition.skillId) != g_superSkillsBySkillId.end())
            {
                if (!g_loggedDuplicateSuperSkills)
                {
                    g_loggedDuplicateSuperSkills = true;
                    WriteLogFmt("[SuperSkill] WARN: duplicate skillId=%d in %s", definition.skillId, kSuperSkillConfigPath);
                }
            }

            g_superSkillsBySkillId[definition.skillId] = definition;
            g_superSkillIdsByTab[definition.tabIndex].push_back(definition.skillId);
            ++loadedCount;
        }

        WriteLogFmt(
            "[SuperSkill] loaded=%d passive=%d active=%d hidden=%d carrier=%d path=%s",
            loadedCount,
            (int)g_superSkillIdsByTab[0].size(),
            (int)g_superSkillIdsByTab[1].size(),
            hiddenLoadedCount,
            g_defaultSuperSpCarrierSkillId,
            kSuperSkillConfigPath);

        RebuildOverlayLearnedVisibilitySnapshot();
    }

    void LoadNativeSkillInjectionRegistry()
    {
        EnsureSkillConfigPathsInitialized();
        g_nativeSkillInjectionsBySkillId.clear();

        std::string json;
        if (!ReadTextFile(kNativeSkillInjectPath, json))
        {
            if (!g_loggedMissingNativeInjectionConfig)
            {
                g_loggedMissingNativeInjectionConfig = true;
                WriteLogFmt("[NativeSkill] WARN: missing native injection config: %s", kNativeSkillInjectPath);
            }
            return;
        }
        g_loggedMissingNativeInjectionConfig = false;

        int loadedCount = 0;
        for (int index = 0;; ++index)
        {
            size_t begin = 0;
            size_t end = 0;
            if (!FindArrayElement(json, "skills", index, begin, end))
                break;

            const std::string skillJson = json.substr(begin, end - begin);
            NativeSkillInjectionDefinition definition = {};
            definition.enabled = true;

            if (!ParseJsonInt(skillJson, "skillId", definition.skillId) || definition.skillId <= 0)
                continue;

            ParseJsonInt(skillJson, "donorSkillId", definition.donorSkillId);
            ParseJsonBool(skillJson, "enabled", definition.enabled);
            if (!definition.enabled)
                continue;

            if (g_nativeSkillInjectionsBySkillId.find(definition.skillId) != g_nativeSkillInjectionsBySkillId.end())
            {
                if (!g_loggedDuplicateNativeInjections)
                {
                    g_loggedDuplicateNativeInjections = true;
                    WriteLogFmt("[NativeSkill] WARN: duplicate skillId=%d in %s", definition.skillId, kNativeSkillInjectPath);
                }
            }

            g_nativeSkillInjectionsBySkillId[definition.skillId] = definition;
            ++loadedCount;
        }

        WriteLogFmt("[NativeSkill] loaded injection rules=%d path=%s", loadedCount, kNativeSkillInjectPath);
    }

    bool RouteUsesProxySkill(const CustomSkillUseRoute& route)
    {
        return route.proxySkillId > 0;
    }

    bool RouteUsesLegacyProxyPacketRewrite(const CustomSkillUseRoute& route)
    {
        return route.proxySkillId > 0 &&
               route.releaseClass == CustomSkillReleaseClass_None;
    }

    bool RouteUsesNativeReleaseClass(const CustomSkillUseRoute& route)
    {
        return route.releaseClass != CustomSkillReleaseClass_None;
    }

    bool RouteUsesNativeClassifierProxy(const CustomSkillUseRoute& route)
    {
        return route.releaseClass == CustomSkillReleaseClass_NativeClassifierProxy &&
               route.proxySkillId > 0;
    }

    bool IsNativeFlyingMountSkillGateFamily(int skillId);

    void ClearRecentNativePresentationContext()
    {
        g_recentNativePresentation.customSkillId = 0;
        g_recentNativePresentation.proxySkillId = 0;
        g_recentNativePresentation.visualSkillId = 0;
        g_recentNativePresentation.borrowDonorVisual = false;
        g_recentNativePresentation.armedTick = 0;
    }

    bool IsRecentNativePresentationContextFresh()
    {
        if (g_recentNativePresentation.customSkillId <= 0 ||
            g_recentNativePresentation.proxySkillId <= 0 ||
            g_recentNativePresentation.armedTick == 0)
        {
            return false;
        }

        const DWORD now = GetTickCount();
        if (now - g_recentNativePresentation.armedTick > kNativePresentationContextTimeoutMs)
        {
            ClearRecentNativePresentationContext();
            return false;
        }

        return true;
    }

    void ClearActiveNativeReleaseContext()
    {
        g_activeNativeRelease.customSkillId = 0;
        g_activeNativeRelease.classifierProxySkillId = 0;
        g_activeNativeRelease.packetRoute = CustomSkillPacketRoute_None;
        g_activeNativeRelease.releaseClass = CustomSkillReleaseClass_None;
        g_activeNativeRelease.armedTick = 0;
        g_activeNativeRelease.firstRewriteTick = 0;
        g_activeNativeRelease.remainingRewriteBudget = 0;
    }

    bool IsActiveNativeReleaseContextFresh()
    {
        if (g_activeNativeRelease.customSkillId <= 0 ||
            g_activeNativeRelease.packetRoute == CustomSkillPacketRoute_None ||
            g_activeNativeRelease.releaseClass == CustomSkillReleaseClass_None ||
            g_activeNativeRelease.remainingRewriteBudget <= 0)
        {
            return false;
        }

        const DWORD now = GetTickCount();
        if (now - g_activeNativeRelease.armedTick > kNativeReleaseContextTimeoutMs)
        {
            if (g_activeNativeRelease.firstRewriteTick == 0)
            {
                WriteLogFmt("[SkillPacket] native-route timeout-no-packet custom=%d donor=%d route=%s releaseClass=%s age=%u",
                    g_activeNativeRelease.customSkillId,
                    g_activeNativeRelease.classifierProxySkillId,
                    PacketRouteToString(g_activeNativeRelease.packetRoute),
                    ReleaseClassToString(g_activeNativeRelease.releaseClass),
                    (unsigned int)(now - g_activeNativeRelease.armedTick));
            }
            ClearActiveNativeReleaseContext();
            return false;
        }

        return true;
    }

    bool ShouldKeepContextForImmediateProxyClassifierPass(int skillId)
    {
        if (skillId <= 0)
            return false;

        if (!IsRecentNativePresentationContextFresh() || !IsActiveNativeReleaseContextFresh())
            return false;

        if (g_recentNativePresentation.proxySkillId != skillId ||
            g_activeNativeRelease.classifierProxySkillId != skillId)
        {
            return false;
        }

        if (g_activeNativeRelease.firstRewriteTick != 0)
            return false;

        const DWORD now = GetTickCount();
        return now - g_activeNativeRelease.armedTick <= kNativeClassifierProxyGraceMs;
    }

    bool ShouldPreserveActiveContextForImmediateProxyRoute(int skillId, const CustomSkillUseRoute& route)
    {
        if (skillId <= 0 || route.releaseClass != CustomSkillReleaseClass_NativeClassifierProxy)
            return false;

        if (!ShouldKeepContextForImmediateProxyClassifierPass(skillId) ||
            !IsActiveNativeReleaseContextFresh())
        {
            return false;
        }

        if (g_activeNativeRelease.classifierProxySkillId != skillId ||
            g_activeNativeRelease.customSkillId <= 0 ||
            g_activeNativeRelease.customSkillId == skillId)
        {
            return false;
        }

        return true;
    }

    void ArmActiveNativeReleaseContext(const CustomSkillUseRoute& route)
    {
        g_activeNativeRelease.customSkillId = route.skillId;
        g_activeNativeRelease.classifierProxySkillId = route.proxySkillId;
        g_activeNativeRelease.packetRoute = route.packetRoute;
        g_activeNativeRelease.releaseClass = route.releaseClass;
        g_activeNativeRelease.armedTick = GetTickCount();
        g_activeNativeRelease.firstRewriteTick = 0;
        g_activeNativeRelease.remainingRewriteBudget = kNativeReleaseRewriteBudget;

        if (RouteUsesNativeClassifierProxy(route))
        {
            ++g_presentationGeneration;
            g_recentNativePresentation.customSkillId = route.skillId;
            g_recentNativePresentation.proxySkillId = route.proxySkillId;
            g_recentNativePresentation.visualSkillId = route.visualSkillId;
            g_recentNativePresentation.borrowDonorVisual = route.borrowDonorVisual;
            g_recentNativePresentation.armedTick = g_activeNativeRelease.armedTick;
            g_recentNativePresentation.generation = g_presentationGeneration;
        }
        else
        {
            ClearRecentNativePresentationContext();
        }
    }

    bool TryResolveRecentNativePresentationOverride(int observedSkillId, int& outOverrideSkillId)
    {
        outOverrideSkillId = 0;
        if (observedSkillId <= 0)
            return false;

        if (!IsRecentNativePresentationContextFresh())
            return false;

        if (g_recentNativePresentation.proxySkillId != observedSkillId)
            return false;

        // generation 守卫：每次 Arm 递增 generation，每次 ABAF70 消费后记录已消费的 generation。
        // 如果当前 generation 已经被消费过，说明这次 ABAF70 调用不属于自定义释放链，
        // 而是用户手动释放的原生技能。
        if (g_recentNativePresentation.generation <= g_lastConsumedGeneration)
        {
            ClearRecentNativePresentationContext();
            return false;
        }

        // Keep donor id at presentation stage for donor-exclusive follow-up branches.
        if (g_recentNativePresentation.borrowDonorVisual)
        {
            g_lastConsumedGeneration = g_recentNativePresentation.generation;
            ClearRecentNativePresentationContext();
            return false;
        }

        if (g_recentNativePresentation.visualSkillId > 0 &&
            g_recentNativePresentation.visualSkillId != observedSkillId)
        {
            outOverrideSkillId = g_recentNativePresentation.visualSkillId;
            g_lastConsumedGeneration = g_recentNativePresentation.generation;
            return true;
        }

        if (g_recentNativePresentation.customSkillId > 0 &&
            g_recentNativePresentation.customSkillId != observedSkillId)
        {
            outOverrideSkillId = g_recentNativePresentation.customSkillId;
            g_lastConsumedGeneration = g_recentNativePresentation.generation;
            return true;
        }

        return false;
    }

    DWORD ReleaseClassToJumpAddress(CustomSkillReleaseClass releaseClass)
    {
        switch (releaseClass)
        {
        case CustomSkillReleaseClass_NativeB31722:
            return ADDR_B31722;
        default:
            return 0;
        }
    }

    void ClearCustomSkillRoutes()
    {
        g_customRoutesBySkillId.clear();
        g_customRoutesByProxyAndRoute.clear();
        ClearActiveNativeReleaseContext();
        ClearRecentNativePresentationContext();
        g_loggedMissingRouteConfig = false;
        g_loggedDuplicateRoutes = false;
    }

    void LoadCustomSkillRoutes()
    {
        EnsureSkillConfigPathsInitialized();
        ClearCustomSkillRoutes();

        std::string json;
        if (!ReadTextFile(kCustomSkillRoutePath, json))
        {
            if (!g_loggedMissingRouteConfig)
            {
                WriteLogFmt("[SkillRoute] no config at %s", kCustomSkillRoutePath);
                g_loggedMissingRouteConfig = true;
            }
            return;
        }
        g_loggedMissingRouteConfig = false;

        for (int i = 0;; ++i)
        {
            size_t routeBegin = 0, routeEnd = 0;
            if (!FindArrayElement(json, "routes", i, routeBegin, routeEnd))
                break;

            const std::string routeJson = json.substr(routeBegin, routeEnd - routeBegin);
            CustomSkillUseRoute route = {};
            std::string routeName;
            std::string releaseClassName;
            if (!ParseJsonInt(routeJson, "skillId", route.skillId) ||
                !ParseJsonString(routeJson, "packetRoute", routeName))
            {
                continue;
            }

            ParseJsonInt(routeJson, "proxySkillId", route.proxySkillId);
            ParseJsonString(routeJson, "releaseClass", releaseClassName);
            const bool hasBorrowDonorVisualConfig =
                ParseJsonBool(routeJson, "borrowDonorVisual", route.borrowDonorVisual);
            if (!ParseJsonInt(routeJson, "visualSkillId", route.visualSkillId))
                ParseJsonInt(routeJson, "presentationSkillId", route.visualSkillId);
            route.packetRoute = ParsePacketRouteName(routeName);
            route.releaseClass = ParseReleaseClassName(releaseClassName);

            if (route.skillId <= 0 || route.packetRoute == CustomSkillPacketRoute_None)
                continue;

            if (!RouteUsesProxySkill(route) && !RouteUsesNativeReleaseClass(route))
                continue;

            if (route.releaseClass == CustomSkillReleaseClass_NativeClassifierProxy &&
                route.proxySkillId <= 0)
            {
                WriteLogFmt("[SkillRoute] skip custom=%d releaseClass=%s (missing proxySkillId)",
                    route.skillId, ReleaseClassToString(route.releaseClass));
                continue;
            }

            SkillLocalBehaviorKind localBehavior = SkillLocalBehavior_Unknown;
            const bool hasLocalBehavior = SkillLocalDataGetBehaviorKind(route.skillId, localBehavior);
            SuperSkillDefinition superDefinition = {};
            const bool hasSuperDefinition = FindSuperSkillDefinition(route.skillId, superDefinition);
            const bool allowPassiveMountedDemonJumpRoute =
                ShouldKeepPassiveMountedDemonJumpRoute(route);
            if (hasLocalBehavior)
            {
                if (localBehavior == SkillLocalBehavior_Passive &&
                    hasSuperDefinition &&
                    !superDefinition.passive)
                {
                    WriteLogFmt("[SkillRoute] override custom=%d localBehavior=passive -> active_by_super_config",
                        route.skillId);
                    localBehavior = SkillLocalBehavior_Unknown;
                }

                  const bool allowBehaviorNormalization =
                      (route.releaseClass == CustomSkillReleaseClass_None);
                  if (allowBehaviorNormalization)
                  {
                      const CustomSkillPacketRoute configuredPacketRoute = route.packetRoute;
                      const bool routeWasNormalized = TryNormalizeRouteForBehavior(localBehavior, route.packetRoute);
                      if (routeWasNormalized)
                      {
                          WriteLogFmt("[SkillRoute] normalize custom=%d localBehavior=%s route=%s -> %s",
                              route.skillId,
                              LocalBehaviorToString(localBehavior),
                              PacketRouteToString(configuredPacketRoute),
                              PacketRouteToString(route.packetRoute));
                      }

                      const int configuredProxySkillId = route.proxySkillId;
                      if (TryNormalizeProxyForBehavior(
                              localBehavior,
                              route.releaseClass,
                              routeWasNormalized,
                              route.proxySkillId))
                      {
                          WriteLogFmt("[SkillRoute] normalize custom=%d localBehavior=%s proxy=%d -> %d",
                              route.skillId,
                              LocalBehaviorToString(localBehavior),
                              configuredProxySkillId,
                              route.proxySkillId);
                      }
                  }
                  else if (!IsBehaviorRouteLikelyCompatible(localBehavior, route.packetRoute))
                  {
                      WriteLogFmt("[SkillRoute] keep explicit route custom=%d localBehavior=%s route=%s releaseClass=%s",
                          route.skillId,
                          LocalBehaviorToString(localBehavior),
                          PacketRouteToString(route.packetRoute),
                          ReleaseClassToString(route.releaseClass));
                  }

                const bool usesConfiguredMonsterRidingBehaviorProxy =
                    hasSuperDefinition &&
                    superDefinition.mountItemId > 0 &&
                    route.packetRoute == CustomSkillPacketRoute_SpecialMove &&
                    route.releaseClass == CustomSkillReleaseClass_NativeClassifierProxy &&
                    route.proxySkillId > 0 &&
                    route.proxySkillId == superDefinition.behaviorSkillId &&
                    route.proxySkillId == 80001004;

                const bool shouldRestoreMonsterRidingBehaviorProxy =
                    hasSuperDefinition &&
                    superDefinition.mountItemId > 0 &&
                    superDefinition.behaviorSkillId == 80001004 &&
                    route.packetRoute == CustomSkillPacketRoute_SpecialMove &&
                    route.releaseClass == CustomSkillReleaseClass_NativeClassifierProxy &&
                    route.proxySkillId > 0 &&
                    route.proxySkillId != superDefinition.behaviorSkillId;

                if (usesConfiguredMonsterRidingBehaviorProxy)
                {
                    WriteLogFmt("[SkillRoute] keep configured mount behavior proxy custom=%d donor=%d route=%s",
                        route.skillId,
                        route.proxySkillId,
                        PacketRouteToString(route.packetRoute));
                }
                else if (shouldRestoreMonsterRidingBehaviorProxy)
                {
                    const int configuredProxySkillId = route.proxySkillId;
                    route.borrowDonorVisual = false;
                    route.proxySkillId = superDefinition.behaviorSkillId;
                    WriteLogFmt("[SkillRoute] restore mount behavior proxy custom=%d donor=%d -> %d route=%s",
                        route.skillId,
                        configuredProxySkillId,
                        route.proxySkillId,
                        PacketRouteToString(route.packetRoute));
                }
                else if (ShouldUseStableMountSpecialMoveProxy(route) &&
                         route.proxySkillId != kStableMountSpecialMoveProxySkillId)
                {
                    const int configuredProxySkillId = route.proxySkillId;
                    if (route.visualSkillId <= 0)
                        route.visualSkillId = configuredProxySkillId;
                    route.borrowDonorVisual = false;
                    route.proxySkillId = kStableMountSpecialMoveProxySkillId;
                    WriteLogFmt("[SkillRoute] stabilize mount proxy custom=%d donor=%d -> %d visualSkillId=%d route=%s",
                        route.skillId,
                        configuredProxySkillId,
                        route.proxySkillId,
                        route.visualSkillId,
                        PacketRouteToString(route.packetRoute));
                }

                if (localBehavior == SkillLocalBehavior_Passive)
                {
                    if (!allowPassiveMountedDemonJumpRoute)
                    {
                        WriteLogFmt("[SkillRoute] skip custom=%d localBehavior=passive (passive uses independent path, not active release route)",
                            route.skillId);
                        continue;
                    }

                    WriteLogFmt("[SkillRoute] keep custom=%d localBehavior=passive -> mounted_demon_root_route",
                        route.skillId);
                }

                if (!hasBorrowDonorVisualConfig &&
                    route.releaseClass == CustomSkillReleaseClass_NativeClassifierProxy &&
                    route.proxySkillId > 0 &&
                    route.packetRoute == CustomSkillPacketRoute_SpecialMove &&
                    route.visualSkillId <= 0 &&
                    (localBehavior == SkillLocalBehavior_Buff ||
                     localBehavior == SkillLocalBehavior_MorphLike ||
                     localBehavior == SkillLocalBehavior_MountLike))
                {
                    route.borrowDonorVisual = true;
                    WriteLogFmt("[SkillRoute] auto-borrow donor visual custom=%d localBehavior=%s donor=%d route=%s",
                        route.skillId,
                        LocalBehaviorToString(localBehavior),
                        route.proxySkillId,
                        PacketRouteToString(route.packetRoute));
                }
            }
            else if (!hasBorrowDonorVisualConfig &&
                     route.releaseClass == CustomSkillReleaseClass_NativeClassifierProxy &&
                     route.proxySkillId == 1001003 &&
                     route.packetRoute == CustomSkillPacketRoute_SpecialMove &&
                     route.visualSkillId <= 0)
            {
                // Fallback for missing local behavior metadata: keep donor follow-up
                // chain for 1001003 special-move family.
                route.borrowDonorVisual = true;
                WriteLogFmt("[SkillRoute] auto-borrow donor visual custom=%d fallback donor=1001003 route=%s",
                    route.skillId,
                    PacketRouteToString(route.packetRoute));
            }

            if (RouteUsesLegacyProxyPacketRewrite(route))
            {
                const unsigned long long proxyKey = MakeProxyRouteKey(route.proxySkillId, route.packetRoute);
                if (g_customRoutesByProxyAndRoute.find(proxyKey) != g_customRoutesByProxyAndRoute.end())
                {
                    if (!g_loggedDuplicateRoutes)
                    {
                        g_loggedDuplicateRoutes = true;
                        WriteLogFmt("[SkillRoute] duplicate proxy mapping ignored: proxy=%d route=%s",
                            route.proxySkillId, PacketRouteToString(route.packetRoute));
                    }
                    continue;
                }

                g_customRoutesByProxyAndRoute[proxyKey] = route;
            }

            if (hasLocalBehavior)
            {
                if (!IsBehaviorRouteLikelyCompatible(localBehavior, route.packetRoute))
                {
                    WriteLogFmt("[SkillRoute] WARN: custom=%d localBehavior=%s route=%s may need dedicated native family",
                        route.skillId,
                        LocalBehaviorToString(localBehavior),
                        PacketRouteToString(route.packetRoute));
                }

                if (localBehavior == SkillLocalBehavior_Passive &&
                    !allowPassiveMountedDemonJumpRoute)
                {
                    WriteLogFmt("[SkillRoute] WARN: custom=%d is passive-like; native active release route will not make passive logic truly independent",
                        route.skillId);
                }
            }

            g_customRoutesBySkillId[route.skillId] = route;
            WriteLogFmt("[SkillRoute] loaded custom=%d proxy=%d route=%s releaseClass=%s borrowVisual=%d visualSkillId=%d",
                route.skillId,
                route.proxySkillId,
                PacketRouteToString(route.packetRoute),
                ReleaseClassToString(route.releaseClass),
                route.borrowDonorVisual ? 1 : 0,
                route.visualSkillId);
        }

        EnsureMountedDemonJumpSyntheticRoutes();

        WriteLogFmt("[SkillRoute] ready count=%d path=%s",
            (int)g_customRoutesBySkillId.size(), kCustomSkillRoutePath);
    }

    bool FindRouteByCustomSkillId(int skillId, CustomSkillUseRoute& outRoute)
    {
        std::map<int, CustomSkillUseRoute>::const_iterator it = g_customRoutesBySkillId.find(skillId);
        if (it == g_customRoutesBySkillId.end())
            return false;
        const CustomSkillUseRoute& route = it->second;
        if (route.skillId == route.proxySkillId &&
            route.releaseClass == CustomSkillReleaseClass_NativeClassifierProxy &&
            route.packetRoute == CustomSkillPacketRoute_SpecialMove &&
            IsNativeFlyingMountSkillGateFamily(route.skillId))
        {
            return false;
        }
        outRoute = route;
        return true;
    }

    bool FindRouteByProxySkillId(int proxySkillId, CustomSkillPacketRoute route, CustomSkillUseRoute& outRoute)
    {
        std::map<unsigned long long, CustomSkillUseRoute>::const_iterator it =
            g_customRoutesByProxyAndRoute.find(MakeProxyRouteKey(proxySkillId, route));
        if (it == g_customRoutesByProxyAndRoute.end())
            return false;
        const CustomSkillUseRoute& mappedRoute = it->second;
        if (mappedRoute.skillId == mappedRoute.proxySkillId &&
            mappedRoute.releaseClass == CustomSkillReleaseClass_NativeClassifierProxy &&
            mappedRoute.packetRoute == CustomSkillPacketRoute_SpecialMove &&
            IsNativeFlyingMountSkillGateFamily(mappedRoute.skillId))
        {
            return false;
        }
        outRoute = mappedRoute;
        return true;
    }

    bool IsNativeFlyingMountSkillGateFamily(int skillId)
    {
        return (skillId >= 80001063 && skillId <= 80001089) || skillId == 80001120;
    }

    int GetTrackedSkillLevel(int skillId)
    {
        SkillManager* manager = GetBridgeManager();
        SkillItem* item = FindManagerSkillItem(manager, skillId);
        if (item && item->level > 0)
        {
            RecordPersistentSuperSkillLevel(skillId, item->level, "manager-item");
            return item->level;
        }

        int persistentLevel = 0;
        if (TryGetPersistentSuperSkillLevel(skillId, persistentLevel))
            return persistentLevel;

        const int gameLevel = GameGetSkillLevel(skillId);
        if (gameLevel > 0)
            RecordPersistentSuperSkillLevel(skillId, gameLevel, "tracked-game");
        return gameLevel;
    }

    int GetObservedActualBaseSkillLevel(int skillId)
    {
        std::map<int, int>::const_iterator it = g_observedActualBaseLevelsBySkillId.find(skillId);
        if (it == g_observedActualBaseLevelsBySkillId.end())
            return 0;
        return it->second;
    }

    int GetObservedActualCurrentSkillLevel(int skillId)
    {
        std::map<int, int>::const_iterator it = g_observedActualCurrentLevelsBySkillId.find(skillId);
        if (it == g_observedActualCurrentLevelsBySkillId.end())
            return 0;
        return it->second;
    }

    bool HasObservedActualBaseSkillLevel(int skillId)
    {
        return g_observedActualBaseLevelsBySkillId.find(skillId) != g_observedActualBaseLevelsBySkillId.end();
    }

    bool HasObservedActualCurrentSkillLevel(int skillId)
    {
        return g_observedActualCurrentLevelsBySkillId.find(skillId) != g_observedActualCurrentLevelsBySkillId.end();
    }

    int GetRuntimeAppliedSkillLevel(int skillId)
    {
        if (skillId <= 0)
            return 0;

        const int actualCurrentLevel = GetObservedActualCurrentSkillLevel(skillId);
        const int actualBaseLevel = GetObservedActualBaseSkillLevel(skillId);
        if (actualCurrentLevel > 0 || actualBaseLevel > 0)
            return (actualCurrentLevel > actualBaseLevel) ? actualCurrentLevel : actualBaseLevel;

        SkillManager* manager = GetBridgeManager();
        SkillItem* item = FindManagerSkillItem(manager, skillId);
        if (item && item->level > 0)
            return item->level;

        int persistentLevel = 0;
        if (TryGetPersistentSuperSkillLevel(skillId, persistentLevel))
            return persistentLevel;

        return 0;
    }

    int GetObservedBaseSkillLevel(int skillId)
    {
        std::map<int, int>::const_iterator it = g_observedBaseLevelsBySkillId.find(skillId);
        if (it == g_observedBaseLevelsBySkillId.end())
            return 0;
        return it->second;
    }

    int GetObservedCurrentSkillLevel(int skillId)
    {
        std::map<int, int>::const_iterator it = g_observedCurrentLevelsBySkillId.find(skillId);
        if (it == g_observedCurrentLevelsBySkillId.end())
            return 0;
        return it->second;
    }

    bool HasObservedBaseSkillLevel(int skillId)
    {
        return g_observedBaseLevelsBySkillId.find(skillId) != g_observedBaseLevelsBySkillId.end();
    }

    bool HasObservedCurrentSkillLevel(int skillId)
    {
        return g_observedCurrentLevelsBySkillId.find(skillId) != g_observedCurrentLevelsBySkillId.end();
    }

    bool IsTrackedNonNativeSuperSkill(int skillId, SkillItem** outItem)
    {
        SkillManager* manager = GetBridgeManager();
        SkillItem* item = FindManagerSkillItem(manager, skillId);
        if (outItem)
            *outItem = item;

        SuperSkillDefinition definition = {};
        if (!FindSuperSkillDefinition(skillId, definition))
            return false;

        return !item || !item->hasNativeUpgradeState;
    }

    bool IsSuperSkillResetLevelSyncWindowActive()
    {
        return g_superSkillResetLevelSyncUntilTick != 0 &&
            static_cast<LONG>(g_superSkillResetLevelSyncUntilTick - GetTickCount()) > 0;
    }

    bool ShouldSuppressNonNativeSuperSkillLevelFallback(int skillId)
    {
        if (!IsSuperSkillResetLevelSyncWindowActive())
            return false;

        if (!IsTrackedNonNativeSuperSkill(skillId))
            return false;

        PendingOptimisticSuperSkillLevelHold optimisticHold;
        return !TryGetFreshPendingOptimisticSuperSkillLevelHold(skillId, optimisticHold);
    }

    void ClearTrackedNonNativeSuperSkillLevel(int skillId, const char* reason)
    {
        if (g_superSkillsBySkillId.empty())
            LoadSuperSkillRegistry();

        SuperSkillDefinition definition = {};
        const bool clearedIndependentRuntimeState =
            FindSuperSkillDefinition(skillId, definition) &&
            definition.independentBuffEnabled &&
            ClearIndependentBuffRuntimeStateForDefinition(
                definition,
                reason ? reason : "level-clear");

        SkillItem* item = nullptr;
        if (!IsTrackedNonNativeSuperSkill(skillId, &item))
        {
            if (clearedIndependentRuntimeState)
                ForceRefreshIndependentBuffUi(reason ? reason : "level-clear");
            return;
        }

        const int previousItemLevel = item ? item->level : 0;
        const int previousObservedBase = GetObservedBaseSkillLevel(skillId);
        const int previousObservedCurrent = GetObservedCurrentSkillLevel(skillId);
        int previousPersistentLevel = 0;
        std::map<int, int>::iterator persistentIt = g_persistentNonNativeSuperSkillLevelsBySkillId.find(skillId);
        if (persistentIt != g_persistentNonNativeSuperSkillLevelsBySkillId.end())
        {
            previousPersistentLevel = persistentIt->second;
            g_persistentNonNativeSuperSkillLevelsBySkillId.erase(persistentIt);
        }

        g_observedBaseLevelsBySkillId.erase(skillId);
        g_observedCurrentLevelsBySkillId.erase(skillId);
        g_observedActualBaseLevelsBySkillId.erase(skillId);
        g_observedActualCurrentLevelsBySkillId.erase(skillId);
        g_pendingOptimisticSuperSkillLevelHoldBySkillId.erase(skillId);

        if (item)
            item->level = 0;

        const bool hadTrackedLevel =
            previousItemLevel > 0 ||
            previousObservedBase > 0 ||
            previousObservedCurrent > 0 ||
            previousPersistentLevel > 0;
        if (hadTrackedLevel)
        {
            static LONG s_resetZeroSyncClearLogBudget = 32;
            const LONG budgetAfterDecrement = InterlockedDecrement(&s_resetZeroSyncClearLogBudget);
            if (budgetAfterDecrement >= 0)
            {
                WriteLogFmt("[SkillLevelBridge] reset-zero sync clear skillId=%d reason=%s item=%d persistent=%d base=%d current=%d",
                    skillId,
                    reason ? reason : "unknown",
                    previousItemLevel,
                    previousPersistentLevel,
                    previousObservedBase,
                    previousObservedCurrent);
            }
        }

        if (clearedIndependentRuntimeState)
            ForceRefreshIndependentBuffUi(reason ? reason : "level-clear");
    }

    void RecordPersistentSuperSkillLevel(int skillId, int level, const char* reason)
    {
        if (skillId <= 0 || level <= 0)
            return;

        SuperSkillDefinition definition = {};
        if (!FindSuperSkillDefinition(skillId, definition))
            return;

        std::map<int, int>::iterator it = g_persistentNonNativeSuperSkillLevelsBySkillId.find(skillId);
        const int previousLevel = (it != g_persistentNonNativeSuperSkillLevelsBySkillId.end()) ? it->second : 0;
        if (previousLevel >= level)
            return;

        g_persistentNonNativeSuperSkillLevelsBySkillId[skillId] = level;
        WriteLogFmt("[SkillLevelBridge] persistent hold skillId=%d level=%d reason=%s prev=%d",
            skillId,
            level,
            reason ? reason : "unknown",
            previousLevel);
    }

    bool TryGetPersistentSuperSkillLevel(int skillId, int& outLevel)
    {
        outLevel = 0;
        if (skillId <= 0)
            return false;

        std::map<int, int>::const_iterator it = g_persistentNonNativeSuperSkillLevelsBySkillId.find(skillId);
        if (it == g_persistentNonNativeSuperSkillLevelsBySkillId.end() || it->second <= 0)
            return false;

        outLevel = it->second;
        return true;
    }

    void ClearPersistentSuperSkillLevels(const char* reason)
    {
        if (g_superSkillsBySkillId.empty())
            LoadSuperSkillRegistry();

        if (!g_persistentNonNativeSuperSkillLevelsBySkillId.empty())
        {
            WriteLogFmt("[SkillLevelBridge] clear persistent levels count=%d reason=%s",
                (int)g_persistentNonNativeSuperSkillLevelsBySkillId.size(),
                reason ? reason : "unknown");
        }
        ClearAllPendingOptimisticSuperSkillLevelHolds(reason);
        g_persistentNonNativeSuperSkillLevelsBySkillId.clear();

        SkillManager* manager = GetBridgeManager();
        bool clearedAnyIndependentRuntimeState = false;
        for (std::map<int, SuperSkillDefinition>::const_iterator it = g_superSkillsBySkillId.begin();
             it != g_superSkillsBySkillId.end();
             ++it)
        {
            const SuperSkillDefinition& definition = it->second;
            const int skillId = it->first;
            if (definition.independentBuffEnabled &&
                ClearIndependentBuffRuntimeStateForDefinition(
                    definition,
                    reason ? reason : "clear-persistent"))
            {
                clearedAnyIndependentRuntimeState = true;
            }

            g_observedBaseLevelsBySkillId.erase(skillId);
            g_observedCurrentLevelsBySkillId.erase(skillId);
            g_observedActualBaseLevelsBySkillId.erase(skillId);
            g_observedActualCurrentLevelsBySkillId.erase(skillId);

            SkillItem* item = FindManagerSkillItem(manager, skillId);
            if (item)
                item->level = 0;
        }

        if (clearedAnyIndependentRuntimeState)
            ForceRefreshIndependentBuffUi(reason ? reason : "clear-persistent");
    }

    void ApplyAuthoritativeSuperSkillLevelSync(int skillId, int level, const char* reason)
    {
        if (skillId <= 0)
            return;

        if (g_superSkillsBySkillId.empty())
            LoadSuperSkillRegistry();

        SuperSkillDefinition definition = {};
        if (!FindSuperSkillDefinition(skillId, definition))
            return;

        if (level <= 0)
        {
            ClearTrackedNonNativeSuperSkillLevel(skillId, reason ? reason : "sync-zero");
            return;
        }

        g_observedActualBaseLevelsBySkillId[skillId] = level;
        g_observedActualCurrentLevelsBySkillId[skillId] = level;
        g_observedBaseLevelsBySkillId[skillId] = level;
        g_observedCurrentLevelsBySkillId[skillId] = level;
        RecordPersistentSuperSkillLevel(skillId, level, reason ? reason : "sync");
        ClearPendingOptimisticSuperSkillLevelHold(skillId);

        SkillItem* item = FindManagerSkillItem(GetBridgeManager(), skillId);
        if (item)
        {
            item->level = level;
            if (item->level < 0)
                item->level = 0;
            if (item->maxLevel > 0 && item->level > item->maxLevel)
                item->level = item->maxLevel;
            RefreshSkillNativeState(*item);
        }

        WriteLogFmt("[SuperSkill] level sync apply skillId=%d level=%d item=%d reason=%s",
            skillId,
            level,
            item ? item->level : -1,
            reason ? reason : "unknown");
    }

    bool TryResolvePersistentNonNativeSuperSkillLevel(int skillId, int observedLevel, int currentItemLevel, int& outLevel)
    {
        outLevel = 0;
        if (skillId <= 0 || observedLevel > 0)
            return false;
        if (ShouldSuppressNonNativeSuperSkillLevelFallback(skillId))
            return false;

        SuperSkillDefinition definition = {};
        if (!FindSuperSkillDefinition(skillId, definition))
            return false;

        int keepLevel = currentItemLevel;
        const int observedBaseLevel = GetObservedBaseSkillLevel(skillId);
        const int observedCurrentLevel = GetObservedCurrentSkillLevel(skillId);
        int persistentLevel = 0;
        if (TryGetPersistentSuperSkillLevel(skillId, persistentLevel) && persistentLevel > keepLevel)
            keepLevel = persistentLevel;
        if (observedBaseLevel > keepLevel)
            keepLevel = observedBaseLevel;
        if (observedCurrentLevel > keepLevel)
            keepLevel = observedCurrentLevel;
        if (keepLevel <= 0)
            return false;

        outLevel = keepLevel;
        return true;
    }

    bool ShouldLogObservedSkillPacket(int skillId)
    {
        if (skillId <= 0)
            return false;

        if (FindManagerSkillItem(GetBridgeManager(), skillId))
            return true;

        CustomSkillUseRoute route = {};
        if (FindRouteByCustomSkillId(skillId, route))
            return true;

        for (std::map<int, CustomSkillUseRoute>::const_iterator it = g_customRoutesBySkillId.begin();
             it != g_customRoutesBySkillId.end();
             ++it)
        {
            if (it->second.proxySkillId == skillId)
                return true;
        }

        return false;
    }

    unsigned short ReadPacketWord(const BYTE* packet)
    {
        return (unsigned short)(packet[0] | (packet[1] << 8));
    }

    int ReadPacketInt(const BYTE* packet, int offset)
    {
        return (int)(
            ((unsigned int)packet[offset]) |
            ((unsigned int)packet[offset + 1] << 8) |
            ((unsigned int)packet[offset + 2] << 16) |
            ((unsigned int)packet[offset + 3] << 24));
    }

    unsigned short ReadPacketShort(const BYTE* packet, int offset)
    {
        return static_cast<unsigned short>(
            ((unsigned int)packet[offset]) |
            ((unsigned int)packet[offset + 1] << 8));
    }

    void WritePacketShort(BYTE* packet, int offset, unsigned short value)
    {
        packet[offset + 0] = static_cast<BYTE>(value & 0xFF);
        packet[offset + 1] = static_cast<BYTE>((value >> 8) & 0xFF);
    }

    void WritePacketInt(BYTE* packet, int offset, int value)
    {
        packet[offset + 0] = (BYTE)(value & 0xFF);
        packet[offset + 1] = (BYTE)((value >> 8) & 0xFF);
        packet[offset + 2] = (BYTE)((value >> 16) & 0xFF);
        packet[offset + 3] = (BYTE)((value >> 24) & 0xFF);
    }

    int GetBuffMaskPacketOffsetForPosition(int position)
    {
        if (position < 1 || position > kBuffMaskIntCount)
            return -1;
        return (kBuffMaskIntCount - position) * static_cast<int>(sizeof(int));
    }

    bool TryReadIncomingPacketPayload(void* inPacket, BYTE*& outPayload, int& outPayloadLen)
    {
        outPayload = nullptr;
        outPayloadLen = 0;

        uintptr_t base = 0;
        WORD length = 0;
        DWORD cursor = 0;
        const uintptr_t packetPtr = reinterpret_cast<uintptr_t>(inPacket);
        if (!SafeReadValue(packetPtr + 0x8, base) ||
            !SafeReadValue(packetPtr + 0xC, length) ||
            !SafeReadValue(packetPtr + 0x14, cursor) ||
            !base)
        {
            return false;
        }

        if (cursor > static_cast<DWORD>(length))
            return false;
        if (length == 0)
            return false;
        if (SafeIsBadReadPtr(reinterpret_cast<void*>(base + cursor), length - cursor))
            return false;

        outPayload = reinterpret_cast<BYTE*>(base + cursor);
        outPayloadLen = static_cast<int>(length - cursor);
        return outPayloadLen > 0;
    }

    bool PacketMaskHasValue(const BYTE* payload, int payloadLen, int position, unsigned int value)
    {
        if (!payload || payloadLen < kBuffMaskByteCount || position <= 0 || value == 0)
            return false;

        const int offset = GetBuffMaskPacketOffsetForPosition(position);
        if (offset < 0 || payloadLen < offset + static_cast<int>(sizeof(int)))
            return false;

        const unsigned int packetValue = static_cast<unsigned int>(ReadPacketInt(payload, offset));
        return (packetValue & value) == value;
    }

    bool IsMonsterRidingGiveBuffPayload(const BYTE* payload, int payloadLen)
    {
        return PacketMaskHasValue(payload, payloadLen, 8, 0x08000000u) &&
               payloadLen >= kMountGiveBuffSkillIdOffset + static_cast<int>(sizeof(int));
    }

    bool TryReadMonsterRidingGiveBuffData(
        const BYTE* payload,
        int payloadLen,
        int& outMountItemId,
        int& outSkillId)
    {
        outMountItemId = 0;
        outSkillId = 0;
        if (!IsMonsterRidingGiveBuffPayload(payload, payloadLen))
            return false;

        outMountItemId = ReadPacketInt(payload, kMountGiveBuffItemIdOffset);
        outSkillId = ReadPacketInt(payload, kMountGiveBuffSkillIdOffset);
        return outMountItemId > 0 && outSkillId > 0;
    }

    bool TryResolveMountedSuperSkillDisplaySkillIdFromActiveContext(
        int mountItemId,
        int observedSkillId,
        int& outDisplaySkillId)
    {
        outDisplaySkillId = 0;
        if (mountItemId <= 0 || observedSkillId <= 0 || !IsActiveNativeReleaseContextFresh())
            return false;
        if (g_activeNativeRelease.customSkillId <= 0)
            return false;

        SuperSkillDefinition definition = {};
        if (!FindSuperSkillDefinition(g_activeNativeRelease.customSkillId, definition))
            return false;
        if (definition.mountItemId <= 0 || definition.mountItemId != mountItemId)
            return false;

        const bool matchesBehaviorSkillId =
            definition.behaviorSkillId > 0 &&
            observedSkillId == definition.behaviorSkillId;
        const bool matchesClassifierProxySkillId =
            g_activeNativeRelease.classifierProxySkillId > 0 &&
            observedSkillId == g_activeNativeRelease.classifierProxySkillId;
        if (!matchesBehaviorSkillId && !matchesClassifierProxySkillId)
            return false;
        if (!GameLookupSkillEntryPointer(definition.skillId))
            return false;

        outDisplaySkillId = definition.skillId;
        return true;
    }

    int ResolveMountBuffDisplaySkillId(int mountItemId, int observedSkillId)
    {
        if (observedSkillId <= 0)
            return 0;

        int activeContextDisplaySkillId = 0;
        if (TryResolveMountedSuperSkillDisplaySkillIdFromActiveContext(
                mountItemId,
                observedSkillId,
                activeContextDisplaySkillId) &&
            activeContextDisplaySkillId != observedSkillId)
        {
            return activeContextDisplaySkillId;
        }

        const int displaySkillId = SkillOverlayBridgeResolveNativeLevelLookupSkillId(observedSkillId);
        if (displaySkillId > 0 &&
            displaySkillId != observedSkillId &&
            GameLookupSkillEntryPointer(displaySkillId))
        {
            return displaySkillId;
        }

        return 0;
    }

    bool RewritePacketMaskValue(
        BYTE* payload,
        int payloadLen,
        int clearPosition,
        unsigned int clearValue,
        int setPosition,
        unsigned int setValue)
    {
        if (!payload || payloadLen < kBuffMaskByteCount)
            return false;

        const int clearOffset = GetBuffMaskPacketOffsetForPosition(clearPosition);
        const int setOffset = GetBuffMaskPacketOffsetForPosition(setPosition);
        if (clearOffset < 0 || setOffset < 0)
            return false;
        if (payloadLen < clearOffset + static_cast<int>(sizeof(int)) ||
            payloadLen < setOffset + static_cast<int>(sizeof(int)))
        {
            return false;
        }

        unsigned int clearMask = static_cast<unsigned int>(ReadPacketInt(payload, clearOffset));
        unsigned int setMask = static_cast<unsigned int>(ReadPacketInt(payload, setOffset));
        clearMask &= ~clearValue;
        setMask |= setValue;
        WritePacketInt(payload, clearOffset, static_cast<int>(clearMask));
        WritePacketInt(payload, setOffset, static_cast<int>(setMask));
        return true;
    }

    bool BuildIndependentBuffRewriteState(const SuperSkillDefinition& definition, ActiveIndependentBuffRewriteState& outState)
    {
        outState = ActiveIndependentBuffRewriteState{};
        if (!definition.independentBuffEnabled)
            return false;
        if (definition.independentCarrierMaskPosition <= 0 ||
            definition.independentCarrierMaskValue == 0)
        {
            return false;
        }

        outState.active = true;
        outState.skillId = definition.skillId;
        outState.carrierMaskPosition = definition.independentCarrierMaskPosition;
        outState.carrierMaskValue = definition.independentCarrierMaskValue;
        outState.activatedTick = GetTickCount();
        const bool hasNativeRewriteConfig =
            definition.independentNativeMaskPosition > 0 &&
            definition.independentNativeMaskValue != 0;
        outState.rewriteToNative =
            DefinitionUsesNativeDisplay(definition) &&
            hasNativeRewriteConfig;
        if (outState.rewriteToNative)
        {
            outState.nativeMaskPosition = definition.independentNativeMaskPosition;
            outState.nativeMaskValue = definition.independentNativeMaskValue;
        }
        else if (hasNativeRewriteConfig)
        {
            WriteLogFmt("[IndependentBuffClient] native rewrite disabled skillId=%d displayMode=%d native=(%d,0x%08X)",
                definition.skillId,
                (int)definition.independentDisplayMode,
                definition.independentNativeMaskPosition,
                definition.independentNativeMaskValue);
        }
        return true;
    }

    void RefreshIndependentBuffRuntimeOwnerBinding();

    int FindNextAvailableIndependentBuffOverlaySlot(int skillIdToIgnore)
    {
        const int kMaxOverlaySlots = 8;
        for (int slotIndex = 0; slotIndex < kMaxOverlaySlots; ++slotIndex)
        {
            bool occupied = false;
            for (std::map<int, IndependentBuffOverlayState>::const_iterator it = g_independentBuffOverlayStates.begin();
                 it != g_independentBuffOverlayStates.end();
                 ++it)
            {
                if (it->first == skillIdToIgnore)
                    continue;
                if (it->second.slotIndex == slotIndex)
                {
                    occupied = true;
                    break;
                }
            }
            if (!occupied)
                return slotIndex;
        }

        return (int)g_independentBuffOverlayStates.size();
    }

    void UpdateIndependentBuffOverlayStateForDefinition(const SuperSkillDefinition& definition, int durationMs)
    {
        RefreshIndependentBuffRuntimeOwnerBinding();

        if (!DefinitionUsesOverlayDisplay(definition))
            return;

        IndependentBuffOverlayState state = {};
        state.skillId = definition.skillId;
        state.iconSkillId = definition.skillId;
        std::map<int, IndependentBuffOverlayState>::const_iterator existingStateIt =
            g_independentBuffOverlayStates.find(definition.skillId);
        state.slotIndex = (existingStateIt != g_independentBuffOverlayStates.end())
            ? existingStateIt->second.slotIndex
            : FindNextAvailableIndependentBuffOverlaySlot(definition.skillId);
        state.startTick = GetTickCount();
        state.durationMs = durationMs > 0 ? durationMs : 0;
        state.cancelable = true;
        state.activationOrder = ++g_independentBuffOverlayActivationCounter;
        g_independentBuffOverlayStates[definition.skillId] = state;
        g_recentIndependentBuffActivationTickBySkillId[definition.skillId] = state.startTick;
        WriteLogFmt("[IndependentBuffOverlay] activate skillId=%d iconSkillId=%d durationMs=%d slot=%d order=%llu",
            state.skillId,
            state.iconSkillId,
            state.durationMs,
            state.slotIndex,
            state.activationOrder);
    }

    void ClearIndependentBuffOverlayState(int skillId)
    {
        if (skillId <= 0)
            return;

        const size_t erased = g_independentBuffOverlayStates.erase(skillId);
        g_recentIndependentBuffActivationTickBySkillId.erase(skillId);
        if (erased > 0)
        {
            WriteLogFmt("[IndependentBuffOverlay] deactivate skillId=%d", skillId);
        }
    }

    void UpdateIndependentBuffVirtualState(int skillId, int iconSkillId, int durationMs)
    {
        if (skillId <= 0)
            return;

        const std::map<int, IndependentBuffOverlayState>::const_iterator existingIt =
            g_independentBuffVirtualStates.find(skillId);
        IndependentBuffOverlayState state = {};
        state.skillId = skillId;
        state.iconSkillId = iconSkillId > 0 ? iconSkillId : skillId;
        state.slotIndex = -1;
        state.startTick = GetTickCount();
        state.durationMs = durationMs > 0 ? durationMs : 0;
        state.cancelable = true;
        state.activationOrder = existingIt != g_independentBuffVirtualStates.end()
            ? existingIt->second.activationOrder
            : ++g_independentBuffOverlayActivationCounter;
        g_independentBuffVirtualStates[skillId] = state;
        WriteLogFmt("[IndependentBuffVirtual] %s skillId=%d iconSkillId=%d durationMs=%d order=%llu",
            existingIt != g_independentBuffVirtualStates.end() ? "refresh" : "activate",
            skillId,
            state.iconSkillId,
            state.durationMs,
            state.activationOrder);
    }

    void ClearIndependentBuffVirtualState(int skillId)
    {
        if (skillId <= 0)
            return;

        if (g_independentBuffVirtualStates.erase(skillId) > 0)
        {
            WriteLogFmt("[IndependentBuffVirtual] deactivate skillId=%d", skillId);
        }
    }

    bool ShouldTrackNativeVisibleBuffDefinition(const NativeBuffMaskDefinition& definition)
    {
        if (!definition.name || !definition.name[0])
            return false;
        return strcmp(definition.name, "DEFAULT_BUFFSTAT") != 0 &&
               strcmp(definition.name, "DEFAULT_BUFFSTAT2") != 0;
    }

    bool ShouldSuppressNativeVisibleBuffSkillId(int skillId)
    {
        if (skillId <= 0)
            return false;

        // SOARING is sent as a real native buff when flying mounts auto-apply flight.
        // If we suppress it here, fallback semantic-slot layout undercounts the native
        // top-row occupancy and our custom buff overlay can slide onto that extra icon.
        if (skillId == 80001089)
        {
            return false;
        }

        if (IsNativeFlyingMountSkillGateFamily(skillId))
        {
            return true;
        }

        HiddenSkillDefinition hiddenDefinition = {};
        if (FindHiddenSkillDefinition(skillId, hiddenDefinition) &&
            hiddenDefinition.hideFromNativeSkillWnd)
        {
            return true;
        }

        return false;
    }

    void RegisterNativeVisibleBuffStatesFromPayload(const BYTE* payload, int payloadLen)
    {
        if (!payload || payloadLen < kBuffMaskByteCount)
            return;

        int packetSkillId = 0;
        int durationMs = 0;
        int mountItemId = 0;
        if (!TryReadMonsterRidingGiveBuffData(payload, payloadLen, mountItemId, packetSkillId))
        {
            if (payloadLen >= kSingleStatGiveBuffSkillIdOffset + static_cast<int>(sizeof(int)))
                packetSkillId = ReadPacketInt(payload, kSingleStatGiveBuffSkillIdOffset);
            if (payloadLen >= kSingleStatGiveBuffDurationOffset + static_cast<int>(sizeof(int)))
                durationMs = ReadPacketInt(payload, kSingleStatGiveBuffDurationOffset);
        }
        if (packetSkillId <= 0 || packetSkillId > kIndependentBuffMaxReasonableSkillId)
        {
            WriteLogFmt("[IndependentBuffNativeVisible] suppress invalid skillId=%d payloadLen=%d",
                packetSkillId,
                payloadLen);
            return;
        }
        const DWORD now = GetTickCount();
        bool changed = false;

        for (size_t i = 0; i < ARRAYSIZE(kNativeBuffMaskDefinitions); ++i)
        {
            const NativeBuffMaskDefinition& definition = kNativeBuffMaskDefinitions[i];
            if (!ShouldTrackNativeVisibleBuffDefinition(definition))
                continue;
            if (!PacketMaskHasValue(payload, payloadLen, definition.position, definition.value))
                continue;
            if (ShouldSuppressNativeVisibleBuffSkillId(packetSkillId))
            {
                WriteLogFmt("[IndependentBuffNativeVisible] suppress hidden skillId=%d mask=(%d,0x%08X) durationMs=%d",
                    packetSkillId,
                    definition.position,
                    definition.value,
                    durationMs);
                continue;
            }

            NativeVisibleBuffState state = {};
            state.position = definition.position;
            state.value = definition.value;
            state.skillId = packetSkillId;
            state.startTick = now;
            state.durationMs = durationMs;
            const unsigned long long key = MakeIndependentBuffMaskKey(definition.position, definition.value);
            const std::map<unsigned long long, NativeVisibleBuffState>::const_iterator existingIt =
                g_nativeVisibleBuffStates.find(key);
            state.activationOrder = existingIt != g_nativeVisibleBuffStates.end()
                ? existingIt->second.activationOrder
                : ++g_independentBuffOverlayActivationCounter;
            g_nativeVisibleBuffStates[key] = state;
            changed = true;
            WriteLogFmt("[IndependentBuffNativeVisible] %s skillId=%d mask=(%d,0x%08X) durationMs=%d order=%llu",
                existingIt != g_nativeVisibleBuffStates.end() ? "refresh" : "activate",
                packetSkillId,
                definition.position,
                definition.value,
                    durationMs,
                    state.activationOrder);
        }

        if (changed)
        {
            const DWORD liveStatusBar = TryGetLiveStatusBar();
            if (liveStatusBar)
                SkillOverlayBridgeSetObservedStatusBarPtr(liveStatusBar);
            WriteLogFmt("[IndependentBuffNativeVisible] liveStatusBar=0x%08X after give/refresh skillId=%d",
                liveStatusBar,
                packetSkillId);
            ForceRefreshIndependentBuffUi("native_visible_give");
        }
    }

    void RemoveNativeVisibleBuffStatesFromPayload(const BYTE* payload, int payloadLen)
    {
        if (!payload || payloadLen < kBuffMaskByteCount)
            return;

        bool changed = false;
        for (size_t i = 0; i < ARRAYSIZE(kNativeBuffMaskDefinitions); ++i)
        {
            const NativeBuffMaskDefinition& definition = kNativeBuffMaskDefinitions[i];
            if (!ShouldTrackNativeVisibleBuffDefinition(definition))
                continue;
            if (!PacketMaskHasValue(payload, payloadLen, definition.position, definition.value))
                continue;

            if (g_nativeVisibleBuffStates.erase(MakeIndependentBuffMaskKey(definition.position, definition.value)) > 0)
            {
                changed = true;
                WriteLogFmt("[IndependentBuffNativeVisible] deactivate mask=(%d,0x%08X)",
                    definition.position,
                    definition.value);
            }
        }

        if (changed)
        {
            const DWORD liveStatusBar = TryGetLiveStatusBar();
            if (liveStatusBar)
                SkillOverlayBridgeSetObservedStatusBarPtr(liveStatusBar);
            WriteLogFmt("[IndependentBuffNativeVisible] liveStatusBar=0x%08X after cancel",
                liveStatusBar);
            ForceRefreshIndependentBuffUi("native_visible_cancel");
        }
    }

    void PruneExpiredNativeVisibleBuffStates()
    {
        const DWORD now = GetTickCount();
        for (std::map<unsigned long long, NativeVisibleBuffState>::iterator it = g_nativeVisibleBuffStates.begin();
             it != g_nativeVisibleBuffStates.end();)
        {
            const NativeVisibleBuffState& state = it->second;
            if (state.durationMs > 0)
            {
                const DWORD elapsed = now - state.startTick;
                if (elapsed >= static_cast<DWORD>(state.durationMs))
                {
                    it = g_nativeVisibleBuffStates.erase(it);
                    continue;
                }
            }
            ++it;
        }
    }

    int CountActiveNativeVisibleBuffStates()
    {
        PruneExpiredNativeVisibleBuffStates();
        if (g_observedNativeVisibleBuffVisualCount > 0)
            return g_observedNativeVisibleBuffVisualCount;
        return (int)g_nativeVisibleBuffStates.size();
    }

    int FindNativeBuffMaskDefinitionOrder(int position, unsigned int value)
    {
        for (int i = 0; i < (int)ARRAYSIZE(kNativeBuffMaskDefinitions); ++i)
        {
            const NativeBuffMaskDefinition& definition = kNativeBuffMaskDefinitions[i];
            if (definition.position == position && definition.value == value)
                return i;
        }
        return -1;
    }

    DWORD TryGetCurrentIndependentBuffOwnerUserLocal()
    {
        DWORD userLocal = 0;
        if (!SafeReadValue(ADDR_UserLocal, userLocal) ||
            !userLocal ||
            SafeIsBadReadPtr(reinterpret_cast<void*>(userLocal), 4))
        {
            return 0;
        }
        return userLocal;
    }

    DWORD TryGetCurrentIndependentBuffOwnerNetClient()
    {
        DWORD netClient = 0;
        if (!SafeReadValue(ADDR_NetClient, netClient) ||
            !netClient ||
            SafeIsBadReadPtr(reinterpret_cast<void*>(netClient), 4))
        {
            return 0;
        }
        return netClient;
    }

    bool HasAnyIndependentBuffRuntimeState()
    {
        return !g_activeIndependentBuffRewriteStates.empty() ||
               !g_activeLocalIndependentPotentialBySkillId.empty() ||
               !g_independentBuffOverlayStates.empty() ||
               !g_independentBuffVirtualStates.empty() ||
               !g_nativeVisibleBuffStates.empty();
    }

    bool IsIndependentBuffGameplaySceneActive()
    {
        const DWORD userLocal = TryGetCurrentIndependentBuffOwnerUserLocal();
        const DWORD skillContext = TryGetLiveSkillContext();
        if (userLocal != 0)
            return true;
        return skillContext != 0;
    }

    void SyncMergedLocalPotentialBufferToExternalAddress();

    void ClearIndependentBuffRuntimeState(const char* reason)
    {
        const bool hadRewriteStates = !g_activeIndependentBuffRewriteStates.empty();
        const bool hadLocalStates = !g_activeLocalIndependentPotentialBySkillId.empty();
        const bool hadOverlayStates = !g_independentBuffOverlayStates.empty();
        const bool hadNativeVisibleStates = !g_nativeVisibleBuffStates.empty();

        g_activeIndependentBuffRewriteStates.clear();
        g_activeLocalIndependentPotentialBySkillId.clear();
        g_localIndependentPotentialBaseBuffer.fill(0);
        g_localIndependentPotentialDeltaBuffer.fill(0);
        g_localIndependentPotentialMergedBuffer.fill(0);
        g_observedNativeVisibleBuffVisualCount = -1;
        g_observedNativeVisibleBuffAnchorX = -1;
        g_independentBuffOverlayStates.clear();
        g_independentBuffVirtualStates.clear();
        g_nativeVisibleBuffStates.clear();
        g_recentIndependentBuffClientCancelTickBySkillId.clear();
        g_recentIndependentBuffClientUseTickBySkillId.clear();
        g_recentIndependentBuffActivationTickBySkillId.clear();
        g_independentBuffOverlayActivationCounter = 0;
        g_independentBuffOwnerUserLocal = 0;
        g_independentBuffOwnerNetClient = 0;
        g_independentBuffSceneDetachSinceTick = 0;
        g_independentBuffOwnerMissingSinceTick = 0;
        SyncMergedLocalPotentialBufferToExternalAddress();

        if (hadRewriteStates || hadLocalStates || hadOverlayStates || hadNativeVisibleStates)
        {
            WriteLogFmt("[IndependentBuffRuntime] clear reason=%s rewrite=%d local=%d overlay=%d nativeVisible=%d",
                reason ? reason : "unknown",
                hadRewriteStates ? 1 : 0,
                hadLocalStates ? 1 : 0,
                hadOverlayStates ? 1 : 0,
                hadNativeVisibleStates ? 1 : 0);
        }
    }

    void RefreshIndependentBuffRuntimeOwnerBinding()
    {
        const DWORD nowTick = GetTickCount();
        const DWORD currentNetClient = TryGetCurrentIndependentBuffOwnerNetClient();
        const DWORD currentUserLocal = TryGetCurrentIndependentBuffOwnerUserLocal();
        const DWORD liveSkillContext = TryGetLiveSkillContext();
        const DWORD liveSkillDataMgr = TryGetLiveSkillDataMgr();
        const DWORD liveStatusBar = TryGetLiveStatusBar();
        const bool hasRuntimeState = HasAnyIndependentBuffRuntimeState();
        const bool sceneLikelyDetached =
            currentUserLocal == 0 &&
            liveSkillContext == 0;

        if (hasRuntimeState && sceneLikelyDetached)
        {
            if (g_independentBuffSceneDetachSinceTick == 0)
            {
                g_independentBuffSceneDetachSinceTick = nowTick;
                WriteLogFmt("[IndependentBuffRuntime] detach pending skillContext=0x%08X skillDataMgr=0x%08X statusBar=0x%08X userLocal=0x%08X netClient=0x%08X",
                    liveSkillContext,
                    liveSkillDataMgr,
                    liveStatusBar,
                    currentUserLocal,
                    currentNetClient);
            }
            else if (nowTick - g_independentBuffSceneDetachSinceTick >= kIndependentBuffSceneDetachClearDelayMs)
            {
                ClearIndependentBuffRuntimeState("scene_detached");
                return;
            }
        }
        else
        {
            g_independentBuffSceneDetachSinceTick = 0;
        }

        if (hasRuntimeState &&
            currentNetClient == 0 &&
            currentUserLocal == 0)
        {
            if (g_independentBuffOwnerMissingSinceTick == 0)
            {
                g_independentBuffOwnerMissingSinceTick = nowTick;
                WriteLogFmt("[IndependentBuffRuntime] owner missing pending skillContext=0x%08X skillDataMgr=0x%08X statusBar=0x%08X",
                    liveSkillContext,
                    liveSkillDataMgr,
                    liveStatusBar);
            }
            else if (nowTick - g_independentBuffOwnerMissingSinceTick >= kIndependentBuffOwnerMissingClearDelayMs)
            {
                ClearIndependentBuffRuntimeState("owner_missing");
                return;
            }
        }
        else
        {
            g_independentBuffOwnerMissingSinceTick = 0;
        }

        if (currentNetClient)
        {
            if (!HasAnyIndependentBuffRuntimeState())
            {
                g_independentBuffOwnerNetClient = currentNetClient;
            }
            else if (g_independentBuffOwnerNetClient == 0)
            {
                g_independentBuffOwnerNetClient = currentNetClient;
            }
            else if (g_independentBuffOwnerNetClient != currentNetClient)
            {
                const DWORD previousNetClient = g_independentBuffOwnerNetClient;
                ClearIndependentBuffRuntimeState("netclient_changed");
                g_independentBuffOwnerNetClient = currentNetClient;
                WriteLogFmt("[IndependentBuffRuntime] netclient changed old=0x%08X new=0x%08X",
                    previousNetClient,
                    currentNetClient);
                return;
            }
        }

        if (!currentUserLocal)
            return;

        if (!HasAnyIndependentBuffRuntimeState())
        {
            g_independentBuffOwnerUserLocal = currentUserLocal;
            return;
        }

        if (g_independentBuffOwnerUserLocal == 0)
        {
            g_independentBuffOwnerUserLocal = currentUserLocal;
            return;
        }

        if (g_independentBuffOwnerUserLocal != currentUserLocal)
        {
            const DWORD previousUserLocal = g_independentBuffOwnerUserLocal;
            const bool likelyMapRebind =
                currentNetClient != 0 &&
                g_independentBuffOwnerNetClient != 0 &&
                currentNetClient == g_independentBuffOwnerNetClient &&
                (liveSkillContext != 0 || liveStatusBar != 0);
            if (likelyMapRebind)
            {
                g_independentBuffOwnerUserLocal = currentUserLocal;
                WriteLogFmt("[IndependentBuffRuntime] userlocal rebound old=0x%08X new=0x%08X keepState=1 skillContext=0x%08X statusBar=0x%08X",
                    previousUserLocal,
                    currentUserLocal,
                    liveSkillContext,
                    liveStatusBar);
                return;
            }
            ClearIndependentBuffRuntimeState("userlocal_changed");
            g_independentBuffOwnerUserLocal = currentUserLocal;
            WriteLogFmt("[IndependentBuffRuntime] userlocal changed old=0x%08X new=0x%08X",
                previousUserLocal,
                currentUserLocal);
            return;
        }

        g_independentBuffOwnerUserLocal = currentUserLocal;
    }

    void ClearLocalPotentialDeltaBuffer(LocalPotentialDeltaBuffer& buffer)
    {
        buffer.fill(0);
    }

    bool AddLocalPotentialDeltaValue(LocalPotentialDeltaBuffer& buffer, int offset, int value)
    {
        if (offset < 0 || offset + static_cast<int>(sizeof(int)) > kLocalIndependentPotentialBufferBytes)
            return false;
        buffer[(size_t)(offset / static_cast<int>(sizeof(int)))] += value;
        return true;
    }

    void SyncMergedLocalPotentialBufferToExternalAddress()
    {
        ClearLocalPotentialDeltaBuffer(g_localIndependentPotentialMergedBuffer);

        for (int index = 0; index < kLocalIndependentPotentialBufferIntCount; ++index)
        {
            g_localIndependentPotentialMergedBuffer[index] =
                g_localIndependentPotentialBaseBuffer[index] +
                g_localIndependentPotentialDeltaBuffer[index];
        }

        if (g_localIndependentPotentialIncreaseAddress &&
            !SafeIsBadWritePtr(reinterpret_cast<void*>(g_localIndependentPotentialIncreaseAddress), kLocalIndependentPotentialBufferBytes))
        {
            memcpy(reinterpret_cast<void*>(g_localIndependentPotentialIncreaseAddress),
                   g_localIndependentPotentialMergedBuffer.data(),
                   kLocalIndependentPotentialBufferBytes);
        }
    }

    void RebuildLocalIndependentPotentialIncreaseBuffer()
    {
        ClearLocalPotentialDeltaBuffer(g_localIndependentPotentialDeltaBuffer);

        for (std::map<int, LocalPotentialDeltaBuffer>::const_iterator skillIt = g_activeLocalIndependentPotentialBySkillId.begin();
             skillIt != g_activeLocalIndependentPotentialBySkillId.end();
             ++skillIt)
        {
            const LocalPotentialDeltaBuffer& values = skillIt->second;
            for (size_t index = 0; index < values.size(); ++index)
            {
                g_localIndependentPotentialDeltaBuffer[index] += values[index];
            }
        }

        SyncMergedLocalPotentialBufferToExternalAddress();
    }

    void UpdateLocalIndependentPotentialStateForDefinition(const SuperSkillDefinition& definition, bool active)
    {
        RefreshIndependentBuffRuntimeOwnerBinding();

        if (definition.skillId <= 0)
            return;

        if (!active)
        {
            g_activeLocalIndependentPotentialBySkillId.erase(definition.skillId);
            RebuildLocalIndependentPotentialIncreaseBuffer();
            WriteLogFmt("[IndependentBuffLocal] deactivate skillId=%d", definition.skillId);
            return;
        }

        if (definition.clientBonusSpecs.empty())
            return;

        int sourceSkillId = definition.independentSourceSkillId > 0 ? definition.independentSourceSkillId : definition.skillId;
        int sourceSkillLevel = GetRuntimeAppliedSkillLevel(definition.skillId);
        if (sourceSkillLevel <= 0 && sourceSkillId != definition.skillId)
            sourceSkillLevel = GetRuntimeAppliedSkillLevel(sourceSkillId);
        if (sourceSkillId <= 0 || sourceSkillLevel <= 0)
            return;

        LocalPotentialDeltaBuffer values = {};
        ClearLocalPotentialDeltaBuffer(values);
        for (std::map<std::string, PassiveValueSpec>::const_iterator it = definition.clientBonusSpecs.begin();
             it != definition.clientBonusSpecs.end();
             ++it)
        {
            int resolvedValue = 0;
            if (!ResolvePassiveValueForLevel(sourceSkillId, sourceSkillLevel, it->second, resolvedValue))
                continue;

            const std::string normalizedKey = NormalizeBuffStatIdentifier(it->first);
            if (normalizedKey == "ALLSTAT")
            {
                const int offsets[] = { 0x08, 0x0C, 0x10, 0x14 };
                for (size_t offsetIndex = 0; offsetIndex < ARRAYSIZE(offsets); ++offsetIndex)
                {
                    AddLocalPotentialDeltaValue(values, offsets[offsetIndex], resolvedValue);
                    WriteLogFmt("[IndependentBuffLocal] activate skillId=%d key=%s offset=0x%X value=%d sourceSkillId=%d level=%d",
                        definition.skillId,
                        it->first.c_str(),
                        offsets[offsetIndex],
                        resolvedValue,
                        sourceSkillId,
                        sourceSkillLevel);
                }
                continue;
            }

            if (normalizedKey == "ALLSTATPERCENT")
            {
                const int offsets[] = { 0x48, 0x4C, 0x50, 0x54 };
                for (size_t offsetIndex = 0; offsetIndex < ARRAYSIZE(offsets); ++offsetIndex)
                {
                    AddLocalPotentialDeltaValue(values, offsets[offsetIndex], resolvedValue);
                    WriteLogFmt("[IndependentBuffLocal] activate skillId=%d key=%s offset=0x%X value=%d sourceSkillId=%d level=%d",
                        definition.skillId,
                        it->first.c_str(),
                        offsets[offsetIndex],
                        resolvedValue,
                        sourceSkillId,
                        sourceSkillLevel);
                }
                continue;
            }

            const int offset = ResolveLocalPotentialOffsetForBonusKey(normalizedKey);
            if (offset < 0)
                continue;

            AddLocalPotentialDeltaValue(values, offset, resolvedValue);
            WriteLogFmt("[IndependentBuffLocal] activate skillId=%d key=%s offset=0x%X value=%d sourceSkillId=%d level=%d",
                definition.skillId,
                it->first.c_str(),
                offset,
                resolvedValue,
                sourceSkillId,
                sourceSkillLevel);
        }

        bool hasAnyValue = false;
        for (size_t index = 0; index < values.size(); ++index)
        {
            if (values[index] != 0)
            {
                hasAnyValue = true;
                break;
            }
        }
        if (!hasAnyValue)
        {
            if (!definition.localBonusKey.empty())
            {
                WriteLogFmt("[IndependentBuffLocal] unsupported local bonus skillId=%d key=%s",
                    definition.skillId,
                    definition.localBonusKey.c_str());
            }
            return;
        }

        g_activeLocalIndependentPotentialBySkillId[definition.skillId] = values;
        RebuildLocalIndependentPotentialIncreaseBuffer();
    }

    uintptr_t PrepareLocalIndependentPotentialCombined(uintptr_t sourcePtr)
    {
        RefreshIndependentBuffRuntimeOwnerBinding();

        if (g_activeLocalIndependentPotentialBySkillId.empty())
            return sourcePtr;

        if (sourcePtr && !SafeIsBadReadPtr(reinterpret_cast<void*>(sourcePtr), kLocalIndependentPotentialBufferBytes))
        {
            memcpy(g_localIndependentPotentialBaseBuffer.data(), reinterpret_cast<const void*>(sourcePtr), kLocalIndependentPotentialBufferBytes);
        }

        SyncMergedLocalPotentialBufferToExternalAddress();

        static DWORD s_lastPrepareLogTick = 0;
        const DWORD now = GetTickCount();
        if (now - s_lastPrepareLogTick > 1000)
        {
            s_lastPrepareLogTick = now;
            WriteLogFmt("[IndependentBuffLocal] prepare source=0x%08X result=0x%08X wdef=%d speed=%d jump=%d activeSkills=%d",
                (DWORD)sourcePtr,
                (DWORD)(uintptr_t)g_localIndependentPotentialMergedBuffer.data(),
                g_localIndependentPotentialDeltaBuffer[0x40 / sizeof(int)],
                g_localIndependentPotentialDeltaBuffer[0x30 / sizeof(int)],
                g_localIndependentPotentialDeltaBuffer[0x34 / sizeof(int)],
                (int)g_activeLocalIndependentPotentialBySkillId.size());
        }

        return reinterpret_cast<uintptr_t>(g_localIndependentPotentialMergedBuffer.data());
    }

    uintptr_t PrepareLocalIndependentPotentialDisplayCombined(uintptr_t sourcePtr)
    {
        RefreshIndependentBuffRuntimeOwnerBinding();

        if (g_activeLocalIndependentPotentialBySkillId.empty())
            return sourcePtr;

        ClearLocalPotentialDeltaBuffer(g_localIndependentPotentialDisplayBuffer);
        if (sourcePtr && !SafeIsBadReadPtr(reinterpret_cast<void*>(sourcePtr), kLocalIndependentPotentialBufferBytes))
        {
            memcpy(g_localIndependentPotentialDisplayBuffer.data(),
                   reinterpret_cast<const void*>(sourcePtr),
                   kLocalIndependentPotentialBufferBytes);
        }

        for (int index = 0; index < kLocalIndependentPotentialBufferIntCount; ++index)
            g_localIndependentPotentialDisplayBuffer[index] += g_localIndependentPotentialDeltaBuffer[index];

        static DWORD s_lastDisplayPrepareLogTick = 0;
        const DWORD now = GetTickCount();
        if (now - s_lastDisplayPrepareLogTick > 1000)
        {
            s_lastDisplayPrepareLogTick = now;
            WriteLogFmt("[IndependentBuffLocalDisplay] prepare source=0x%08X result=0x%08X wdef=%d mdef=%d watk=%d matk=%d activeSkills=%d",
                (DWORD)sourcePtr,
                (DWORD)(uintptr_t)g_localIndependentPotentialDisplayBuffer.data(),
                g_localIndependentPotentialDeltaBuffer[0x40 / sizeof(int)],
                g_localIndependentPotentialDeltaBuffer[0x44 / sizeof(int)],
                g_localIndependentPotentialDeltaBuffer[0x38 / sizeof(int)],
                g_localIndependentPotentialDeltaBuffer[0x3C / sizeof(int)],
                (int)g_activeLocalIndependentPotentialBySkillId.size());
        }

        return reinterpret_cast<uintptr_t>(g_localIndependentPotentialDisplayBuffer.data());
    }

    bool TryReadSingleStatGiveBuffSkillId(const BYTE* payload, int payloadLen, int& outSkillId)
    {
        outSkillId = 0;
        if (!payload || payloadLen < kSingleStatGiveBuffSkillIdOffset + static_cast<int>(sizeof(int)))
            return false;

        outSkillId = ReadPacketInt(payload, kSingleStatGiveBuffSkillIdOffset);
        return outSkillId > 0;
    }

    bool TryReadSingleStatGiveBuffDurationMs(const BYTE* payload, int payloadLen, int& outDurationMs)
    {
        outDurationMs = 0;
        if (!payload || payloadLen < kSingleStatGiveBuffDurationOffset + static_cast<int>(sizeof(int)))
            return false;

        outDurationMs = ReadPacketInt(payload, kSingleStatGiveBuffDurationOffset);
        if (outDurationMs < 0)
            outDurationMs = 0;
        return true;
    }

    void ApplyPotentialOptionPacketToBaseBuffer(const BYTE* payload, int payloadLen)
    {
        if (!payload || payloadLen <= 0)
            return;

        ClearLocalPotentialDeltaBuffer(g_localIndependentPotentialBaseBuffer);

        for (int offset = 0; offset + 8 <= payloadLen; offset += 8)
        {
            const int optionId = ReadPacketInt(payload, offset);
            const int optionValue = ReadPacketInt(payload, offset + 4);
            const int localOffset = ResolveLocalPotentialOffsetForOptionId(optionId);
            if (localOffset < 0)
                continue;
            AddLocalPotentialDeltaValue(g_localIndependentPotentialBaseBuffer, localOffset, optionValue);
        }

        SyncMergedLocalPotentialBufferToExternalAddress();
    }

    void ApplyPotentialBaseValueInternal(uintptr_t writeAddress, int baseValue)
    {
        if (!g_localIndependentPotentialIncreaseAddress || writeAddress < g_localIndependentPotentialIncreaseAddress)
            return;

        const uintptr_t byteOffset = writeAddress - g_localIndependentPotentialIncreaseAddress;
        if (byteOffset + sizeof(int) > (uintptr_t)kLocalIndependentPotentialBufferBytes ||
            (byteOffset % sizeof(int)) != 0)
        {
            return;
        }

        const size_t index = (size_t)(byteOffset / sizeof(int));
        if (index >= g_localIndependentPotentialBaseBuffer.size())
            return;

        g_localIndependentPotentialBaseBuffer[index] = baseValue;
        SyncMergedLocalPotentialBufferToExternalAddress();
    }

    void ClearPotentialBaseValuesInternal()
    {
        ClearLocalPotentialDeltaBuffer(g_localIndependentPotentialBaseBuffer);
        SyncMergedLocalPotentialBufferToExternalAddress();
    }

    bool TryResolveIndependentBuffDefinitionForCancel(int skillId, SuperSkillDefinition& outDefinition)
    {
        if (FindSuperSkillDefinition(skillId, outDefinition))
            return true;

        for (std::map<int, SuperSkillDefinition>::const_iterator it = g_superSkillsBySkillId.begin();
             it != g_superSkillsBySkillId.end();
             ++it)
        {
            const SuperSkillDefinition& definition = it->second;
            const int displaySkillId = definition.independentNativeDisplaySkillId > 0
                ? definition.independentNativeDisplaySkillId
                : definition.skillId;
            if (skillId == definition.skillId ||
                skillId == displaySkillId)
            {
                outDefinition = definition;
                return true;
            }
        }

        return false;
    }

    bool TryResolveObservedIndependentBuffSkillId(
        int observedSkillId,
        CustomSkillPacketRoute packetRoute,
        int& outSkillId)
    {
        outSkillId = 0;
        if (observedSkillId <= 0)
            return false;

        SuperSkillDefinition definition = {};
        if (FindSuperSkillDefinition(observedSkillId, definition) &&
            definition.independentBuffEnabled)
        {
            outSkillId = definition.skillId;
            return true;
        }

        CustomSkillUseRoute route = {};
        if (!FindRouteByProxySkillId(observedSkillId, packetRoute, route))
            return false;

        if (!FindSuperSkillDefinition(route.skillId, definition) ||
            !definition.independentBuffEnabled)
        {
            return false;
        }

        outSkillId = definition.skillId;
        return true;
    }

    void MarkRecentIndependentBuffManualUse(
        int skillId,
        int observedSkillId,
        CustomSkillPacketRoute packetRoute,
        unsigned short opcode,
        uintptr_t callerRetAddr)
    {
        if (skillId <= 0)
            return;

        const DWORD now = GetTickCount();
        g_recentIndependentBuffClientUseTickBySkillId[skillId] = now;

        std::map<int, DWORD>::iterator cancelIt =
            g_recentIndependentBuffClientCancelTickBySkillId.find(skillId);
        if (cancelIt == g_recentIndependentBuffClientCancelTickBySkillId.end())
            return;

        const DWORD cancelDelta = now - cancelIt->second;
        g_recentIndependentBuffClientCancelTickBySkillId.erase(cancelIt);
        WriteLogFmt(
            "[IndependentBuffClient] rearm after manual-use skillId=%d observed=%d route=%s opcode=0x%X cancelDelta=%u caller=0x%08X",
            skillId,
            observedSkillId,
            PacketRouteToString(packetRoute),
            (unsigned int)opcode,
            (unsigned int)cancelDelta,
            (DWORD)(uintptr_t)callerRetAddr);
    }

    bool CancelIndependentBuffLocalState(const SuperSkillDefinition& definition)
    {
        if (definition.skillId <= 0)
            return false;

        bool changed = false;

        std::map<int, LocalPotentialDeltaBuffer>::iterator localIt =
            g_activeLocalIndependentPotentialBySkillId.find(definition.skillId);
        if (localIt != g_activeLocalIndependentPotentialBySkillId.end())
        {
            ApplyImmediateLiveAbilityCancelDelta(localIt->second);
            g_activeLocalIndependentPotentialBySkillId.erase(localIt);
            RebuildLocalIndependentPotentialIncreaseBuffer();
            changed = true;
        }

        if (g_independentBuffOverlayStates.erase(definition.skillId) > 0)
            changed = true;

        g_recentIndependentBuffActivationTickBySkillId.erase(definition.skillId);

        for (std::map<unsigned long long, ActiveIndependentBuffRewriteState>::iterator it = g_activeIndependentBuffRewriteStates.begin();
             it != g_activeIndependentBuffRewriteStates.end();)
        {
            if (it->second.skillId == definition.skillId)
            {
                it = g_activeIndependentBuffRewriteStates.erase(it);
                changed = true;
                continue;
            }
            ++it;
        }

        if (changed)
        {
            WriteLogFmt("[IndependentBuffOverlay] cancel local-only skillId=%d", definition.skillId);
        }
        else
        {
            WriteLogFmt("[IndependentBuffOverlay] cancel local-only noop skillId=%d", definition.skillId);
        }

        return changed;
    }

    bool ClearIndependentBuffRuntimeStateForDefinition(const SuperSkillDefinition& definition, const char* reason)
    {
        if (definition.skillId <= 0)
            return false;

        const bool clearedLocalState = CancelIndependentBuffLocalState(definition);

        std::vector<int> relatedSkillIds;
        relatedSkillIds.reserve(4);
        auto appendRelatedSkillId = [&relatedSkillIds](int skillId)
        {
            if (skillId <= 0)
                return;

            for (size_t i = 0; i < relatedSkillIds.size(); ++i)
            {
                if (relatedSkillIds[i] == skillId)
                    return;
            }
            relatedSkillIds.push_back(skillId);
        };
        auto matchesRelatedSkillId = [&relatedSkillIds](int skillId)
        {
            if (skillId <= 0)
                return false;

            for (size_t i = 0; i < relatedSkillIds.size(); ++i)
            {
                if (relatedSkillIds[i] == skillId)
                    return true;
            }
            return false;
        };

        appendRelatedSkillId(definition.skillId);
        appendRelatedSkillId(definition.independentNativeDisplaySkillId);
        appendRelatedSkillId(definition.behaviorSkillId);

        CustomSkillUseRoute route = {};
        if (FindRouteByCustomSkillId(definition.skillId, route))
            appendRelatedSkillId(route.proxySkillId);

        bool changed = clearedLocalState;
        int clearedVirtualStates = 0;
        for (std::map<int, IndependentBuffOverlayState>::iterator it = g_independentBuffVirtualStates.begin();
             it != g_independentBuffVirtualStates.end();)
        {
            const IndependentBuffOverlayState& state = it->second;
            if (matchesRelatedSkillId(it->first) ||
                matchesRelatedSkillId(state.skillId) ||
                matchesRelatedSkillId(state.iconSkillId))
            {
                it = g_independentBuffVirtualStates.erase(it);
                ++clearedVirtualStates;
                changed = true;
                continue;
            }
            ++it;
        }

        int clearedNativeVisibleStates = 0;
        for (std::map<unsigned long long, NativeVisibleBuffState>::iterator it = g_nativeVisibleBuffStates.begin();
             it != g_nativeVisibleBuffStates.end();)
        {
            if (matchesRelatedSkillId(it->second.skillId))
            {
                it = g_nativeVisibleBuffStates.erase(it);
                ++clearedNativeVisibleStates;
                changed = true;
                continue;
            }
            ++it;
        }

        const bool clearedRecentCancelTick =
            g_recentIndependentBuffClientCancelTickBySkillId.erase(definition.skillId) > 0;
        const bool clearedRecentUseTick =
            g_recentIndependentBuffClientUseTickBySkillId.erase(definition.skillId) > 0;
        if (clearedRecentCancelTick || clearedRecentUseTick)
            changed = true;

        if (changed)
        {
            WriteLogFmt(
                "[IndependentBuffRuntime] clear-by-skill skillId=%d reason=%s local=%d virtual=%d nativeVisible=%d recentCancel=%d recentUse=%d",
                definition.skillId,
                reason ? reason : "unknown",
                clearedLocalState ? 1 : 0,
                clearedVirtualStates,
                clearedNativeVisibleStates,
                clearedRecentCancelTick ? 1 : 0,
                clearedRecentUseTick ? 1 : 0);
        }

        return changed;
    }

    bool TryLogUnmappedCustomSkillPacket(const BYTE* packet, int packetLen, unsigned short opcode, uintptr_t callerRetAddr)
    {
        if (!packet || packetLen < 8)
            return false;

        const int candidateOffsets[] = { 2, 4, 6, 8, 10, 12, 14, 16, 18 };
        for (int i = 0; i < (int)ARRAYSIZE(candidateOffsets); ++i)
        {
            const int skillIdOffset = candidateOffsets[i];
            if (packetLen < skillIdOffset + 4)
                continue;

            const int candidateSkillId = ReadPacketInt(packet, skillIdOffset);
            if (!ShouldLogObservedSkillPacket(candidateSkillId))
                continue;

            const DWORD now = GetTickCount();
            if (g_lastUnmappedRouteOpcode == opcode &&
                now - g_lastUnmappedRouteLogTick < 1000)
            {
                return true;
            }

            g_lastUnmappedRouteOpcode = opcode;
            g_lastUnmappedRouteLogTick = now;
            WriteLogFmt(
                "[SkillRoute] unmapped opcode=0x%X candidateSkillId=%d offset=%d len=%d caller=0x%08X (add packetRoute in custom_skill_routes.json)",
                (unsigned int)opcode,
                candidateSkillId,
                skillIdOffset,
                packetLen,
                (DWORD)(uintptr_t)callerRetAddr);
            return true;
        }

        return false;
    }

    bool TryRewritePendingSuperSkillUpgradePacket(
        BYTE* packet,
        int packetLen,
        unsigned short opcode,
        uintptr_t callerRetAddr)
    {
        if (!packet || packetLen < 10 || opcode != 0x0092)
            return false;
        if (!IsPendingSuperSkillUpgradePacketRewriteFresh())
            return false;

        const int observedSkillId = ReadPacketInt(packet, 6);
        if (observedSkillId != g_pendingSuperSkillUpgradePacketRewrite.proxySkillId)
            return false;

        const int targetSkillId = g_pendingSuperSkillUpgradePacketRewrite.targetSkillId;
        WritePacketInt(packet, 6, targetSkillId);
        WriteLogFmt("[SuperSkill] rewrite distribute_sp proxy=%d -> target=%d len=%d caller=0x%08X",
            observedSkillId,
            targetSkillId,
            packetLen,
            (DWORD)(uintptr_t)callerRetAddr);

        // Super-skill upgrades now wait for the authoritative server sync packet
        // instead of optimistic local +1 / -SP. Fail paths only send enableActions()
        // and no level-sync packet, so optimistic bookkeeping could leave the UI
        // showing a fake level/SP increase for rejected upgrades.

        ClearPendingSuperSkillUpgradePacketRewrite();
        return true;
    }

    CustomSkillPacketRoute ResolvePacketRoute(unsigned short opcode, int& outSkillIdOffset, int& outLevelOffset)
    {
        outSkillIdOffset = -1;
        outLevelOffset = -1;

        switch (opcode)
        {
        case 0x0046:
            outSkillIdOffset = 4;
            return CustomSkillPacketRoute_CloseRange;
        case 0x0049: // PASSIVE_ENERGY: packet body follows close-range layout
            outSkillIdOffset = 4;
            return CustomSkillPacketRoute_CloseRange;
        case 0x0047:
            outSkillIdOffset = 4;
            return CustomSkillPacketRoute_RangedAttack;
        case 0x0048:
            outSkillIdOffset = 4;
            return CustomSkillPacketRoute_MagicAttack;
        case 0x0093:
            outSkillIdOffset = 6;
            outLevelOffset = 10;
            return CustomSkillPacketRoute_SpecialMove;
        case 0x0095:
            outSkillIdOffset = 2;
            outLevelOffset = 6;
            return CustomSkillPacketRoute_SkillEffect;
        case 0x0094:
            outSkillIdOffset = 2;
            return CustomSkillPacketRoute_CancelBuff;
        case 0x00AA:
            outSkillIdOffset = 18;
            return CustomSkillPacketRoute_SpecialAttack;
        default:
            return CustomSkillPacketRoute_None;
        }
    }

    bool IntVectorContains(const std::vector<int>& values, int needle)
    {
        for (size_t i = 0; i < values.size(); ++i)
        {
            if (values[i] == needle)
                return true;
        }
        return false;
    }

    int ResolveConfiguredPassiveBonusForSkill(
        int targetSkillId,
        PassiveValueSpec PassiveBonusDefinition::* valueMember)
    {
        if (targetSkillId <= 0 || !valueMember || g_superSkillsBySkillId.empty())
            return 0;

        int total = 0;
        for (std::map<int, SuperSkillDefinition>::const_iterator it = g_superSkillsBySkillId.begin();
             it != g_superSkillsBySkillId.end();
             ++it)
        {
            const SuperSkillDefinition& definition = it->second;
            if (definition.passiveBonuses.empty())
                continue;

            for (size_t bonusIndex = 0; bonusIndex < definition.passiveBonuses.size(); ++bonusIndex)
            {
                const PassiveBonusDefinition& bonus = definition.passiveBonuses[bonusIndex];
                if (!IntVectorContains(bonus.targetSkillIds, targetSkillId))
                    continue;

                const int sourceSkillId = bonus.sourceSkillId > 0 ? bonus.sourceSkillId : definition.skillId;
                const int sourceSkillLevel = GetRuntimeAppliedSkillLevel(sourceSkillId);
                if (sourceSkillLevel <= 0)
                    continue;

                int value = 0;
                if (ResolvePassiveValueForLevel(sourceSkillId, sourceSkillLevel, (bonus.*valueMember), value))
                    total += value;
            }
        }

        return total;
    }

    int ResolveConfiguredPassiveDamagePercentBonusForSkill(int targetSkillId)
    {
        return ResolveConfiguredPassiveBonusForSkill(targetSkillId, &PassiveBonusDefinition::damagePercent);
    }

    int ResolveConfiguredPassiveIgnoreDefensePercentBonusForSkill(int targetSkillId)
    {
        return ResolveConfiguredPassiveBonusForSkill(targetSkillId, &PassiveBonusDefinition::ignoreDefensePercent);
    }

    int ResolveConfiguredPassiveAttackCountBonusForSkill(int targetSkillId)
    {
        return ResolveConfiguredPassiveBonusForSkill(targetSkillId, &PassiveBonusDefinition::attackCount);
    }

    int ResolveConfiguredPassiveMobCountBonusForSkill(int targetSkillId)
    {
        return ResolveConfiguredPassiveBonusForSkill(targetSkillId, &PassiveBonusDefinition::mobCount);
    }

    int ClampPassiveEffectValue(long long value)
    {
        if (value < 0)
            return 0;
        if (value > 0x3FFFFFFFLL)
            return 0x3FFFFFFF;
        return static_cast<int>(value);
    }

    int ClampPassivePercentValue(long long value)
    {
        if (value < 0)
            return 0;
        if (value > 100)
            return 100;
        return static_cast<int>(value);
    }

    int ClampProtocolCountNibble(long long value)
    {
        if (value < 1)
            return 1;
        if (value > 15)
            return 15;
        return static_cast<int>(value);
    }

    bool TryResolvePassiveEffectBaseValueFromLocalData(
        int skillId,
        int level,
        const char* primaryField,
        const char* alternateField,
        int& outValue)
    {
        outValue = 0;
        if (skillId <= 0 || level <= 0 || !primaryField || !primaryField[0])
            return false;

        if (SkillLocalDataGetLevelValueInt(skillId, level, primaryField, outValue))
            return true;
        if (alternateField && alternateField[0] &&
            SkillLocalDataGetLevelValueInt(skillId, level, alternateField, outValue))
        {
            return true;
        }
        return false;
    }

    void LogPassiveEffectDecodedValueCandidates(uintptr_t effectPtr, int skillId, int level)
    {
        if (!effectPtr || skillId <= 0 || level <= 0)
            return;

        static std::map<uintptr_t, DWORD> s_lastCandidateLogTickByEffectPtr;
        const DWORD nowTick = GetTickCount();
        std::map<uintptr_t, DWORD>::iterator lastIt = s_lastCandidateLogTickByEffectPtr.find(effectPtr);
        if (lastIt != s_lastCandidateLogTickByEffectPtr.end() &&
            nowTick - lastIt->second <= 30000)
        {
            return;
        }
        s_lastCandidateLogTickByEffectPtr[effectPtr] = nowTick;

        DWORD* effectBase = reinterpret_cast<DWORD*>(effectPtr);
        int damageBase = 0;
        int attackBase = 0;
        int mobBase = 0;
        bool hasDamage =
            TryResolvePassiveEffectBaseValueFromLocalData(skillId, level, "damage", "damRate", damageBase) ||
            TryResolvePassiveEffectBaseValueFromLocalData(skillId, level, "damR", "damagePercent", damageBase);
        bool hasAttack =
            TryResolvePassiveEffectBaseValueFromLocalData(skillId, level, "attackCount", "hitCount", attackBase);
        bool hasMob =
            TryResolvePassiveEffectBaseValueFromLocalData(skillId, level, "mobCount", "targetCount", mobBase);

        std::string damageCandidates;
        std::string attackCandidates;
        std::string mobCandidates;
        size_t damageCount = 0;
        size_t attackCount = 0;
        size_t mobCount = 0;
        const size_t kMaxCandidatesPerValue = 16;
        const size_t kMaxKeyIndex = 276;

        for (size_t keyIndex = 0; keyIndex <= kMaxKeyIndex; ++keyIndex)
        {
            int decodedValue = 0;
            if (!ReadEncryptedTripletValueLocal(effectBase, keyIndex, &decodedValue))
                continue;

            if (hasDamage && decodedValue == damageBase)
                AppendCandidateIndexText(damageCandidates, keyIndex, decodedValue, damageCount, kMaxCandidatesPerValue);
            if (hasAttack && decodedValue == attackBase)
                AppendCandidateIndexText(attackCandidates, keyIndex, decodedValue, attackCount, kMaxCandidatesPerValue);
            if (hasMob && decodedValue == mobBase)
                AppendCandidateIndexText(mobCandidates, keyIndex, decodedValue, mobCount, kMaxCandidatesPerValue);
        }

        WriteLogFmt(
            "[SuperPassiveEffectScan] target=%d level=%d effect=0x%08X localDamage=%d damageCandidates=%s localAttack=%d attackCandidates=%s localMob=%d mobCandidates=%s",
            skillId,
            level,
            (DWORD)effectPtr,
            hasDamage ? damageBase : -1,
            damageCandidates.empty() ? "none" : damageCandidates.c_str(),
            hasAttack ? attackBase : -1,
            attackCandidates.empty() ? "none" : attackCandidates.c_str(),
            hasMob ? mobBase : -1,
            mobCandidates.empty() ? "none" : mobCandidates.c_str());
    }

    int ResolvePassiveEffectGetterOverrideValue(
        int skillId,
        int level,
        int originalValue,
        const char* getterTag,
        const char* getterSourceTag,
        int& outBaseValue,
        int& outBonusValue)
    {
        outBaseValue = originalValue;
        outBonusValue = 0;
        if (skillId <= 0 || level <= 0 || !getterTag || !getterTag[0])
            return originalValue;

        const bool useB05C40HitCountSemantics =
            getterSourceTag &&
            getterSourceTag[0] &&
            _stricmp(getterSourceTag, "7D1990") == 0;

        if (_stricmp(getterTag, "damage") == 0)
        {
            int localDamage = 0;
            if (outBaseValue <= 0 &&
                (TryResolvePassiveEffectBaseValueFromLocalData(skillId, level, "damage", "damRate", localDamage) ||
                 TryResolvePassiveEffectBaseValueFromLocalData(skillId, level, "damR", "damagePercent", localDamage)))
            {
                outBaseValue = localDamage;
            }

            const int configuredBonus = ResolveConfiguredPassiveDamagePercentBonusForSkill(skillId);
            if (configuredBonus == 0 || outBaseValue <= 0)
                return originalValue;
            outBonusValue = configuredBonus;

            const long long scaledValue =
                static_cast<long long>(outBaseValue) +
                (static_cast<long long>(outBaseValue) * outBonusValue) / 100;
            return ClampPassiveEffectValue(scaledValue);
        }

        if (_stricmp(getterTag, "mobCount") == 0)
        {
            if (useB05C40HitCountSemantics)
            {
                int localAttackCount = 0;
                if (outBaseValue <= 0 &&
                    TryResolvePassiveEffectBaseValueFromLocalData(skillId, level, "attackCount", "hitCount", localAttackCount))
                {
                    outBaseValue = localAttackCount;
                }
                if (outBaseValue <= 0)
                    outBaseValue = 1;

                outBonusValue = ResolveConfiguredPassiveAttackCountBonusForSkill(skillId);
                if (outBonusValue == 0)
                    return originalValue;

                return ClampProtocolCountNibble(static_cast<long long>(outBaseValue) + outBonusValue);
            }

            int localMobCount = 0;
            if (outBaseValue <= 0 &&
                TryResolvePassiveEffectBaseValueFromLocalData(skillId, level, "mobCount", "targetCount", localMobCount))
            {
                outBaseValue = localMobCount;
            }
            if (outBaseValue <= 0)
                outBaseValue = 1;

            outBonusValue = ResolveConfiguredPassiveMobCountBonusForSkill(skillId);
            if (outBonusValue == 0)
                return originalValue;

            return ClampProtocolCountNibble(static_cast<long long>(outBaseValue) + outBonusValue);
        }

        if (_stricmp(getterTag, "attackCount") == 0)
        {
            int localAttackCount = 0;
            if (outBaseValue <= 0 &&
                TryResolvePassiveEffectBaseValueFromLocalData(skillId, level, "attackCount", "hitCount", localAttackCount))
            {
                outBaseValue = localAttackCount;
            }
            if (outBaseValue <= 0)
                outBaseValue = 1;

            outBonusValue = ResolveConfiguredPassiveAttackCountBonusForSkill(skillId);
            if (outBonusValue == 0)
                return originalValue;

            return ClampProtocolCountNibble(static_cast<long long>(outBaseValue) + outBonusValue);
        }

        if (_stricmp(getterTag, "ignoreMobpdpR") == 0)
        {
            int localIgnore = 0;
            if (outBaseValue <= 0 &&
                TryResolvePassiveEffectBaseValueFromLocalData(skillId, level, "ignoreMobpdpR", "ignoreMob", localIgnore))
            {
                outBaseValue = localIgnore;
            }

            outBonusValue = ResolveConfiguredPassiveIgnoreDefensePercentBonusForSkill(skillId);
            if (outBonusValue == 0)
                return originalValue;

            return ClampPassivePercentValue(static_cast<long long>(outBaseValue) + outBonusValue);
        }

        return originalValue;
    }

    bool TryResolveDesiredPassiveEffectValueFromSnapshot(
        const PassiveEffectPatchSnapshot& snapshot,
        const char* getterTag,
        const char* getterSourceTag,
        int& outDesiredValue,
        int& outBaseValue,
        int& outBonusValue)
    {
        outDesiredValue = 0;
        outBaseValue = 0;
        outBonusValue = 0;
        if (!getterTag || !getterTag[0] || snapshot.skillId <= 0 || snapshot.level <= 0)
            return false;

        int originalValue = 0;
        bool hasStoredField = false;
        if (_stricmp(getterTag, "damage") == 0)
        {
            const bool preferAltDamageField =
                getterSourceTag &&
                getterSourceTag[0] &&
                _stricmp(getterSourceTag, "43DE50") == 0;
            if (preferAltDamageField)
            {
                if (snapshot.hasDamageAlt && snapshot.originalDamageAlt > 0)
                {
                    hasStoredField = true;
                    originalValue = snapshot.originalDamageAlt;
                }
                else if (snapshot.hasDamage && snapshot.originalDamage > 0)
                {
                    hasStoredField = true;
                    originalValue = snapshot.originalDamage;
                }
                else if (snapshot.hasDamageLocal && snapshot.originalDamageLocal > 0)
                {
                    hasStoredField = true;
                    originalValue = snapshot.originalDamageLocal;
                }
                else if (snapshot.hasDamageAlt)
                {
                    hasStoredField = true;
                    originalValue = snapshot.originalDamageAlt;
                }
                else if (snapshot.hasDamage)
                {
                    hasStoredField = true;
                    originalValue = snapshot.originalDamage;
                }
                else
                {
                    hasStoredField = snapshot.hasDamageLocal;
                    originalValue = snapshot.originalDamageLocal;
                }
            }
            else if (snapshot.hasDamage && snapshot.originalDamage > 0)
            {
                hasStoredField = true;
                originalValue = snapshot.originalDamage;
            }
            else if (snapshot.hasDamageAlt && snapshot.originalDamageAlt > 0)
            {
                hasStoredField = true;
                originalValue = snapshot.originalDamageAlt;
            }
            else if (snapshot.hasDamageLocal && snapshot.originalDamageLocal > 0)
            {
                hasStoredField = true;
                originalValue = snapshot.originalDamageLocal;
            }
            else if (snapshot.hasDamage)
            {
                hasStoredField = true;
                originalValue = snapshot.originalDamage;
            }
            else if (snapshot.hasDamageAlt)
            {
                hasStoredField = true;
                originalValue = snapshot.originalDamageAlt;
            }
            else
            {
                hasStoredField = snapshot.hasDamageLocal;
                originalValue = snapshot.originalDamageLocal;
            }
        }
        else if (_stricmp(getterTag, "mobCount") == 0)
        {
            hasStoredField = snapshot.hasMobCount;
            originalValue = snapshot.originalMobCount;
        }
        else if (_stricmp(getterTag, "attackCount") == 0)
        {
            if (snapshot.hasAttackCountAlt && snapshot.originalAttackCountAlt > 0)
            {
                hasStoredField = true;
                originalValue = snapshot.originalAttackCountAlt;
            }
            else if (snapshot.hasAttackCount && snapshot.originalAttackCount > 0)
            {
                hasStoredField = true;
                originalValue = snapshot.originalAttackCount;
            }
            else if (snapshot.hasAttackCountAlt)
            {
                hasStoredField = true;
                originalValue = snapshot.originalAttackCountAlt;
            }
            else
            {
                hasStoredField = snapshot.hasAttackCount;
                originalValue = snapshot.originalAttackCount;
            }
        }
        else if (_stricmp(getterTag, "ignoreMobpdpR") == 0)
        {
            hasStoredField = snapshot.hasIgnoreMobpdpR;
            originalValue = snapshot.originalIgnoreMobpdpR;
        }
        else
        {
            return false;
        }

        if (!hasStoredField)
            return false;

        outDesiredValue = ResolvePassiveEffectGetterOverrideValue(
            snapshot.skillId,
            snapshot.level,
            originalValue,
            getterTag,
            getterSourceTag,
            outBaseValue,
            outBonusValue);
        return outBonusValue != 0;
    }

    void RememberPassiveEffectDamageWrite(int skillId)
    {
        if (skillId <= 0)
            return;
        g_passiveEffectDamageWriteTickBySkillId[skillId] = GetTickCount();
    }

    bool HasRecentPassiveEffectDamageWrite(int skillId)
    {
        if (skillId <= 0)
            return false;
        std::map<int, DWORD>::const_iterator it =
            g_passiveEffectDamageWriteTickBySkillId.find(skillId);
        if (it == g_passiveEffectDamageWriteTickBySkillId.end())
            return false;
        return GetTickCount() - it->second <= kPassiveEffectDamageWriteSuppressPacketRewriteMs;
    }

    void RememberPassiveEffectDamageGetterUse(int skillId)
    {
        if (skillId <= 0)
            return;
        g_passiveEffectDamageGetterTickBySkillId[skillId] = GetTickCount();
    }

    bool HasRecentPassiveEffectDamageGetterUse(int skillId)
    {
        if (skillId <= 0)
            return false;
        std::map<int, DWORD>::const_iterator it =
            g_passiveEffectDamageGetterTickBySkillId.find(skillId);
        if (it == g_passiveEffectDamageGetterTickBySkillId.end())
            return false;
        return GetTickCount() - it->second <= kPassiveEffectDamageWriteSuppressPacketRewriteMs;
    }

    void RememberPassiveEffectAttackCountGetterUse(int skillId)
    {
        if (skillId <= 0)
            return;
        g_passiveEffectAttackCountGetterTickBySkillId[skillId] = GetTickCount();
    }

    bool HasRecentPassiveEffectAttackCountGetterUse(int skillId)
    {
        if (skillId <= 0)
            return false;
        std::map<int, DWORD>::const_iterator it =
            g_passiveEffectAttackCountGetterTickBySkillId.find(skillId);
        if (it == g_passiveEffectAttackCountGetterTickBySkillId.end())
            return false;
        return GetTickCount() - it->second <= kPassiveEffectDamageWriteSuppressPacketRewriteMs;
    }

    void RememberPassiveEffectRuntimeContext(int skillId, int level, uintptr_t effectPtr)
    {
        if (skillId <= 0 || level <= 0 || !effectPtr)
            return;

        if (g_passiveEffectRuntimeContextsByEffectPtr.size() > 4096)
        {
            const DWORD nowTick = GetTickCount();
            for (std::map<uintptr_t, PassiveEffectRuntimeContext>::iterator it =
                     g_passiveEffectRuntimeContextsByEffectPtr.begin();
                 it != g_passiveEffectRuntimeContextsByEffectPtr.end(); )
            {
                if (nowTick - it->second.lastSeenTick > 300000)
                    it = g_passiveEffectRuntimeContextsByEffectPtr.erase(it);
                else
                    ++it;
            }
        }

        PassiveEffectRuntimeContext& context = g_passiveEffectRuntimeContextsByEffectPtr[effectPtr];
        context.skillId = skillId;
        context.level = level;
        context.lastSeenTick = GetTickCount();
    }

    void LogConfiguredPassiveSemanticBonusesForSkill(
        int targetSkillId,
        CustomSkillPacketRoute packetRoute,
        unsigned short opcode,
        uintptr_t callerRetAddr)
    {
        if (targetSkillId <= 0)
            return;

        static std::map<int, DWORD> s_lastLogTickByTargetSkillId;
        const DWORD now = GetTickCount();
        std::map<int, DWORD>::iterator lastIt = s_lastLogTickByTargetSkillId.find(targetSkillId);
        if (lastIt != s_lastLogTickByTargetSkillId.end() && now - lastIt->second < 1000)
            return;
        s_lastLogTickByTargetSkillId[targetSkillId] = now;

        const int damagePercent = ResolveConfiguredPassiveDamagePercentBonusForSkill(targetSkillId);
        const int ignoreDefensePercent = ResolveConfiguredPassiveIgnoreDefensePercentBonusForSkill(targetSkillId);
        const int attackCount = ResolveConfiguredPassiveAttackCountBonusForSkill(targetSkillId);
        const int mobCount = ResolveConfiguredPassiveMobCountBonusForSkill(targetSkillId);
        if (damagePercent == 0 && ignoreDefensePercent == 0 && attackCount == 0 && mobCount == 0)
            return;

        WriteLogFmt("[SuperPassive] semantic bonuses target=%d damage=%d ignoreMobpdpR=%d attackCount=%d mobCount=%d opcode=0x%X route=%s caller=0x%08X",
            targetSkillId,
            damagePercent,
            ignoreDefensePercent,
            attackCount,
            mobCount,
            (unsigned int)opcode,
            PacketRouteToString(packetRoute),
            (DWORD)(uintptr_t)callerRetAddr);
    }

    int ResolveActiveIndependentBuffBonusTotal(const char* bonusKey)
    {
        RefreshIndependentBuffRuntimeOwnerBinding();

        if (!bonusKey || !*bonusKey || g_activeIndependentBuffRewriteStates.empty())
            return 0;

        int total = 0;
        std::map<int, bool> visitedSkillIds;
        for (std::map<unsigned long long, ActiveIndependentBuffRewriteState>::const_iterator it = g_activeIndependentBuffRewriteStates.begin();
             it != g_activeIndependentBuffRewriteStates.end();
             ++it)
        {
            const ActiveIndependentBuffRewriteState& state = it->second;
            if (!state.active || state.skillId <= 0)
                continue;
            if (visitedSkillIds.find(state.skillId) != visitedSkillIds.end())
                continue;
            visitedSkillIds[state.skillId] = true;

            SuperSkillDefinition definition = {};
            if (!FindSuperSkillDefinition(state.skillId, definition))
                continue;

            std::map<std::string, PassiveValueSpec>::const_iterator bonusIt = definition.clientBonusSpecs.find(bonusKey);
            if (bonusIt == definition.clientBonusSpecs.end())
                continue;

            const int sourceSkillId = definition.independentSourceSkillId > 0 ? definition.independentSourceSkillId : definition.skillId;
            int sourceSkillLevel = GetRuntimeAppliedSkillLevel(definition.skillId);
            if (sourceSkillLevel <= 0 && sourceSkillId != definition.skillId)
                sourceSkillLevel = GetRuntimeAppliedSkillLevel(sourceSkillId);
            if (sourceSkillId <= 0 || sourceSkillLevel <= 0)
                continue;

            int value = 0;
            if (ResolvePassiveValueForLevel(sourceSkillId, sourceSkillLevel, bonusIt->second, value))
                total += value;
        }

        return total;
    }

    bool PacketCanRead(int cursor, int byteCount, int packetLen)
    {
        return cursor >= 0 && byteCount >= 0 && cursor <= packetLen && byteCount <= packetLen - cursor;
    }

    bool PacketSkip(int& cursor, int byteCount, int packetLen)
    {
        if (!PacketCanRead(cursor, byteCount, packetLen))
            return false;
        cursor += byteCount;
        return true;
    }

    bool PacketReadByte(const BYTE* packet, int& cursor, int packetLen, BYTE& outValue)
    {
        if (!packet || !PacketCanRead(cursor, 1, packetLen))
            return false;
        outValue = packet[cursor++];
        return true;
    }

    bool TryLogIncomingCloseRangeAttackPacket(int opcode, BYTE* payload, int payloadLen, uintptr_t callerRetAddr)
    {
        if (!payload || payloadLen < 20)
            return false;
        if (opcode != (int)kServerCloseRangeAttackPacketOpcode &&
            opcode != (int)kServerEnergyAttackPacketOpcode)
        {
            return false;
        }

        const int cid = ReadPacketInt(payload, 0);
        const BYTE tbyte = payload[4];
        const int targetCount = (tbyte >> 4) & 0x0F;
        const int hitCount = tbyte & 0x0F;
        const int level = payload[6];
        const int skillId = ReadPacketInt(payload, 7);
        if (skillId <= 0)
            return false;

        const int damageBonus = ResolveConfiguredPassiveDamagePercentBonusForSkill(skillId);
        const int attackBonus = ResolveConfiguredPassiveAttackCountBonusForSkill(skillId);
        const int mobBonus = ResolveConfiguredPassiveMobCountBonusForSkill(skillId);
        const int ignoreBonus = ResolveConfiguredPassiveIgnoreDefensePercentBonusForSkill(skillId);
        if (damageBonus == 0 && attackBonus == 0 && mobBonus == 0 && ignoreBonus == 0)
            return false;

        int cursor = 20;
        int firstOid = 0;
        std::string sample;
        int parsedTargets = 0;
        for (int targetIndex = 0; targetIndex < targetCount; ++targetIndex)
        {
            if (!PacketCanRead(cursor, 5, payloadLen))
                break;

            const int oid = ReadPacketInt(payload, cursor);
            cursor += 4;
            cursor += 1; // hit action marker (0x07 for close-range attack broadcast)

            if (!PacketCanRead(cursor, hitCount * 4, payloadLen))
                break;

            if (parsedTargets == 0)
            {
                firstOid = oid;
                for (int hitIndex = 0; hitIndex < hitCount; ++hitIndex)
                {
                    if (hitIndex > 0)
                        sample += ",";
                    char buf[32] = {};
                    std::snprintf(buf, sizeof(buf), "%d", ReadPacketInt(payload, cursor + hitIndex * 4));
                    sample += buf;
                    if (hitIndex >= 5 && hitCount > 6)
                    {
                        sample += "...";
                        break;
                    }
                }
            }

            cursor += hitCount * 4;
            parsedTargets++;
        }

        WriteLogFmt("[SuperPassiveRecv] opcode=0x%X cid=%d skillId=%d level=%d targets=%d hits=%d parsedTargets=%d firstOid=%d sample=%s len=%d caller=0x%08X",
            (unsigned int)opcode,
            cid,
            skillId,
            level,
            targetCount,
            hitCount,
            parsedTargets,
            firstOid,
            sample.empty() ? "none" : sample.c_str(),
            payloadLen,
            (DWORD)(uintptr_t)callerRetAddr);
        return true;
    }

    void TryLogIncomingPassiveAttackProbe(int opcode, BYTE* payload, int payloadLen, uintptr_t callerRetAddr)
    {
        int recentSkillId = 0;
        if (!TryGetRecentPassiveAttackProbeSkillId(recentSkillId))
            return;
        if (!payload || payloadLen <= 0)
            return;
        if (opcode < 0x100 || opcode > 0x120)
            return;

        static DWORD s_lastProbeLogTick = 0;
        static int s_lastProbeOpcode = 0;
        static int s_lastProbeSkillId = 0;
        const DWORD nowTick = GetTickCount();
        if (s_lastProbeOpcode == opcode &&
            s_lastProbeSkillId == recentSkillId &&
            nowTick - s_lastProbeLogTick <= 250)
        {
            return;
        }
        s_lastProbeLogTick = nowTick;
        s_lastProbeOpcode = opcode;
        s_lastProbeSkillId = recentSkillId;

        const BYTE byte4 = payloadLen > 4 ? payload[4] : 0;
        const int skillAt7 = payloadLen >= 11 ? ReadPacketInt(payload, 7) : 0;
        const int skillAt4 = payloadLen >= 8 ? ReadPacketInt(payload, 4) : 0;
        const int skillAt6 = payloadLen >= 10 ? ReadPacketInt(payload, 6) : 0;
        WriteLogFmt(
            "[SuperPassiveRecvProbe] recentSkill=%d opcode=0x%X len=%d b4=0x%02X skill@4=%d skill@6=%d skill@7=%d caller=0x%08X",
            recentSkillId,
            (unsigned int)opcode,
            payloadLen,
            (unsigned int)byte4,
            skillAt4,
            skillAt6,
            skillAt7,
            (DWORD)(uintptr_t)callerRetAddr);
    }

    struct AttackTargetPacketBlock
    {
        int blockStart;
        BYTE headerByte;
        int damageStart;
        int damageEnd;
        int blockEnd;
        std::vector<int> damageOffsets;
    };

    struct AttackPacketLayout
    {
        int tbyteOffset;
        int skillId;
        int targetCount;
        int hitCount;
        int tailOffset;
        int tailBytes;
        std::vector<AttackTargetPacketBlock> targets;
    };

    void CollectDamageOffsetsFromAttackLayout(
        const AttackPacketLayout& layout,
        std::vector<int>& outOffsets)
    {
        outOffsets.clear();

        size_t totalDamageOffsets = 0;
        for (size_t i = 0; i < layout.targets.size(); ++i)
            totalDamageOffsets += layout.targets[i].damageOffsets.size();

        outOffsets.reserve(totalDamageOffsets);
        for (size_t i = 0; i < layout.targets.size(); ++i)
        {
            const AttackTargetPacketBlock& block = layout.targets[i];
            outOffsets.insert(outOffsets.end(), block.damageOffsets.begin(), block.damageOffsets.end());
        }
    }

    int CountAttackTargetHeaderByteMatches(const AttackPacketLayout& layout, BYTE expectedValue)
    {
        int matched = 0;
        for (size_t i = 0; i < layout.targets.size(); ++i)
        {
            if (layout.targets[i].headerByte == expectedValue)
                ++matched;
        }
        return matched;
    }

    bool IsCloseAttackMoveSkipOnlySkill(int skillId)
    {
        switch (skillId)
        {
        case 2111007:
        case 2211007:
        case 2311007:
        case 12111007:
        case 22161005:
        case 32111010:
            return true;
        default:
            return false;
        }
    }

    bool IsCloseAttackChargeSkill(int skillId)
    {
        switch (skillId)
        {
        case 5101004:
        case 15101003:
        case 5201002:
        case 14111006:
        case 4341002:
        case 4341003:
        case 5301001:
        case 5300007:
        case 31001000:
        case 31101000:
        case 31111005:
            return true;
        default:
            return false;
        }
    }

    bool IsRangedAttackExtra4Skill(int skillId)
    {
        switch (skillId)
        {
        case 3121004:
        case 3221001:
        case 5221004:
        case 13111002:
        case 33121009:
        case 35001001:
        case 35101009:
        case 23121000:
        case 5311002:
            return true;
        default:
            return false;
        }
    }

    bool IsMagicChargeSkillClientKnown(int skillId)
    {
        switch (skillId)
        {
        case 2121001:
        case 2221001:
        case 2321001:
        case 22121000:
        case 22151001:
            return true;
        default:
            return false;
        }
    }

    bool SkipAttackMovementBlock(const BYTE* packet, int packetLen, int& cursor)
    {
        BYTE commandCount = 0;
        if (!PacketReadByte(packet, cursor, packetLen, commandCount))
            return false;

        for (int i = 0; i < (int)commandCount; ++i)
        {
            BYTE command = 0;
            if (!PacketReadByte(packet, cursor, packetLen, command))
                return false;

            int skip = -1;
            switch (command)
            {
            case 0x0E:
                skip = 19;
                break;
            case 0x00:
            case 0x07:
            case 0x10:
            case 0x2D:
            case 0x2E:
                skip = 17;
                break;
            case 0x2C:
                skip = 13;
                break;
            case 0x01:
            case 0x02:
            case 0x0F:
            case 0x15:
            case 0x28:
            case 0x29:
            case 0x2A:
            case 0x2B:
                skip = 7;
                break;
            case 0x12:
            case 0x13:
                skip = 9;
                break;
            case 0x11:
            case 0x17:
            case 0x18:
            case 0x19:
            case 0x1A:
            case 0x1B:
            case 0x1C:
            case 0x1D:
            case 0x1E:
            case 0x1F:
            case 0x20:
            case 0x21:
            case 0x22:
            case 0x23:
            case 0x24:
            case 0x25:
            case 0x26:
            case 0x27:
                skip = 3;
                break;
            case 0x03:
            case 0x04:
            case 0x05:
            case 0x06:
            case 0x08:
            case 0x09:
            case 0x0A:
            case 0x0C:
                skip = 9;
                break;
            case 0x14:
                skip = 11;
                break;
            case 0x0D:
                skip = 9;
                break;
            case 0x0B:
                skip = 1;
                break;
            default:
                return false;
            }

            if (!PacketSkip(cursor, skip, packetLen))
                return false;
        }

        BYTE encodedExtra = 0;
        if (!PacketReadByte(packet, cursor, packetLen, encodedExtra))
            return false;

        const int extraBytes = ((int)encodedExtra + 1) / 2;
        return PacketSkip(cursor, extraBytes, packetLen);
    }

    bool SkipAttackHeaderAfterSkill(
        const BYTE* packet,
        int packetLen,
        CustomSkillPacketRoute packetRoute,
        int skillId,
        int cygnusBlessSkip,
        int& cursor)
    {
        BYTE moveFlag = 0;
        switch (packetRoute)
        {
        case CustomSkillPacketRoute_CloseRange:
            if (IsCloseAttackMoveSkipOnlySkill(skillId))
            {
                if (!PacketSkip(cursor, 1, packetLen))
                    return false;
            }
            else
            {
                if (!PacketReadByte(packet, cursor, packetLen, moveFlag))
                    return false;
                if (!PacketSkip(cursor, 1, packetLen))
                    return false;
                if (moveFlag != 0)
                {
                    if (!PacketSkip(cursor, 8, packetLen))
                        return false;
                    if (!PacketSkip(cursor, 4, packetLen))
                        return false;
                    if (!SkipAttackMovementBlock(packet, packetLen, cursor))
                        return false;
                    if (!PacketSkip(cursor, 9, packetLen))
                        return false;
                }
            }

            if (IsCloseAttackChargeSkill(skillId) && !PacketSkip(cursor, 4, packetLen))
                return false;

            if (!PacketSkip(cursor, 4, packetLen))
                return false;
            if (!PacketSkip(cursor, 1, packetLen))
                return false;
            if (!PacketSkip(cursor, 2, packetLen))
                return false;
            if (!PacketSkip(cursor, 4, packetLen))
                return false;
            if (!PacketSkip(cursor, 1, packetLen))
                return false;
            if (cygnusBlessSkip > 0 && !PacketSkip(cursor, cygnusBlessSkip, packetLen))
                return false;
            if (!PacketSkip(cursor, 1, packetLen))
                return false;
            if (!PacketSkip(cursor, 4, packetLen))
                return false;
            return PacketSkip(cursor, 4, packetLen);

        case CustomSkillPacketRoute_RangedAttack:
            if (!PacketReadByte(packet, cursor, packetLen, moveFlag))
                return false;
            if (!PacketSkip(cursor, 1, packetLen))
                return false;
            if (moveFlag != 0)
            {
                if (!PacketSkip(cursor, 8, packetLen))
                    return false;
                if (!PacketSkip(cursor, 4, packetLen))
                    return false;
                if (!SkipAttackMovementBlock(packet, packetLen, cursor))
                    return false;
                if (!PacketSkip(cursor, 9, packetLen))
                    return false;
            }
            if (IsRangedAttackExtra4Skill(skillId) && !PacketSkip(cursor, 4, packetLen))
                return false;
            if (!PacketSkip(cursor, 4, packetLen))
                return false;
            if (!PacketSkip(cursor, 1, packetLen))
                return false;
            if (!PacketSkip(cursor, 1, packetLen))
                return false;
            if (!PacketSkip(cursor, 2, packetLen))
                return false;
            if (!PacketSkip(cursor, 4, packetLen))
                return false;
            if (!PacketSkip(cursor, 1, packetLen))
                return false;
            if (cygnusBlessSkip > 0 && !PacketSkip(cursor, cygnusBlessSkip, packetLen))
                return false;
            if (!PacketSkip(cursor, 1, packetLen))
                return false;
            if (!PacketSkip(cursor, 4, packetLen))
                return false;
            if (!PacketSkip(cursor, 4, packetLen))
                return false;
            if (!PacketSkip(cursor, 2, packetLen))
                return false;
            if (!PacketSkip(cursor, 2, packetLen))
                return false;
            return PacketSkip(cursor, 1, packetLen);

        case CustomSkillPacketRoute_MagicAttack:
            if (!PacketReadByte(packet, cursor, packetLen, moveFlag))
                return false;
            if (!PacketSkip(cursor, 1, packetLen))
                return false;
            if (moveFlag != 0)
            {
                if (!PacketSkip(cursor, 8, packetLen))
                    return false;
                if (!PacketSkip(cursor, 4, packetLen))
                    return false;
                if (!SkipAttackMovementBlock(packet, packetLen, cursor))
                    return false;
                if (!PacketSkip(cursor, 9, packetLen))
                    return false;
            }
            if (IsMagicChargeSkillClientKnown(skillId) && !PacketSkip(cursor, 4, packetLen))
                return false;
            if (!PacketSkip(cursor, 4, packetLen))
                return false;
            if (!PacketSkip(cursor, 1, packetLen))
                return false;
            if (!PacketSkip(cursor, 2, packetLen))
                return false;
            if (!PacketSkip(cursor, 4, packetLen))
                return false;
            if (!PacketSkip(cursor, 1, packetLen))
                return false;
            if (cygnusBlessSkip > 0 && !PacketSkip(cursor, cygnusBlessSkip, packetLen))
                return false;
            if (!PacketSkip(cursor, 1, packetLen))
                return false;
            if (!PacketSkip(cursor, 4, packetLen))
                return false;
            return PacketSkip(cursor, 4, packetLen);

        default:
            return false;
        }
    }

    bool IsAttackTailPlausible(
        const BYTE* packet,
        int packetLen,
        CustomSkillPacketRoute packetRoute,
        int skillId,
        int cursor)
    {
        if (!packet || cursor < 0 || cursor > packetLen)
            return false;

        int tailCursor = cursor;
        if (packetRoute == CustomSkillPacketRoute_RangedAttack)
        {
            if (!PacketSkip(tailCursor, 4, packetLen))
                return false;
        }

        if (!PacketSkip(tailCursor, 4, packetLen))
            return false;

        if (packetRoute == CustomSkillPacketRoute_MagicAttack && skillId == 2301002)
        {
            if (!PacketSkip(tailCursor, 4, packetLen))
                return false;
        }

        const int remaining = packetLen - tailCursor;
        if (remaining == 0 || remaining == 4)
            return true;

        if (remaining > 0)
        {
            const int amount = (int)packet[tailCursor];
            return remaining == 1 + amount * 2;
        }

        return false;
    }

    bool IsPlausibleAttackTargetOid(int oid)
    {
        return oid >= 100000 && oid <= 0x00FFFFFF;
    }

    bool TryBuildAttackPacketLayoutFromCursor(
        const BYTE* packet,
        int packetLen,
        CustomSkillPacketRoute packetRoute,
        int skillId,
        int tbyteOffset,
        int targetCount,
        int hitCount,
        int cursor,
        AttackPacketLayout& outLayout)
    {
        outLayout = AttackPacketLayout();
        if (!packet || targetCount <= 0 || hitCount <= 0)
            return false;

        outLayout.tbyteOffset = tbyteOffset;
        outLayout.skillId = skillId;
        outLayout.targetCount = targetCount;
        outLayout.hitCount = hitCount;
        outLayout.targets.reserve((size_t)targetCount);

        for (int targetIndex = 0; targetIndex < targetCount; ++targetIndex)
        {
            AttackTargetPacketBlock block = {};
            block.blockStart = cursor;
            if (!PacketCanRead(cursor, 4, packetLen))
                return false;

            const int oid = ReadPacketInt(packet, cursor);
            if (!IsPlausibleAttackTargetOid(oid))
                return false;

            cursor += 4;
            if (!PacketCanRead(cursor, 14, packetLen))
                return false;
            block.headerByte = packet[cursor];
            cursor += 14;

            block.damageStart = cursor;
            block.damageOffsets.reserve((size_t)hitCount);
            for (int hitIndex = 0; hitIndex < hitCount; ++hitIndex)
            {
                if (!PacketCanRead(cursor, 4, packetLen))
                    return false;
                block.damageOffsets.push_back(cursor);
                cursor += 4;
            }
            block.damageEnd = cursor;

            if (!PacketSkip(cursor, 4, packetLen))
                return false;
            if (!PacketSkip(cursor, 4, packetLen))
                return false;

            block.blockEnd = cursor;
            outLayout.targets.push_back(block);
        }

        if (!IsAttackTailPlausible(packet, packetLen, packetRoute, skillId, cursor))
            return false;

        outLayout.tailOffset = cursor;
        outLayout.tailBytes = packetLen - cursor;
        return true;
    }

    bool TryScanCloseRangeAttackPacketLayout(
        const BYTE* packet,
        int packetLen,
        int skillIdOffset,
        AttackPacketLayout& outLayout)
    {
        outLayout = AttackPacketLayout();
        if (!packet || packetLen < skillIdOffset + 4 || skillIdOffset != 4)
            return false;

        const int tbyteOffset = skillIdOffset - 1;
        if (!PacketCanRead(tbyteOffset, 1, packetLen))
            return false;

        const BYTE targetHitByte = packet[tbyteOffset];
        const int targetCount = ((int)targetHitByte >> 4) & 0x0F;
        const int hitCount = (int)targetHitByte & 0x0F;
        if (targetCount <= 0 || hitCount <= 0 || targetCount > 15 || hitCount > 15)
            return false;

        const int skillId = ReadPacketInt(packet, skillIdOffset);
        const int perTargetBytes = 26 + hitCount * 4;
        const int minTailBytes = 4;
        const int minCursor = skillIdOffset + 26;
        const int maxCursor = packetLen - (targetCount * perTargetBytes) - minTailBytes;
        if (maxCursor < minCursor)
            return false;

        struct CloseRangeLayoutCandidate
        {
            AttackPacketLayout layout;
            int cursor;
            int headerByte6Count;
        };

        AttackPacketLayout candidateLayout;
        std::vector<CloseRangeLayoutCandidate> candidates;
        for (int candidateCursor = minCursor; candidateCursor <= maxCursor; ++candidateCursor)
        {
            if (!TryBuildAttackPacketLayoutFromCursor(
                    packet,
                    packetLen,
                    CustomSkillPacketRoute_CloseRange,
                    skillId,
                    tbyteOffset,
                    targetCount,
                    hitCount,
                    candidateCursor,
                    candidateLayout))
            {
                continue;
            }

            CloseRangeLayoutCandidate candidate = {};
            candidate.layout = candidateLayout;
            candidate.cursor = candidateCursor;
            candidate.headerByte6Count = CountAttackTargetHeaderByteMatches(candidateLayout, 6);
            candidates.push_back(candidate);
        }

        if (candidates.size() == 1)
        {
            outLayout = candidates[0].layout;
            WriteLogFmt("[SuperPassive] close_range cursor scan skillId=%d targets=%d hits=%d cursor=%d len=%d",
                skillId,
                targetCount,
                hitCount,
                candidates[0].cursor,
                packetLen);
            return true;
        }

        int bestHeaderByte6Count = -1;
        int bestCandidateIndex = -1;
        bool bestTied = false;
        for (size_t i = 0; i < candidates.size(); ++i)
        {
            if (candidates[i].headerByte6Count > bestHeaderByte6Count)
            {
                bestHeaderByte6Count = candidates[i].headerByte6Count;
                bestCandidateIndex = (int)i;
                bestTied = false;
            }
            else if (candidates[i].headerByte6Count == bestHeaderByte6Count)
            {
                bestTied = true;
            }
        }

        if (!bestTied &&
            bestCandidateIndex >= 0 &&
            bestHeaderByte6Count > 0)
        {
            outLayout = candidates[(size_t)bestCandidateIndex].layout;
            WriteLogFmt("[SuperPassive] close_range cursor scan resolved skillId=%d targets=%d hits=%d cursor=%d header6=%d/%d len=%d",
                skillId,
                targetCount,
                hitCount,
                candidates[(size_t)bestCandidateIndex].cursor,
                bestHeaderByte6Count,
                targetCount,
                packetLen);
            return true;
        }

        if (!candidates.empty())
        {
            const int cursor0 = candidates.size() > 0 ? candidates[0].cursor : -1;
            const int cursor1 = candidates.size() > 1 ? candidates[1].cursor : -1;
            const int header60 = candidates.size() > 0 ? candidates[0].headerByte6Count : -1;
            const int header61 = candidates.size() > 1 ? candidates[1].headerByte6Count : -1;
            WriteLogFmt("[SuperPassive] close_range cursor scan ambiguous skillId=%d targets=%d hits=%d candidates=%d c0=%d h60=%d c1=%d h61=%d len=%d",
                skillId,
                targetCount,
                hitCount,
                (int)candidates.size(),
                cursor0,
                header60,
                cursor1,
                header61,
                packetLen);
        }

        return false;
    }

    bool TryCollectAttackDamageOffsetsWithBlessSkip(
        const BYTE* packet,
        int packetLen,
        CustomSkillPacketRoute packetRoute,
        int skillIdOffset,
        int cygnusBlessSkip,
        std::vector<int>& outOffsets,
        int& outTailBytes)
    {
        outOffsets.clear();
        outTailBytes = -1;
        if (!packet || packetLen < skillIdOffset + 4 || skillIdOffset != 4)
            return false;

        const int tbyteOffset = skillIdOffset - 1;
        if (!PacketCanRead(tbyteOffset, 1, packetLen))
            return false;

        const BYTE targetHitByte = packet[tbyteOffset];
        const int targetCount = ((int)targetHitByte >> 4) & 0x0F;
        const int hitCount = (int)targetHitByte & 0x0F;
        if (targetCount <= 0 || hitCount <= 0 || targetCount > 15 || hitCount > 15)
            return false;

        const int skillId = ReadPacketInt(packet, skillIdOffset);
        int cursor = skillIdOffset + 4;
        if (!SkipAttackHeaderAfterSkill(packet, packetLen, packetRoute, skillId, cygnusBlessSkip, cursor))
            return false;

        AttackPacketLayout layout;
        if (!TryBuildAttackPacketLayoutFromCursor(
                packet,
                packetLen,
                packetRoute,
                skillId,
                tbyteOffset,
                targetCount,
                hitCount,
                cursor,
                layout))
        {
            return false;
        }

        CollectDamageOffsetsFromAttackLayout(layout, outOffsets);
        outTailBytes = layout.tailBytes;
        return true;
    }

    bool TryCollectAttackPacketLayoutWithBlessSkip(
        const BYTE* packet,
        int packetLen,
        CustomSkillPacketRoute packetRoute,
        int skillIdOffset,
        int cygnusBlessSkip,
        AttackPacketLayout& outLayout)
    {
        outLayout = AttackPacketLayout();
        if (!packet || packetLen < skillIdOffset + 4 || skillIdOffset != 4)
            return false;

        const int tbyteOffset = skillIdOffset - 1;
        if (!PacketCanRead(tbyteOffset, 1, packetLen))
            return false;

        const BYTE targetHitByte = packet[tbyteOffset];
        const int targetCount = ((int)targetHitByte >> 4) & 0x0F;
        const int hitCount = (int)targetHitByte & 0x0F;
        if (targetCount <= 0 || hitCount <= 0 || targetCount > 15 || hitCount > 15)
            return false;

        const int skillId = ReadPacketInt(packet, skillIdOffset);
        int cursor = skillIdOffset + 4;
        if (!SkipAttackHeaderAfterSkill(packet, packetLen, packetRoute, skillId, cygnusBlessSkip, cursor))
            return false;

        return TryBuildAttackPacketLayoutFromCursor(
            packet,
            packetLen,
            packetRoute,
            skillId,
            tbyteOffset,
            targetCount,
            hitCount,
            cursor,
            outLayout);
    }

    bool TryCollectAttackPacketLayout(
        const BYTE* packet,
        int packetLen,
        CustomSkillPacketRoute packetRoute,
        int skillIdOffset,
        AttackPacketLayout& outLayout)
    {
        outLayout = AttackPacketLayout();
        if (packetRoute != CustomSkillPacketRoute_CloseRange &&
            packetRoute != CustomSkillPacketRoute_RangedAttack &&
            packetRoute != CustomSkillPacketRoute_MagicAttack)
        {
            return false;
        }

        AttackPacketLayout layoutNoBless;
        AttackPacketLayout layoutBless;
        const bool okNoBless = TryCollectAttackPacketLayoutWithBlessSkip(
            packet, packetLen, packetRoute, skillIdOffset, 0, layoutNoBless);
        const bool okBless = TryCollectAttackPacketLayoutWithBlessSkip(
            packet, packetLen, packetRoute, skillIdOffset, 12, layoutBless);

        if (okNoBless && !okBless)
        {
            outLayout = layoutNoBless;
            return true;
        }

        if (!okNoBless && okBless)
        {
            outLayout = layoutBless;
            return true;
        }

        if (okNoBless && okBless &&
            layoutNoBless.tailOffset == layoutBless.tailOffset &&
            layoutNoBless.targets.size() == layoutBless.targets.size())
        {
            outLayout = layoutNoBless;
            return true;
        }

        if (packetRoute == CustomSkillPacketRoute_CloseRange &&
            TryScanCloseRangeAttackPacketLayout(packet, packetLen, skillIdOffset, outLayout))
        {
            return true;
        }

        return false;
    }

    bool TryCollectAttackDamageOffsets(
        const BYTE* packet,
        int packetLen,
        CustomSkillPacketRoute packetRoute,
        int skillIdOffset,
        std::vector<int>& outOffsets)
    {
        outOffsets.clear();
        if (packetRoute != CustomSkillPacketRoute_CloseRange &&
            packetRoute != CustomSkillPacketRoute_RangedAttack &&
            packetRoute != CustomSkillPacketRoute_MagicAttack)
        {
            return false;
        }

        std::vector<int> offsetsNoBless;
        std::vector<int> offsetsBless;
        int tailNoBless = -1;
        int tailBless = -1;
        const bool okNoBless = TryCollectAttackDamageOffsetsWithBlessSkip(
            packet, packetLen, packetRoute, skillIdOffset, 0, offsetsNoBless, tailNoBless);
        const bool okBless = TryCollectAttackDamageOffsetsWithBlessSkip(
            packet, packetLen, packetRoute, skillIdOffset, 12, offsetsBless, tailBless);

        if (okNoBless && !okBless)
        {
            outOffsets.swap(offsetsNoBless);
            return true;
        }

        if (!okNoBless && okBless)
        {
            outOffsets.swap(offsetsBless);
            return true;
        }

        if (okNoBless && okBless && offsetsNoBless == offsetsBless)
        {
            outOffsets.swap(offsetsNoBless);
            return true;
        }

        if (packetRoute == CustomSkillPacketRoute_CloseRange)
        {
            AttackPacketLayout fallbackLayout;
            if (TryScanCloseRangeAttackPacketLayout(packet, packetLen, skillIdOffset, fallbackLayout))
            {
                CollectDamageOffsetsFromAttackLayout(fallbackLayout, outOffsets);
                return !outOffsets.empty();
            }
        }

        return false;
    }

    int ScaleDamageValueByPercent(int damage, int bonusPercent)
    {
        if (damage <= 0 || bonusPercent == 0)
            return damage;

        __int64 scaled = ((__int64)damage * (100 + bonusPercent) + 50) / 100;
        if (scaled < 1)
            scaled = 1;
        if (scaled > 0x7FFFFFFFLL)
            scaled = 0x7FFFFFFFLL;
        return (int)scaled;
    }

    bool TryApplyConfiguredPassiveDamagePacketRewrite(
        BYTE* packet,
        int packetLen,
        CustomSkillPacketRoute packetRoute,
        int skillIdOffset,
        int observedSkillId,
        uintptr_t callerRetAddr,
        unsigned short opcode)
    {
        if (!packet || packetLen < 8 || observedSkillId <= 0)
            return false;

        const int passiveBonusPercent = ResolveConfiguredPassiveDamagePercentBonusForSkill(observedSkillId);
        const int independentBuffBonusPercent = ResolveActiveIndependentBuffBonusTotal("damagePercent");
        const int bonusPercent = passiveBonusPercent + independentBuffBonusPercent;
        if (bonusPercent == 0)
            return false;

        if (passiveBonusPercent > 0 && HasRecentPassiveEffectDamageGetterUse(observedSkillId))
        {
            static std::map<int, DWORD> s_lastLocalEffectDamageSkipLogTickBySkillId;
            DWORD& lastLogTick = s_lastLocalEffectDamageSkipLogTickBySkillId[observedSkillId];
            const DWORD nowTick = GetTickCount();
            if (nowTick - lastLogTick > 1000)
            {
                lastLogTick = nowTick;
                WriteLogFmt("[SuperPassive] skip packet damage rewrite skillId=%d bonus=%d opcode=0x%X route=%s reason=damage-getter-owned",
                    observedSkillId,
                    bonusPercent,
                    (unsigned int)opcode,
                    PacketRouteToString(packetRoute));
            }
            return false;
        }

        std::vector<int> damageOffsets;
        if (!TryCollectAttackDamageOffsets(packet, packetLen, packetRoute, skillIdOffset, damageOffsets) ||
            damageOffsets.empty())
        {
            WriteLogFmt("[SuperPassive] skip packet damage rewrite skillId=%d bonus=%d opcode=0x%X route=%s len=%d caller=0x%08X",
                observedSkillId,
                bonusPercent,
                (unsigned int)opcode,
                PacketRouteToString(packetRoute),
                packetLen,
                (DWORD)(uintptr_t)callerRetAddr);
            return false;
        }

        int changedCount = 0;
        int firstChangedOffset = -1;
        int firstOldDamage = 0;
        int firstNewDamage = 0;
        int lastChangedOffset = -1;
        int lastOldDamage = 0;
        int lastNewDamage = 0;
        for (size_t i = 0; i < damageOffsets.size(); ++i)
        {
            const int offset = damageOffsets[i];
            const int oldDamage = ReadPacketInt(packet, offset);
            const int newDamage = ScaleDamageValueByPercent(oldDamage, bonusPercent);
            if (newDamage != oldDamage)
            {
                WritePacketInt(packet, offset, newDamage);
                if (changedCount == 0)
                {
                    firstChangedOffset = offset;
                    firstOldDamage = oldDamage;
                    firstNewDamage = newDamage;
                }
                lastChangedOffset = offset;
                lastOldDamage = oldDamage;
                lastNewDamage = newDamage;
                ++changedCount;
            }
        }

        if (changedCount <= 0)
            return false;

        WriteLogFmt("[SuperPassive] damage rewrite skillId=%d bonus=%d passive=%d buff=%d hits=%d changed=%d first@%d=%d->%d last@%d=%d->%d opcode=0x%X route=%s len=%d caller=0x%08X",
            observedSkillId,
            bonusPercent,
            passiveBonusPercent,
            independentBuffBonusPercent,
            (int)damageOffsets.size(),
            changedCount,
            firstChangedOffset,
            firstOldDamage,
            firstNewDamage,
            lastChangedOffset,
            lastOldDamage,
            lastNewDamage,
            (unsigned int)opcode,
            PacketRouteToString(packetRoute),
            packetLen,
            (DWORD)(uintptr_t)callerRetAddr);
        return true;
    }

    bool TryApplyConfiguredPassiveAttackCountPacketExpansion(
        const BYTE* packet,
        int packetLen,
        CustomSkillPacketRoute packetRoute,
        int skillIdOffset,
        int observedSkillId,
        uintptr_t callerRetAddr,
        unsigned short opcode,
        std::vector<BYTE>& outPacket)
    {
        outPacket.clear();
        if (!packet || packetLen < 8 || observedSkillId <= 0)
            return false;

        const int attackCountBonus = ResolveConfiguredPassiveAttackCountBonusForSkill(observedSkillId);
        const int mobCountBonus = ResolveConfiguredPassiveMobCountBonusForSkill(observedSkillId);
        if (attackCountBonus <= 0 && mobCountBonus <= 0)
            return false;

        if (attackCountBonus > 0 && HasRecentPassiveEffectAttackCountGetterUse(observedSkillId))
        {
            static std::map<int, DWORD> s_lastAttackCountGetterOwnedSkipLogTickBySkillId;
            DWORD& lastLogTick = s_lastAttackCountGetterOwnedSkipLogTickBySkillId[observedSkillId];
            const DWORD nowTick = GetTickCount();
            if (nowTick - lastLogTick > 1000)
            {
                lastLogTick = nowTick;
                WriteLogFmt("[SuperPassive] skip packet count expansion skillId=%d attackBonus=%d mobBonus=%d opcode=0x%X route=%s reason=attackCount-getter-owned",
                    observedSkillId,
                    attackCountBonus,
                    mobCountBonus,
                    (unsigned int)opcode,
                    PacketRouteToString(packetRoute));
            }
            return false;
        }

        AttackPacketLayout layout;
        if (!TryCollectAttackPacketLayout(packet, packetLen, packetRoute, skillIdOffset, layout))
        {
            WriteLogFmt("[SuperPassive] skip packet count expansion skillId=%d attackBonus=%d mobBonus=%d opcode=0x%X route=%s len=%d caller=0x%08X",
                observedSkillId,
                attackCountBonus,
                mobCountBonus,
                (unsigned int)opcode,
                PacketRouteToString(packetRoute),
                packetLen,
                (DWORD)(uintptr_t)callerRetAddr);
            return false;
        }

        int newHitCount = layout.hitCount;
        if (attackCountBonus > 0)
            newHitCount = (std::min)(15, layout.hitCount + attackCountBonus);

        // A send-packet rewrite can safely add hit damage entries for already-selected
        // targets. It cannot invent new monster object IDs; mobCount remains a server
        // allowance for packets that the client can already build with more targets.
        if (mobCountBonus > 0)
        {
            WriteLogFmt("[SuperPassive] mobCount bonus configured skillId=%d bonus=%d currentTargets=%d (packet rewrite cannot synthesize unknown monster OIDs)",
                observedSkillId,
                mobCountBonus,
                layout.targetCount);
        }

        if (newHitCount <= layout.hitCount)
            return false;

        const int extraHitsPerTarget = newHitCount - layout.hitCount;
        const int extraBytes = extraHitsPerTarget * 4 * layout.targetCount;
        if (extraBytes <= 0 || packetLen + extraBytes > 0xFFFF)
            return false;

        outPacket.reserve((size_t)(packetLen + extraBytes));

        int copyCursor = 0;
        for (size_t targetIndex = 0; targetIndex < layout.targets.size(); ++targetIndex)
        {
            const AttackTargetPacketBlock& block = layout.targets[targetIndex];
            if (block.blockStart < copyCursor || block.damageStart < block.blockStart ||
                block.damageEnd < block.damageStart || block.blockEnd < block.damageEnd ||
                block.blockEnd > packetLen)
            {
                outPacket.clear();
                return false;
            }

            outPacket.insert(outPacket.end(), packet + copyCursor, packet + block.damageEnd);

            int seedDamage = 1;
            if (!block.damageOffsets.empty())
                seedDamage = ReadPacketInt(packet, block.damageOffsets.back());
            if (seedDamage <= 0)
                seedDamage = 1;

            for (int i = 0; i < extraHitsPerTarget; ++i)
            {
                const BYTE* src = reinterpret_cast<const BYTE*>(&seedDamage);
                outPacket.insert(outPacket.end(), src, src + 4);
            }

            copyCursor = block.damageEnd;
        }

        if (copyCursor < 0 || copyCursor > packetLen)
        {
            outPacket.clear();
            return false;
        }
        outPacket.insert(outPacket.end(), packet + copyCursor, packet + packetLen);

        if ((int)outPacket.size() != packetLen + extraBytes ||
            layout.tbyteOffset < 0 ||
            layout.tbyteOffset >= (int)outPacket.size())
        {
            outPacket.clear();
            return false;
        }

        outPacket[(size_t)layout.tbyteOffset] =
            (BYTE)(((layout.targetCount & 0x0F) << 4) | (newHitCount & 0x0F));

        WriteLogFmt("[SuperPassive] attackCount packet expansion skillId=%d oldHits=%d newHits=%d targets=%d extraBytes=%d opcode=0x%X route=%s caller=0x%08X",
            observedSkillId,
            layout.hitCount,
            newHitCount,
            layout.targetCount,
            extraBytes,
            (unsigned int)opcode,
            PacketRouteToString(packetRoute),
            (DWORD)(uintptr_t)callerRetAddr);
        return true;
    }

    bool TryRewritePacketFromActiveNativeRelease(
        BYTE* packet,
        int packetLen,
        CustomSkillPacketRoute packetRoute,
        int skillIdOffset,
        int skillLevelOffset,
        int observedSkillId,
        uintptr_t callerRetAddr,
        unsigned short opcode)
    {
        if (!packet || packetLen <= 0)
            return false;

        if (!IsActiveNativeReleaseContextFresh())
            return false;

        const int activeCustomSkillId = g_activeNativeRelease.customSkillId;
        if (activeCustomSkillId <= 0)
        {
            ClearActiveNativeReleaseContext();
            return false;
        }

        const bool sameRoute = (g_activeNativeRelease.packetRoute == packetRoute);
        const bool preserveMountedDemonChildPacket =
            activeCustomSkillId == 30010110 &&
            IsMountedDemonJumpRuntimeChildSkillId(observedSkillId);
        const bool matchesPrimaryObserved =
            (observedSkillId == activeCustomSkillId) ||
            (g_activeNativeRelease.classifierProxySkillId > 0 &&
             observedSkillId == g_activeNativeRelease.classifierProxySkillId) ||
            preserveMountedDemonChildPacket;

        const DWORD now = GetTickCount();
        if (sameRoute)
        {
            if (g_activeNativeRelease.firstRewriteTick == 0 && !matchesPrimaryObserved)
                return false;
        }
        else
        {
            if (g_activeNativeRelease.firstRewriteTick == 0)
                return false;
            if (now - g_activeNativeRelease.firstRewriteTick > kNativeReleaseFollowupWindowMs)
                return false;
        }

        const int targetSkillId =
            preserveMountedDemonChildPacket ? observedSkillId : activeCustomSkillId;

        int customLevel = GetTrackedSkillLevel(activeCustomSkillId);
        if (customLevel <= 0)
            customLevel = 1;
        if (customLevel > 255)
            customLevel = 255;

        if (observedSkillId != targetSkillId)
            WritePacketInt(packet, skillIdOffset, targetSkillId);

        if (skillLevelOffset >= 0 && packetLen > skillLevelOffset)
            packet[skillLevelOffset] = (BYTE)customLevel;

        if (g_activeNativeRelease.firstRewriteTick == 0)
            g_activeNativeRelease.firstRewriteTick = now;

        if (g_activeNativeRelease.remainingRewriteBudget > 0)
            --g_activeNativeRelease.remainingRewriteBudget;

        const bool followupRoute = !sameRoute;
        WriteLogFmt("[SkillPacket] native-route opcode=0x%X route=%s armRoute=%s followup=%d releaseClass=%s observed=%d -> custom=%d level=%d len=%d remain=%d caller=0x%08X",
            (unsigned int)opcode,
            PacketRouteToString(packetRoute),
            PacketRouteToString(g_activeNativeRelease.packetRoute),
            followupRoute ? 1 : 0,
            ReleaseClassToString(g_activeNativeRelease.releaseClass),
            observedSkillId,
            targetSkillId,
            customLevel,
            packetLen,
            g_activeNativeRelease.remainingRewriteBudget,
            (DWORD)(uintptr_t)callerRetAddr);

        if (g_activeNativeRelease.remainingRewriteBudget <= 0)
        {
            ClearActiveNativeReleaseContext();
            // 释放链完全结束，清除 presentation context，防止残留污染后续原生技能特效
            ClearRecentNativePresentationContext();
        }
        return true;
    }

    SkillItem* FindManagerSkillItem(SkillManager* manager, int skillId)
    {
        if (!manager || skillId <= 0)
            return nullptr;

        for (int t = 0; t < manager->tabCount; ++t)
        {
            SkillTab& tab = manager->tabs[t];
            for (int i = 0; i < tab.count; ++i)
            {
                if (tab.skills[i].skillID == skillId)
                    return &tab.skills[i];
            }
        }

        return nullptr;
    }

    int ResolveFallbackUpgradeState(int level, int maxLevel)
    {
        if (maxLevel <= 0)
            return (level > 0) ? -1 : 0;
        if (level < maxLevel)
            return 1;
        return -1;
    }

    NativeSkillRowInfo FindNativeSkillRowInfo(int skillId)
    {
        NativeSkillRowInfo info;

        const uintptr_t skillWndThis = g_bridge.skillWndThis ? g_bridge.skillWndThis : GetLiveSkillWndThis();
        if (!skillWndThis)
            return info;

        uintptr_t listView = 0;
        uintptr_t entries = 0;
        int totalCount = 0;
        if (!SafeReadValue(skillWndThis + kOff_SkillWnd_ListView, listView) || !listView)
            return info;
        if (!SafeReadValue(skillWndThis + kOff_SkillWnd_Entries, entries) || !entries)
            return info;
        if (!SafeReadValue(listView + kOff_ListView_Count, totalCount))
            return info;

        if (totalCount < 0)
            totalCount = 0;
        if (totalCount > kMaxReasonableSkillCount)
            totalCount = kMaxReasonableSkillCount;

        for (int i = 0; i < totalCount; ++i)
        {
            const uintptr_t rowSlot = entries + static_cast<uintptr_t>(i) * 8;
            int slotSkillId = 0;
            uintptr_t rowData = 0;
            if (!SafeReadValue(rowSlot, slotSkillId))
                break;
            if (!SafeReadValue(rowSlot + 4, rowData) || !rowData)
                break;

            int rowSkillId = 0;
            if (!SafeReadValue(rowData, rowSkillId) || rowSkillId == 0)
                rowSkillId = slotSkillId;

            if (rowSkillId != skillId)
                continue;

            info.rowData = rowData;
            info.rowIndex = i;
            info.found = true;
            return info;
        }

        return info;
    }

    void RefreshSkillNativeState(SkillItem& item)
    {
        item.upgradeState = ResolveFallbackUpgradeState(item.level, item.maxLevel);
        item.upgradeBlocked = false;
        item.hasNativeUpgradeState = false;
    }

    bool GameRequestUpgradeSkillBySkillId(int skillId)
    {
        if (skillId <= 0)
            return false;

        const DWORD wndMan = *(DWORD*)ADDR_CWndMan;
        if (!wndMan || SafeIsBadReadPtr((void*)wndMan, 4))
            return false;

        DWORD fnAddr = ADDR_BF43E0;
        __try
        {
            __asm
            {
                push skillId
                mov ecx, wndMan
                call fnAddr
            }
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            WriteLogFmt("[SkillBridge] EXCEPTION in sub_BF43E0: 0x%08X", GetExceptionCode());
            return false;
        }

        WriteLogFmt("[SkillBridge] native plus by skillId skillId=%d", skillId);
        return true;
    }

    bool GameRequestUpgradeSkillByRow(int skillId)
    {
        if (skillId <= 0)
            return false;

        const uintptr_t skillWndThis = g_bridge.skillWndThis ? g_bridge.skillWndThis : GetLiveSkillWndThis();
        if (!skillWndThis || SafeIsBadReadPtr((void*)skillWndThis, 0x20))
            return false;

        const NativeSkillRowInfo nativeInfo = FindNativeSkillRowInfo(skillId);
        if (!nativeInfo.found || nativeInfo.rowIndex < 0)
            return false;

        uintptr_t pageObj = 0;
        int firstVisibleIndex = 0;
        if (SafeReadValue(skillWndThis + kOff_SkillWnd_PageObj, pageObj) && pageObj)
            SafeReadValue(pageObj + kOff_PageObj_FirstVisibleIndex, firstVisibleIndex);

        const int relativeRowIndex = nativeInfo.rowIndex - firstVisibleIndex;
        FnSkillWndPlusByRow fn = reinterpret_cast<FnSkillWndPlusByRow>(ADDR_9DB640);
        __try
        {
            fn(reinterpret_cast<void*>(skillWndThis), relativeRowIndex);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            WriteLogFmt("[SkillBridge] EXCEPTION in sub_9DB640: 0x%08X", GetExceptionCode());
            return false;
        }

        WriteLogFmt("[SkillBridge] native plus row skillId=%d row=%d firstVisible=%d relative=%d",
            skillId, nativeInfo.rowIndex, firstVisibleIndex, relativeRowIndex);
        return true;
    }

    bool GameRequestUpgradeSkill(int skillId)
    {
        if (GameRequestUpgradeSkillBySkillId(skillId))
            return true;
        return GameRequestUpgradeSkillByRow(skillId);
    }

    void ClearPendingSuperSkillUpgradePacketRewrite()
    {
        g_pendingSuperSkillUpgradePacketRewrite = PendingSuperSkillUpgradePacketRewrite{};
    }

    void ArmPendingSuperSkillUpgradePacketRewrite(int proxySkillId, int targetSkillId)
    {
        g_pendingSuperSkillUpgradePacketRewrite.proxySkillId = proxySkillId;
        g_pendingSuperSkillUpgradePacketRewrite.targetSkillId = targetSkillId;
        g_pendingSuperSkillUpgradePacketRewrite.armedTick = GetTickCount();
        WriteLogFmt("[SuperSkill] arm distribute_sp rewrite proxy=%d -> target=%d",
            proxySkillId,
            targetSkillId);
    }

    bool IsPendingSuperSkillUpgradePacketRewriteFresh()
    {
        if (g_pendingSuperSkillUpgradePacketRewrite.proxySkillId <= 0 ||
            g_pendingSuperSkillUpgradePacketRewrite.targetSkillId <= 0 ||
            g_pendingSuperSkillUpgradePacketRewrite.armedTick == 0)
        {
            return false;
        }

        const DWORD nowTick = GetTickCount();
        if (nowTick - g_pendingSuperSkillUpgradePacketRewrite.armedTick > kPendingSuperSkillUpgradeRewriteWindowMs)
        {
            ClearPendingSuperSkillUpgradePacketRewrite();
            return false;
        }
        return true;
    }

    void ArmPendingOptimisticSuperSkillLevelHold(int skillId, int expectedLevel)
    {
        if (skillId <= 0 || expectedLevel <= 0)
            return;

        PendingOptimisticSuperSkillLevelHold& hold = g_pendingOptimisticSuperSkillLevelHoldBySkillId[skillId];
        hold.expectedLevel = expectedLevel;
        hold.expireTick = GetTickCount() + kPendingOptimisticSuperSkillLevelHoldWindowMs;
    }

    void ClearPendingOptimisticSuperSkillLevelHold(int skillId)
    {
        if (skillId <= 0)
            return;
        g_pendingOptimisticSuperSkillLevelHoldBySkillId.erase(skillId);
    }

    void ClearAllPendingOptimisticSuperSkillLevelHolds(const char* reason)
    {
        if (!g_pendingOptimisticSuperSkillLevelHoldBySkillId.empty())
        {
            WriteLogFmt("[SkillLevelBridge] clear optimistic holds count=%d reason=%s",
                (int)g_pendingOptimisticSuperSkillLevelHoldBySkillId.size(),
                reason ? reason : "unknown");
        }
        g_pendingOptimisticSuperSkillLevelHoldBySkillId.clear();
    }

    bool TryGetFreshPendingOptimisticSuperSkillLevelHold(int skillId, PendingOptimisticSuperSkillLevelHold& outHold)
    {
        outHold = PendingOptimisticSuperSkillLevelHold{};
        if (skillId <= 0)
            return false;

        std::map<int, PendingOptimisticSuperSkillLevelHold>::iterator it =
            g_pendingOptimisticSuperSkillLevelHoldBySkillId.find(skillId);
        if (it == g_pendingOptimisticSuperSkillLevelHoldBySkillId.end())
            return false;

        const DWORD nowTick = GetTickCount();
        if (it->second.expireTick == 0 || nowTick > it->second.expireTick)
        {
            g_pendingOptimisticSuperSkillLevelHoldBySkillId.erase(it);
            return false;
        }

        outHold = it->second;
        return true;
    }

    void ApplyOptimisticSuperSkillUpgradeObservation(int skillId, int carrierSkillId, int nextSkillLevel, int nextCarrierPoints)
    {
        if (skillId > 0)
        {
            SkillItem* item = FindManagerSkillItem(GetBridgeManager(), skillId);
            if (item && nextSkillLevel > item->level)
                item->level = nextSkillLevel;

            g_observedBaseLevelsBySkillId[skillId] = nextSkillLevel;
            g_observedCurrentLevelsBySkillId[skillId] = nextSkillLevel;
            RecordPersistentSuperSkillLevel(skillId, nextSkillLevel, "optimistic-upgrade");
            ArmPendingOptimisticSuperSkillLevelHold(skillId, nextSkillLevel);
        }
        if (carrierSkillId > 0)
        {
            g_observedBaseLevelsBySkillId[carrierSkillId] = nextCarrierPoints;
            g_observedCurrentLevelsBySkillId[carrierSkillId] = nextCarrierPoints;
        }

        WriteLogFmt("[SuperSkill] optimistic observe skill=%d level=%d carrier=%d points=%d",
            skillId,
            nextSkillLevel,
            carrierSkillId,
            nextCarrierPoints);
    }

    bool GameRequestSuperSkillUpgradeByProxyPacket(int proxySkillId, int targetSkillId)
    {
        if (proxySkillId <= 0 || targetSkillId <= 0)
            return false;

        ArmPendingSuperSkillUpgradePacketRewrite(proxySkillId, targetSkillId);
        const bool requestOk = GameRequestUpgradeSkillBySkillId(proxySkillId);
        if (!requestOk)
        {
            ClearPendingSuperSkillUpgradePacketRewrite();
            return false;
        }

        WriteLogFmt("[SuperSkill] proxy distribute_sp request proxy=%d target=%d",
            proxySkillId,
            targetSkillId);
        return true;
    }

    bool RequestSuperSkillUpgrade(const SkillItem& item)
    {
        const int carrierSkillId = ResolveSuperSkillCarrierSkillId(item);
        const int availablePoints = ResolveAvailableSuperSkillPointsForCarrier(carrierSkillId);
        const int superSpCost = (item.superSpCost > 0) ? item.superSpCost : 1;
        const bool hasEnoughPoints = carrierSkillId > 0 && availablePoints >= superSpCost;

        if (!hasEnoughPoints)
        {
            if (item.allowNativeUpgradeFallback && item.hasNativeUpgradeState)
            {
                WriteLogFmt(
                    "[SuperSkill] plus fallback(no-super-sp)->native skillId=%d cost=%d carrier=%d points=%d",
                    item.skillID,
                    superSpCost,
                    carrierSkillId,
                    availablePoints);
                return GameRequestUpgradeSkill(item.skillID);
            }

            WriteLogFmt(
                "[SuperSkill] plus BLOCKED skillId=%d cost=%d carrier=%d points=%d",
                item.skillID,
                superSpCost,
                carrierSkillId,
                availablePoints);
            return false;
        }

        // 超级技能升级路线：
        // 1. 如果技能在原生系统里 (hasNativeUpgradeState)，用 item.skillID 走原生发包
        // 2. 如果技能不在原生系统里，走本地独立升级（直接 +1），不调用 BF43E0
        //    因为 BF43E0 对不存在的 skillId 会栈溢出崩溃 (STATUS_STACK_BUFFER_OVERRUN)
        if (item.hasNativeUpgradeState)
        {
            const bool requestOk = GameRequestUpgradeSkill(item.skillID);
            WriteLogFmt(
                "[SuperSkill] plus native-upgrade skillId=%d ok=%d cost=%d carrier=%d points=%d",
                item.skillID,
                requestOk ? 1 : 0,
                superSpCost,
                carrierSkillId,
                availablePoints);
            return requestOk;
        }

        // 非原生技能：本地独立升级，不走游戏发包
        const int proxySkillId = carrierSkillId > 0 ? carrierSkillId : item.skillID;
        const bool requestOk = GameRequestSuperSkillUpgradeByProxyPacket(proxySkillId, item.skillID);

        WriteLogFmt(
            "[SuperSkill] plus proxy-upgrade skillId=%d ok=%d level=%d->%d cost=%d carrier=%d proxy=%d points=%d",
            item.skillID,
            requestOk ? 1 : 0,
            item.level,
            item.level + 1,
            superSpCost,
            carrierSkillId,
            proxySkillId,
            availablePoints);
        return requestOk;
    }

    bool RequestSuperSkillReset()
    {
        if (g_superSkillsBySkillId.empty())
            LoadSuperSkillRegistry();

        const int proxySkillId = ResolveAnySuperSkillCarrierSkillId();
        if (proxySkillId <= 0)
        {
            WriteLogFmt("[SuperSkill] reset request BLOCKED: missing carrier");
            return false;
        }

        ArmPendingSuperSkillUpgradePacketRewrite(proxySkillId, kSuperSkillResetRequestSkillId);
        const bool requestOk = GameRequestUpgradeSkillBySkillId(proxySkillId);
        if (!requestOk)
        {
            ClearPendingSuperSkillUpgradePacketRewrite();
            WriteLogFmt("[SuperSkill] reset request FAIL proxy=%d target=%d",
                proxySkillId,
                kSuperSkillResetRequestSkillId);
            return false;
        }

        const DWORD nowTick = GetTickCount();
        g_lastRefreshTick = 0;
        g_fastRefreshUntilTick = nowTick + kPendingRefreshWindowMs;
        g_superSkillResetLevelSyncUntilTick = nowTick + kSuperSkillResetLevelSyncWindowMs;
        WriteLogFmt("[SuperSkill] reset request sent proxy=%d target=%d localClearDeferred=1",
            proxySkillId,
            kSuperSkillResetRequestSkillId);
        return true;
    }

    bool RequestSuperSkillResetCostPreview()
    {
        if (g_superSkillResetPreviewReceiveHookReady == 0)
        {
            WriteLog("[SuperSkill] reset preview BLOCKED: recv hook not ready");
            return false;
        }

        if (g_superSkillsBySkillId.empty())
            LoadSuperSkillRegistry();

        const int proxySkillId = ResolveAnySuperSkillCarrierSkillId();
        if (proxySkillId <= 0)
        {
            WriteLogFmt("[SuperSkill] reset preview BLOCKED: missing carrier");
            return false;
        }

        ArmPendingSuperSkillUpgradePacketRewrite(proxySkillId, kSuperSkillResetPreviewRequestSkillId);
        const bool requestOk = GameRequestUpgradeSkillBySkillId(proxySkillId);
        if (!requestOk)
        {
            ClearPendingSuperSkillUpgradePacketRewrite();
            WriteLogFmt("[SuperSkill] reset preview FAIL proxy=%d target=%d",
                proxySkillId,
                kSuperSkillResetPreviewRequestSkillId);
            return false;
        }

        WriteLogFmt("[SuperSkill] reset preview sent proxy=%d target=%d",
            proxySkillId,
            kSuperSkillResetPreviewRequestSkillId);
        return true;
    }

    void PopulateManagerTab(SkillManager* manager, int tabIndex, const char* tabName, const std::vector<int>& skillIds)
    {
        if (!manager || tabIndex < 0 || tabIndex >= MAX_TABS)
            return;

        SkillTab& tab = manager->tabs[tabIndex];
        tab.Init(tabIndex, tabName);

        for (size_t i = 0; i < skillIds.size() && tab.count < MAX_SKILLS_PER_TAB; ++i)
        {
            const int skillId = skillIds[i];
            SuperSkillDefinition superDefinition = {};
            const bool isSuperSkill = FindSuperSkillDefinition(skillId, superDefinition);
            if (isSuperSkill && !ShouldShowSuperSkillInOverlay(skillId, superDefinition))
                continue;

            HiddenSkillDefinition hiddenDefinition = {};
            if (FindHiddenSkillDefinition(skillId, hiddenDefinition) &&
                hiddenDefinition.hideFromSuperSkillWnd)
            {
                continue;
            }

            std::string skillName;
            if (!SkillLocalDataGetName(skillId, skillName) || skillName.empty())
            {
                char fallbackName[16] = {};
                sprintf_s(fallbackName, "#%07d", skillId);
                skillName = fallbackName;
            }
            if (isSuperSkill)
                skillName = BuildSuperSkillDisplayName(skillName, superDefinition);

            // 等级优先从游戏 CALL 查询，fallback 到本地 JSON
            int gameLevel = GameGetSkillLevel(skillId);
            int gameMaxLevel = GameGetMaxSkillLevel(skillId);
            int localMaxLevel = 1;
            SkillLocalDataGetMaxLevel(skillId, localMaxLevel);

            int persistentLevel = 0;
            if (TryResolvePersistentNonNativeSuperSkillLevel(skillId, gameLevel, 0, persistentLevel))
                gameLevel = persistentLevel;

            int maxLevel = (gameMaxLevel > 0) ? gameMaxLevel : ((localMaxLevel > 0) ? localMaxLevel : 1);
            int level = (gameLevel > 0) ? gameLevel : 0;
            if (isSuperSkill && level > 0)
                RecordPersistentSuperSkillLevel(skillId, level, "populate");
            bool passiveHint = false;
            SkillLocalDataIsPassiveHint(skillId, passiveHint);

            tab.AddSkill(skillId, skillName.c_str(), level);
            SkillItem& item = tab.skills[tab.count - 1];
            item.maxLevel = maxLevel;
            item.iconID = skillId;
            item.isEnabled = true;
            item.isPassive = isSuperSkill ? superDefinition.passive : passiveHint;
            item.isSuperSkill = isSuperSkill;
            item.hideFromNativeSkillWnd = isSuperSkill && superDefinition.hideFromNativeSkillWnd;
            item.allowNativeUpgradeFallback = isSuperSkill && superDefinition.allowNativeUpgradeFallback;
            item.superSpCost = isSuperSkill ? superDefinition.superSpCost : 0;
            item.superSpCarrierSkillId = isSuperSkill ? superDefinition.superSpCarrierSkillId : 0;
            RefreshSkillNativeState(item);

            WriteLogFmt("[SkillBridge] populate skillId=%d level=%d/%d (game=%d/%d local=%d passive=%d super=%d cost=%d carrier=%d upgradeState=%d blocked=%d native=%d)",
                skillId, level, maxLevel, gameLevel, gameMaxLevel, localMaxLevel,
                item.isPassive ? 1 : 0,
                item.isSuperSkill ? 1 : 0,
                item.superSpCost,
                item.superSpCarrierSkillId,
                item.upgradeState,
                item.upgradeBlocked ? 1 : 0,
                item.hasNativeUpgradeState ? 1 : 0);
        }
    }

    void PopulateManagerTab(SkillManager* manager, int tabIndex, const char* tabName, const int* skillIds, int count)
    {
        std::vector<int> ids;
        ids.reserve((count > 0) ? count : 0);
        for (int i = 0; i < count; ++i)
            ids.push_back(skillIds[i]);
        PopulateManagerTab(manager, tabIndex, tabName, ids);
    }

    void ConfigureIndependentOverlayManager(SkillManager* manager)
    {
        if (!manager)
            return;

        SkillLocalDataInitialize();

        memset(manager->tabs, 0, sizeof(manager->tabs));
        manager->tabCount = 2;
        manager->currentTab = 0;

        if (!g_superSkillsBySkillId.empty())
        {
            PopulateManagerTab(manager, 0, "Passive", g_superSkillIdsByTab[0]);
            PopulateManagerTab(manager, 1, "Active", g_superSkillIdsByTab[1]);
        }
        else
        {
            PopulateManagerTab(manager, 0, "Tab0", kIndependentTab0SkillIds, ARRAYSIZE(kIndependentTab0SkillIds));
            PopulateManagerTab(manager, 1, "Tab1", kIndependentTab1SkillIds, ARRAYSIZE(kIndependentTab1SkillIds));
        }

        int currentJobId = 0;
        g_lastOverlayConfiguredJobId = TryReadCurrentPlayerJobId(currentJobId) ? currentJobId : -1;

        WriteLogFmt("[SkillBridge] independent manager ready: tab0=%d tab1=%d",
            manager->tabs[0].count, manager->tabs[1].count);
    }

    SkillOverlaySource* ResolveActiveSource()
    {
        g_bridge.gameSource.skillWndThis = g_bridge.skillWndThis;
        g_bridge.managerSource.skillWndThis = g_bridge.skillWndThis;
        // 当前阶段始终使用独立 manager source，不受游戏 SkillWnd 影响
        return &g_bridge.managerSource;
    }

    SkillOverlaySource* ResolveActionSource()
    {
        if (g_bridge.activeSource)
            return g_bridge.activeSource;
        return ResolveActiveSource();
    }

    void LogSourceSwitchIfNeeded(const char* name)
    {
        if (!name)
            name = "unknown";
        if (g_bridge.lastLoggedSource != name)
        {
            g_bridge.lastLoggedSource = name;
            WriteLogFmt("[SkillBridge] active source = %s", name);
        }
    }

    RetroSkillActionDecision OnBridgeTabAction(const RetroSkillActionContext& context, void* userData)
    {
        (void)userData;
        return SkillOverlaySourceHandleTabAction(ResolveActionSource(), context);
    }

    RetroSkillActionDecision OnBridgePlusAction(const RetroSkillActionContext& context, void* userData)
    {
        (void)userData;
        RetroSkillActionDecision result = SkillOverlaySourceHandlePlusAction(ResolveActionSource(), context);
        if (context.skillId <= 0)
            return result;

        SkillManager* manager = GetBridgeManager();
        SkillItem* item = FindManagerSkillItem(manager, context.skillId);
        if (!item)
        {
            WriteLogFmt("[SkillBridge] plus BLOCKED skillId=%d (not found)", context.skillId);
            return RetroSkill_SuppressDefault;
        }

        RefreshSkillNativeState(*item);
        if (!item->isEnabled || item->upgradeBlocked || item->level >= item->maxLevel)
        {
            WriteLogFmt("[SkillBridge] plus BLOCKED skillId=%d level=%d/%d state=%d blocked=%d enabled=%d passive=%d super=%d native=%d",
                context.skillId,
                item->level,
                item->maxLevel,
                item->upgradeState,
                item->upgradeBlocked ? 1 : 0,
                item->isEnabled ? 1 : 0,
                item->isPassive ? 1 : 0,
                item->isSuperSkill ? 1 : 0,
                item->hasNativeUpgradeState ? 1 : 0);
            return RetroSkill_SuppressDefault;
        }

        const bool upgradeOk = item->isSuperSkill
            ? RequestSuperSkillUpgrade(*item)
            : GameRequestUpgradeSkill(context.skillId);
        if (!upgradeOk)
        {
            WriteLogFmt("[SkillBridge] plus FAIL skillId=%d", context.skillId);
            return RetroSkill_SuppressDefault;
        }

        g_lastRefreshTick = 0;
        g_fastRefreshUntilTick = GetTickCount() + kPendingRefreshWindowMs;
        WriteLogFmt("[SkillBridge] plus request sent skillId=%d level=%d/%d", context.skillId, item->level, item->maxLevel);
        return RetroSkill_SuppressDefault;
    }

    RetroSkillActionDecision OnBridgeInitPreviewAction(const RetroSkillActionContext& context, void* userData)
    {
        (void)context;
        (void)userData;
        return RequestSuperSkillResetCostPreview() ? RetroSkill_SuppressDefault : RetroSkill_UseDefault;
    }

    RetroSkillActionDecision OnBridgeInitAction(const RetroSkillActionContext& context, void* userData)
    {
        (void)userData;
        RetroSkillActionDecision result = SkillOverlaySourceHandleInitAction(ResolveActionSource(), context);
        const bool requestOk = RequestSuperSkillReset();
        if (!requestOk)
        {
            SkillManager* manager = GetBridgeManager();
            if (manager)
            {
                RefreshSkillLevelsFromGame(manager);
                WriteLogFmt("[SkillBridge] init request tab=%d sent=%d -> local refresh kept because request failed",
                    context.currentTab,
                    0);
            }
        }
        else
        {
            WriteLogFmt("[SkillBridge] init request tab=%d sent=%d -> awaiting authoritative reset sync",
                context.currentTab,
                1);
        }
        return result == RetroSkill_UseDefault ? RetroSkill_SuppressDefault : result;
    }

    RetroSkillActionDecision OnBridgeSkillDragBegin(const RetroSkillActionContext& context, void* userData)
    {
        (void)userData;
        return SkillOverlaySourceHandleSkillDragBegin(ResolveActionSource(), context);
    }

    RetroSkillActionDecision OnBridgeSkillDragEnd(const RetroSkillActionContext& context, void* userData)
    {
        (void)userData;
        SkillOverlaySource* actionSource = ResolveActionSource();
        SkillOverlaySourceHandleSkillDragEnd(actionSource, context);

        // 先让 source 处理（记日志）
        SkillOverlaySourceHandleSkillDragEnd(ResolveActionSource(), context);

        // 如果 drop 到了技能栏的某个 slot，调用游戏原生赋值 + 记录本地绑定
        if (context.dropSlotIndex >= 0 && context.dropSlotIndex < SKILL_BAR_TOTAL_SLOTS)
        {
            g_bridge.gameSource.skillWndThis = g_bridge.skillWndThis;
            bool nativeOk = false;
            bool displayUpdated = false;
            bool queueRestore = false;
            const char* route = "native_keymap";
            nativeOk = SkillOverlayBridgeAssignSkillToQuickSlot(context.dropSlotIndex, context.skillId);
            if (nativeOk)
            {
                SkillOverlayBridgeSetQuickSlot(context.dropSlotIndex, context.skillId, context.currentTab, nullptr);
                displayUpdated = true;
            }
            else if (context.droppedOutsidePanel)
            {
                route = "game_native_drag";
                nativeOk = SkillOverlaySourceGameSimulateNativeDrag(&g_bridge.gameSource, context);
                if (nativeOk)
                {
                    SkillOverlayBridgeSetQuickSlot(context.dropSlotIndex, context.skillId, context.currentTab, nullptr);
                    displayUpdated = true;
                    queueRestore = true;
                }
            }
            WriteLogFmt("[SkillBridge] quickSlot[%d] = skillId=%d tab=%d native=%s route=%s",
                context.dropSlotIndex, context.skillId, context.currentTab, nativeOk ? "OK" : "FAIL", route);

            if (queueRestore)
                QueuePendingQuickSlotRestore(context.dropSlotIndex, context.skillId, "drag_end_native_drag");

            if (displayUpdated)
            {
                g_lastRefreshTick = 0;
                g_fastRefreshUntilTick = GetTickCount() + kPendingRefreshWindowMs;
                SkillOverlayBridgeSaveState(g_bridge.savePath);
            }

            // 拖到技能栏后自动保存状态
            SkillOverlayBridgeSaveState(g_bridge.savePath);
        }

        return RetroSkill_UseDefault;
    }

    RetroSkillActionDecision OnBridgeSkillUse(const RetroSkillActionContext& context, void* userData)
    {
        (void)userData;
        if (context.skillId <= 0)
            return RetroSkill_UseDefault;

        bool ok = SkillOverlayBridgeUseSkill(context.skillId);
        WriteLogFmt("[SkillBridge] onSkillUse skillId=%d level=%d result=%s",
            context.skillId, context.currentLevel, ok ? "OK" : "BLOCKED");
        return RetroSkill_UseDefault;
    }

    // ========================================================================
    // Simple JSON persistence helpers (no external JSON lib)
    // ========================================================================

    bool Utf8PathToWide(const char* path, std::wstring& outPath)
    {
        outPath.clear();
        if (!path || !path[0])
            return false;

        const int length = ::MultiByteToWideChar(CP_UTF8, 0, path, -1, nullptr, 0);
        if (length <= 1)
            return false;

        outPath.resize(static_cast<size_t>(length - 1));
        return ::MultiByteToWideChar(CP_UTF8, 0, path, -1, &outPath[0], length) > 0;
    }

    bool OpenFileByUtf8Path(const char* path, const wchar_t* mode, FILE** outFile)
    {
        if (!outFile)
            return false;
        *outFile = nullptr;

        std::wstring widePath;
        if (!Utf8PathToWide(path, widePath))
            return false;

        return _wfopen_s(outFile, widePath.c_str(), mode) == 0 && *outFile;
    }

    bool WriteTextFile(const char* path, const char* text)
    {
        FILE* f = nullptr;
        if (!OpenFileByUtf8Path(path, L"wb", &f))
            return false;
        size_t len = strlen(text);
        size_t written = fwrite(text, 1, len, f);
        fclose(f);
        return written == len;
    }

    bool ReadTextFile(const char* path, std::string& out)
    {
        out.clear();
        FILE* f = nullptr;
        if (!OpenFileByUtf8Path(path, L"rb", &f))
            return false;
        fseek(f, 0, SEEK_END);
        long len = ftell(f);
        fseek(f, 0, SEEK_SET);
        if (len <= 0) { fclose(f); return false; }
        out.resize((size_t)len);
        size_t rd = fread(&out[0], 1, (size_t)len, f);
        fclose(f);
        if (rd != (size_t)len) { out.clear(); return false; }
        return true;
    }

    // Parse a simple integer value after "key": in JSON text
    bool ParseJsonInt(const std::string& json, const char* key, int& outVal)
    {
        std::string token = std::string("\"") + key + "\"";
        size_t pos = json.find(token);
        if (pos == std::string::npos) return false;
        pos = json.find(':', pos + token.size());
        if (pos == std::string::npos) return false;
        ++pos;
        while (pos < json.size() && (json[pos] == ' ' || json[pos] == '\t')) ++pos;
        bool negative = false;
        if (pos < json.size() && json[pos] == '-') { negative = true; ++pos; }
        if (pos >= json.size() || json[pos] < '0' || json[pos] > '9') return false;
        int val = 0;
        while (pos < json.size() && json[pos] >= '0' && json[pos] <= '9')
        {
            val = val * 10 + (json[pos] - '0');
            ++pos;
        }
        outVal = negative ? -val : val;
        return true;
    }

    // Find array element: "arrayKey": [ ... { ... }, { ... } ]
    // Returns position of nth '{' inside the array
    bool FindArrayElement(const std::string& json, const char* arrayKey, int index, size_t& outBegin, size_t& outEnd)
    {
        std::string token = std::string("\"") + arrayKey + "\"";
        size_t pos = json.find(token);
        if (pos == std::string::npos) return false;
        pos = json.find('[', pos);
        if (pos == std::string::npos) return false;

        int elemIdx = 0;
        size_t i = pos + 1;
        while (i < json.size())
        {
            while (i < json.size() && json[i] != '{' && json[i] != ']') ++i;
            if (i >= json.size() || json[i] == ']') return false;

            // Found '{'
            size_t objStart = i;
            int depth = 0;
            for (; i < json.size(); ++i)
            {
                if (json[i] == '{') ++depth;
                else if (json[i] == '}')
                {
                    --depth;
                    if (depth == 0)
                    {
                        if (elemIdx == index)
                        {
                            outBegin = objStart;
                            outEnd = i + 1;
                            return true;
                        }
                        ++elemIdx;
                        ++i;
                        break;
                    }
                }
            }
        }
        return false;
    }
}

void SkillOverlayBridgeInitialize(SkillManager* manager)
{
    g_bridge = BridgeState{};
    g_lastRefreshTick = 0;
    g_fastRefreshUntilTick = 0;
    g_superSkillResetLevelSyncUntilTick = 0;
    g_initialGameLevelLoaded = false;
    g_observedCurrentLevelsBySkillId.clear();
    g_observedBaseLevelsBySkillId.clear();
    g_persistentNonNativeSuperSkillLevelsBySkillId.clear();
    g_passiveEffectPatchSnapshotsByEffectPtr.clear();
    g_passiveEffectRuntimeContextsByEffectPtr.clear();
    g_passiveEffectDamageWriteTickBySkillId.clear();
    g_passiveEffectDamageGetterTickBySkillId.clear();
    g_passiveEffectAttackCountGetterTickBySkillId.clear();
    g_loggedMissingSuperSkillConfig = false;
    g_loggedDuplicateSuperSkills = false;
    ClearPendingSuperSkillUpgradePacketRewrite();
    LoadSuperSkillRegistry();
    LoadCustomSkillRoutes();
    LoadNativeSkillInjectionRegistry();

    for (int i = 0; i < SKILL_BAR_TOTAL_SLOTS; ++i)
    {
        g_quickSlots[i] = QuickSlotBinding{};
        g_pendingQuickSlotRestores[i] = PendingQuickSlotRestore{};
    }

    ConfigureIndependentOverlayManager(manager);
    SkillOverlaySourceManagerInitialize(&g_bridge.managerSource, manager);
    SkillOverlaySourceGameInitialize(&g_bridge.gameSource);
    g_bridge.activeSource = &g_bridge.managerSource;
}

void SkillOverlayBridgeShutdown()
{
    ClearPendingSuperSkillUpgradePacketRewrite();
    ClearCustomSkillRoutes();
    ClearSuperSkillRegistry();
    ClearNativeSkillInjectionRegistry();
    g_observedCurrentLevelsBySkillId.clear();
    g_observedBaseLevelsBySkillId.clear();
    g_persistentNonNativeSuperSkillLevelsBySkillId.clear();
    g_passiveEffectPatchSnapshotsByEffectPtr.clear();
    g_passiveEffectRuntimeContextsByEffectPtr.clear();
    g_passiveEffectDamageWriteTickBySkillId.clear();
    g_passiveEffectDamageGetterTickBySkillId.clear();
    g_passiveEffectAttackCountGetterTickBySkillId.clear();
    g_superSkillResetPreviewRevision = 0;
    g_superSkillResetPreviewSpentSp = 0;
    g_superSkillResetPreviewCostMeso = 0;
    g_superSkillResetPreviewCurrentMeso = 0;
    g_superSkillResetPreviewHasCurrentMeso = 0;
    g_superSkillResetPreviewReceiveHookReady = 0;
    g_lastObservedLevelContext = 0;
    g_lastObservedSkillDataMgr = 0;
    for (int i = 0; i < SKILL_BAR_TOTAL_SLOTS; ++i)
        g_pendingQuickSlotRestores[i] = PendingQuickSlotRestore{};
    g_bridge = BridgeState{};
}

void SkillOverlayBridgeSetSavePath(const char* path)
{
    g_bridge.savePath = path;
}

void SkillOverlayBridgeSetPotentialIncreaseAddress(uintptr_t address)
{
    g_localIndependentPotentialIncreaseAddress = address;
    SyncMergedLocalPotentialBufferToExternalAddress();
    WriteLogFmt("[IndependentBuffLocal] potential increase address=0x%08X", (DWORD)address);
}

void SkillOverlayBridgeSetObservedNativeVisibleBuffVisualCount(int count)
{
    if (count < 0)
        count = -1;
    if (count > 9)
        count = 9;
    g_observedNativeVisibleBuffVisualCount = count;
}

void SkillOverlayBridgeGetObservedNativeVisibleSemanticSlots(std::vector<int>& outSlots)
{
    outSlots.clear();
    PruneExpiredNativeVisibleBuffStates();

    std::map<int, std::vector<int> > semanticOrdersBySkillId;
    std::vector<int> pickedOrders;
    pickedOrders.reserve(g_nativeVisibleBuffStates.size());
    for (std::map<unsigned long long, NativeVisibleBuffState>::const_iterator it = g_nativeVisibleBuffStates.begin();
         it != g_nativeVisibleBuffStates.end();
         ++it)
    {
        const NativeVisibleBuffState& state = it->second;
        const int order = FindNativeBuffMaskDefinitionOrder(state.position, state.value);
        if (order < 0)
            continue;
        semanticOrdersBySkillId[state.skillId].push_back(order);
    }

    if (semanticOrdersBySkillId.empty())
        return;

    for (std::map<int, std::vector<int> >::iterator it = semanticOrdersBySkillId.begin();
         it != semanticOrdersBySkillId.end();
         ++it)
    {
        std::vector<int>& orders = it->second;
        std::sort(orders.begin(), orders.end());
        orders.erase(std::unique(orders.begin(), orders.end()), orders.end());
        if (!orders.empty() && orders.front() >= 0)
            pickedOrders.push_back(orders.front());
    }

    if (pickedOrders.empty())
        return;

    std::sort(pickedOrders.begin(), pickedOrders.end());
    pickedOrders.erase(std::unique(pickedOrders.begin(), pickedOrders.end()), pickedOrders.end());

    // Fallback semantic row is "one slot per active native-visible skill".
    // Keep ordering by semantic order, but compact spacing to avoid multi-gap drift
    // when statusBar fixed-child chain is unavailable.
    outSlots.resize(pickedOrders.size());
    for (size_t i = 0; i < outSlots.size(); ++i)
        outSlots[i] = (int)i;

    static DWORD s_lastSemanticSlotLogTick = 0;
    const DWORD nowTick = GetTickCount();
    if (nowTick - s_lastSemanticSlotLogTick > 1000)
    {
        s_lastSemanticSlotLogTick = nowTick;
        std::string skillOrders;
        std::string rawOrders;
        std::string normalizedSlots;
        for (std::map<int, std::vector<int> >::const_iterator it = semanticOrdersBySkillId.begin();
             it != semanticOrdersBySkillId.end();
             ++it)
        {
            if (!skillOrders.empty())
                skillOrders += ",";
            skillOrders += std::to_string(it->first);
            skillOrders += ":[";
            const std::vector<int>& orders = it->second;
            for (size_t orderIndex = 0; orderIndex < orders.size(); ++orderIndex)
            {
                if (orderIndex > 0)
                    skillOrders += "/";
                skillOrders += std::to_string(orders[orderIndex]);
            }
            skillOrders += "]";
        }
        for (size_t i = 0; i < pickedOrders.size(); ++i)
        {
            if (!rawOrders.empty())
                rawOrders += ",";
            rawOrders += std::to_string(pickedOrders[i]);
        }
        for (size_t i = 0; i < outSlots.size(); ++i)
        {
            if (!normalizedSlots.empty())
                normalizedSlots += ",";
            normalizedSlots += std::to_string(outSlots[i]);
        }
        WriteLogFmt("[IndependentBuffSemanticSlots] skills=%s raw=%s normalized=%s nativeStateCount=%d skillCount=%d",
            skillOrders.c_str(),
            rawOrders.c_str(),
            normalizedSlots.c_str(),
            (int)g_nativeVisibleBuffStates.size(),
            (int)semanticOrdersBySkillId.size());
    }
}

void SkillOverlayBridgeSetObservedNativeVisibleBuffAnchorX(int x)
{
    g_observedNativeVisibleBuffAnchorX = x;
}

int SkillOverlayBridgeGetObservedNativeVisibleBuffAnchorX()
{
    return g_observedNativeVisibleBuffAnchorX;
}

void SkillOverlayBridgeSetObservedStatusBarPtr(uintptr_t statusBar)
{
    g_observedStatusBarPtr = statusBar;
}

uintptr_t SkillOverlayBridgeGetObservedStatusBarPtr()
{
    return g_observedStatusBarPtr;
}

void SkillOverlayBridgeSetObservedNativeCursorState(int state)
{
    if (state < -1)
        state = -1;
    if (state > 31)
        state = 31;
    g_observedNativeCursorState = state;
}

int SkillOverlayBridgeGetObservedNativeCursorState()
{
    return g_observedNativeCursorState;
}

void SkillOverlayBridgeObserveExtendedMountSoaringIntent(int mountItemId, int skillId)
{
    if (mountItemId <= 0)
        return;

    int preferredSkillId = 0;
    const int candidateSkillId = skillId > 0 ? skillId : 80001089;
    if (!TryResolveMountedMovementSelectionSkillId(
            mountItemId,
            candidateSkillId,
            0,
            preferredSkillId))
    {
        return;
    }

    ObserveMountedMovementOverrideSelection(mountItemId, preferredSkillId);

    static DWORD s_lastMountedSoaringSelectionLogTick = 0;
    const DWORD nowTick = GetTickCount();
    if (nowTick - s_lastMountedSoaringSelectionLogTick > 1000)
    {
        s_lastMountedSoaringSelectionLogTick = nowTick;
        WriteLogFmt(
            "[MountMoveSelect] soaring mount=%d preferredSkill=%d",
            mountItemId,
            preferredSkillId);
    }
}

void SkillOverlayBridgeBeginFrameObservation()
{
    g_observedSceneFadeAlpha = 0;
}

void SkillOverlayBridgeObserveSceneFadeCandidate(int imageObj, int x, int y, int w, int h, int alpha, int clientW, int clientH)
{
    if (imageObj <= 0 || alpha <= 0 || clientW <= 0 || clientH <= 0 || w <= 0 || h <= 0)
        return;

    const bool coversViewport =
        x <= 16 &&
        y <= 16 &&
        x + w >= clientW - 16 &&
        y + h >= clientH - 16 &&
        w >= clientW - 32 &&
        h >= clientH - 32;
    const bool nearFullscreen =
        x <= 64 &&
        y <= 64 &&
        x + w >= clientW - 64 &&
        y + h >= clientH - 64 &&
        w >= ((clientW * 3) / 4) &&
        h >= ((clientH * 3) / 4);
    if (!coversViewport && !nearFullscreen)
        return;

    const DWORD nowTick = GetTickCount();
    bool matchesCandidate = (g_observedSceneFadeCandidateImageObj == imageObj);
    if (!matchesCandidate)
    {
        if (alpha >= 255)
            return;
        g_observedSceneFadeCandidateImageObj = imageObj;
        matchesCandidate = true;
        WriteLogFmt("[ObservedSceneFade] learn imageObj=0x%08X rect=(%d,%d,%d,%d) alpha=%d mode=%s client=%dx%d",
            imageObj,
            x,
            y,
            x + w,
            y + h,
            alpha,
            coversViewport ? "fullscreen" : "near-fullscreen",
            clientW,
            clientH);
    }

    if (!matchesCandidate)
        return;

    g_observedSceneFadeCandidateLastSeenTick = nowTick;
    if (alpha > g_observedSceneFadeAlpha)
    {
        g_observedSceneFadeAlpha = alpha;
        static DWORD s_lastFadeAlphaLogTick = 0;
        if (nowTick - s_lastFadeAlphaLogTick > 100)
        {
            s_lastFadeAlphaLogTick = nowTick;
            WriteLogFmt("[ObservedSceneFade] active imageObj=0x%08X alpha=%d rect=(%d,%d,%d,%d)",
                imageObj,
                alpha,
                x,
                y,
                x + w,
                y + h);
        }
    }
}

int SkillOverlayBridgeGetObservedSceneFadeAlpha()
{
    const DWORD nowTick = GetTickCount();
    if (g_observedSceneFadeCandidateImageObj &&
        nowTick - g_observedSceneFadeCandidateLastSeenTick > 1500)
    {
        g_observedSceneFadeCandidateImageObj = 0;
        g_observedSceneFadeCandidateLastSeenTick = 0;
    }
    return g_observedSceneFadeAlpha;
}

void SkillOverlayBridgeApplyPotentialBaseValue(uintptr_t writeAddress, int baseValue)
{
    ApplyPotentialBaseValueInternal(writeAddress, baseValue);
}

void SkillOverlayBridgeClearPotentialBaseValues()
{
    ClearPotentialBaseValuesInternal();
}

void SkillOverlayBridgeSetSkillWnd(uintptr_t skillWndThis)
{
    g_bridge.skillWndThis = skillWndThis;
    g_bridge.gameSource.skillWndThis = skillWndThis;
    g_bridge.managerSource.skillWndThis = skillWndThis;
}

void SkillOverlayBridgeSetResetPreviewReceiveHookReady(bool ready)
{
    g_superSkillResetPreviewReceiveHookReady = ready ? 1 : 0;
}

void SkillOverlayBridgeSyncRetroState(RetroSkillRuntimeState& state)
{
    state.hasSuperSkillData = !g_superSkillsBySkillId.empty();
    state.superSkillCarrierSkillId = g_defaultSuperSpCarrierSkillId;
    state.superSkillPoints = ResolveAvailableSuperSkillPointsForCarrier(state.superSkillCarrierSkillId);
    state.superSkillResetPreviewSpentSp = (int)g_superSkillResetPreviewSpentSp;
    state.superSkillResetPreviewCostMeso = (int)g_superSkillResetPreviewCostMeso;
    state.superSkillResetPreviewCurrentMeso = (int)g_superSkillResetPreviewCurrentMeso;
    state.superSkillResetPreviewHasCurrentMeso = g_superSkillResetPreviewHasCurrentMeso != 0;
    state.superSkillResetPreviewRevision = (unsigned int)g_superSkillResetPreviewRevision;
    if ((state.superSkillResetConfirmVisible || state.superSkillResetConfirmOpenRequested) &&
        state.superSkillResetConfirmCostPending &&
        state.superSkillResetPreviewRevision != state.superSkillResetConfirmPreviewRequestRevision)
    {
        state.superSkillResetConfirmSpentSp = state.superSkillResetPreviewSpentSp;
        state.superSkillResetConfirmCostMeso = state.superSkillResetPreviewCostMeso;
        state.superSkillResetConfirmCurrentMeso = state.superSkillResetPreviewCurrentMeso;
        state.superSkillResetConfirmHasCurrentMeso = state.superSkillResetPreviewHasCurrentMeso;
        state.superSkillResetConfirmCostPending = false;
        WriteLogFmt("[SkillBridge] reset preview applied requestRev=%u newRev=%u spentSp=%d cost=%d meso=%d hasMeso=%d",
            state.superSkillResetConfirmPreviewRequestRevision,
            state.superSkillResetPreviewRevision,
            state.superSkillResetPreviewSpentSp,
            state.superSkillResetPreviewCostMeso,
            state.superSkillResetPreviewCurrentMeso,
            state.superSkillResetPreviewHasCurrentMeso ? 1 : 0);
    }

    // 定期从游戏刷新技能等级
    DWORD now = GetTickCount();
    const DWORD refreshInterval = (now < g_fastRefreshUntilTick) ? kPendingRefreshIntervalMs : kRefreshIntervalMs;
    if (now - g_lastRefreshTick >= refreshInterval)
    {
        g_lastRefreshTick = now;
        // 获取 manager 指针
        if (g_bridge.managerSource.userData)
        {
            struct MSS { SkillManager* m; };
            MSS* mss = static_cast<MSS*>(g_bridge.managerSource.userData);
            if (mss->m)
                RefreshSkillLevelsFromGame(mss->m);
        }
    }

    SkillManager* manager = GetBridgeManager();
    if (manager && g_superSkillsBySkillId.empty() && now - g_lastMissingConfigRetryTick >= kMissingConfigRetryIntervalMs)
    {
        g_lastMissingConfigRetryTick = now;
        LoadSuperSkillRegistry();
        LoadCustomSkillRoutes();
        LoadNativeSkillInjectionRegistry();

        if (!g_superSkillsBySkillId.empty())
        {
            const int currentTab = manager->currentTab;
            ConfigureIndependentOverlayManager(manager);
            if (currentTab >= 0 && currentTab < manager->tabCount)
                manager->currentTab = currentTab;
            WriteLogFmt("[SuperSkill] overlay rebuilt: config retry loaded");
        }
    }

    if (manager && !g_superSkillsBySkillId.empty() && UpdateOverlayLearnedVisibilitySnapshot())
    {
        const int currentTab = manager->currentTab;
        ConfigureIndependentOverlayManager(manager);
        if (currentTab >= 0 && currentTab < manager->tabCount)
            manager->currentTab = currentTab;
        WriteLogFmt("[SuperSkill] overlay rebuilt: showInSuperWhenLearned visibility changed");
    }

    int currentJobId = 0;
    if (manager &&
        !g_superSkillsBySkillId.empty() &&
        TryReadCurrentPlayerJobId(currentJobId) &&
        currentJobId != g_lastOverlayConfiguredJobId)
    {
        const int currentTab = manager->currentTab;
        ConfigureIndependentOverlayManager(manager);
        if (currentTab >= 0 && currentTab < manager->tabCount)
            manager->currentTab = currentTab;
        WriteLogFmt("[SuperSkill] overlay rebuilt: current job changed job=%d", currentJobId);
    }

    TryRestorePendingQuickSlots();

    SkillOverlaySource* preferred = ResolveActiveSource();
    if (preferred && SkillOverlaySourceSyncRetroState(preferred, state))
    {
        g_bridge.activeSource = preferred;
        LogSourceSwitchIfNeeded(preferred->name);

        // Sync quick slot bindings into state
        for (int i = 0; i < SKILL_BAR_TOTAL_SLOTS; ++i)
            state.quickSlots[i] = g_quickSlots[i];

        ApplySuperSkillStateToEntries(state.passiveSkills, state);
        ApplySuperSkillStateToEntries(state.activeSkills, state);
        ApplyConfiguredPassiveBonusTooltipAugments(state);

        static DWORD s_lastSuperSpSyncLogTick = 0;
        const DWORD nowTick = GetTickCount();
        if (EnableSuperSkillSyncStateDiagnosticLogs() &&
            state.superSkillCarrierSkillId > 0 &&
            nowTick - s_lastSuperSpSyncLogTick > 1000)
        {
            s_lastSuperSpSyncLogTick = nowTick;
            WriteLogFmt("[SuperSkill] sync state carrier=%d points=%d observedBase=%d observedCurrent=%d base=%d current=%d",
                state.superSkillCarrierSkillId,
                state.superSkillPoints,
                GetObservedBaseSkillLevel(state.superSkillCarrierSkillId),
                GetObservedCurrentSkillLevel(state.superSkillCarrierSkillId),
                GameGetBaseSkillLevel(state.superSkillCarrierSkillId),
                GameGetSkillLevel(state.superSkillCarrierSkillId));
        }

        return;
    }
}

void SkillOverlayBridgeConfigureHooks(RetroSkillBehaviorHooks& hooks)
{
    hooks.userData = nullptr;
    hooks.onTabAction = OnBridgeTabAction;
    hooks.onPlusAction = OnBridgePlusAction;
    hooks.onInitPreviewAction = OnBridgeInitPreviewAction;
    hooks.onInitAction = OnBridgeInitAction;
    hooks.onSkillDragBegin = OnBridgeSkillDragBegin;
    hooks.onSkillDragEnd = OnBridgeSkillDragEnd;
    hooks.onSkillUse = OnBridgeSkillUse;
}

const char* SkillOverlayBridgeGetActiveSourceName()
{
    if (g_bridge.activeSource && g_bridge.activeSource->name)
        return g_bridge.activeSource->name;
    return "unset";
}

// ============================================================================
// Quick Slot API
// ============================================================================

const QuickSlotBinding* SkillOverlayBridgeGetQuickSlots()
{
    return g_quickSlots;
}

int SkillOverlayBridgeGetQuickSlotSkillId(int slotIndex)
{
    if (slotIndex < 0 || slotIndex >= SKILL_BAR_TOTAL_SLOTS)
        return 0;
    return g_quickSlots[slotIndex].skillId;
}

void SkillOverlayBridgeSetQuickSlot(int slotIndex, int skillId, int skillTab, const char* skillName)
{
    if (slotIndex < 0 || slotIndex >= SKILL_BAR_TOTAL_SLOTS)
        return;
    ClearPendingQuickSlotRestore(slotIndex);
    g_quickSlots[slotIndex].skillId = skillId;
    g_quickSlots[slotIndex].skillTab = skillTab;
    if (skillName)
        g_quickSlots[slotIndex].skillName = skillName;
    else
    {
        std::string name;
        if (SkillLocalDataGetName(skillId, name))
            g_quickSlots[slotIndex].skillName = name;
        else
        {
            char buf[16] = {};
            sprintf_s(buf, "#%07d", skillId);
            g_quickSlots[slotIndex].skillName = buf;
        }
    }
}

void SkillOverlayBridgeClearQuickSlot(int slotIndex)
{
    if (slotIndex < 0 || slotIndex >= SKILL_BAR_TOTAL_SLOTS)
        return;
    ClearPendingQuickSlotRestore(slotIndex);
    g_quickSlots[slotIndex] = QuickSlotBinding{};
}

// ============================================================================
// Persistence — save/load skill levels + quick slot bindings
// ============================================================================

bool SkillOverlayBridgeSaveState(const char* path)
{
    if (!path)
        return false;

    SkillOverlaySource* src = ResolveActionSource();
    if (!src || !src->userData)
        return false;

    // Build JSON manually
    std::string json;
    json.reserve(2048);
    json += "{\n";

    SkillManager* mgr = GetBridgeManager();

    // Save skill levels per tab
    json += "  \"tabs\": [\n";
    for (int t = 0; mgr && t < mgr->tabCount; ++t)
    {
        SkillTab* tab = &mgr->tabs[t];

        json += "    {\n";
        char buf[64];
        sprintf_s(buf, "      \"tabIndex\": %d,\n", t);
        json += buf;
        json += "      \"skills\": [\n";

        for (int i = 0; i < tab->count; ++i)
        {
            const SkillItem& item = tab->skills[i];
            sprintf_s(buf, "        { \"skillId\": %d, \"level\": %d }", item.skillID, item.level);
            json += buf;
            if (i < tab->count - 1) json += ",";
            json += "\n";
        }

        json += "      ]\n";
        json += "    }";
        if (mgr && t < mgr->tabCount - 1) json += ",";
        json += "\n";
    }
    json += "  ],\n";

    // Save quick slot bindings
    json += "  \"quickSlots\": [\n";
    for (int i = 0; i < SKILL_BAR_TOTAL_SLOTS; ++i)
    {
        char buf[128];
        sprintf_s(buf, "    { \"slot\": %d, \"skillId\": %d, \"tab\": %d }",
            i, g_quickSlots[i].skillId, g_quickSlots[i].skillTab);
        json += buf;
        if (i < SKILL_BAR_TOTAL_SLOTS - 1) json += ",";
        json += "\n";
    }
    json += "  ]\n";

    json += "}\n";

    bool ok = WriteTextFile(path, json.c_str());
    if (ok)
        WriteLogFmt("[SkillBridge] state saved to %s", path);
    else
        WriteLogFmt("[SkillBridge] WARN: failed to save state to %s", path);
    return ok;
}

bool SkillOverlayBridgeLoadState(const char* path)
{
    if (!path)
        return false;

    std::string json;
    if (!ReadTextFile(path, json))
    {
        WriteLogFmt("[SkillBridge] no saved state at %s (first run?)", path);
        return false;
    }

    for (int i = 0; i < SKILL_BAR_TOTAL_SLOTS; ++i)
        g_pendingQuickSlotRestores[i] = PendingQuickSlotRestore{};

    // Find manager
    SkillManager* mgr = nullptr;
    if (g_bridge.managerSource.userData)
    {
        struct MSS { SkillManager* m; };
        MSS* mss = static_cast<MSS*>(g_bridge.managerSource.userData);
        mgr = mss->m;
    }

    // Skill levels now come from live game state. Keep loading the file for
    // quickslot bindings, but do not restore overlay-only levels.

    // Load quick slot bindings
    for (int i = 0; i < SKILL_BAR_TOTAL_SLOTS; ++i)
    {
        size_t slotBegin = 0, slotEnd = 0;
        if (!FindArrayElement(json, "quickSlots", i, slotBegin, slotEnd))
            continue;

        std::string slotJson = json.substr(slotBegin, slotEnd - slotBegin);
        int skillId = 0, tab = -1;
        ParseJsonInt(slotJson, "skillId", skillId);
        ParseJsonInt(slotJson, "tab", tab);

        if (skillId > 0)
        {
            SkillOverlayBridgeSetQuickSlot(i, skillId, tab, nullptr);
            const bool nativeOk = ::SkillOverlayBridgeAssignSkillToQuickSlot(i, skillId);
            if (!nativeOk)
                QueuePendingQuickSlotRestore(i, skillId, "load_state");
            WriteLogFmt("[SkillBridge] restore quickSlot[%d] skillId=%d native=%s",
                i, skillId, nativeOk ? "OK" : "FAIL");
        }
    }

    if (mgr)
        RefreshSkillLevelsFromGame(mgr);

    WriteLogFmt("[SkillBridge] state loaded from %s", path);
    return true;
}

void SkillOverlayBridgeFilterNativeSkillWindow(uintptr_t skillWndThis)
{
    static uintptr_t s_lastLoggedSkillWnd = 0;
    static int s_lastLoggedRemovedCount = -1;
    static int s_lastLoggedInjectedCount = -1;
    static DWORD s_lastLogTick = 0;

    if (!skillWndThis)
        return;
    if (g_superSkillsBySkillId.empty() && g_nativeSkillInjectionsBySkillId.empty())
        return;

    uintptr_t listView = 0;
    uintptr_t pageObj = 0;
    uintptr_t wndEntries = 0;
    int listCount = 0;
    int firstVisibleIndex = 0;
    if (!SafeReadValue(skillWndThis + kOff_SkillWnd_ListView, listView) || !listView)
        return;
    if (!SafeReadValue(skillWndThis + kOff_SkillWnd_PageObj, pageObj) || !pageObj)
        return;
    if (!SafeReadValue(skillWndThis + kOff_SkillWnd_Entries, wndEntries) || !wndEntries)
        return;
    if (!SafeReadValue(listView + kOff_ListView_Count, listCount))
        return;
    SafeReadValue(pageObj + kOff_PageObj_FirstVisibleIndex, firstVisibleIndex);

    const bool usingInjectedEntries =
        (g_nativeInjectedEntriesPtr != 0) &&
        (g_nativeInjectedSkillWnd == skillWndThis) &&
        (wndEntries == g_nativeInjectedEntriesPtr) &&
        (g_nativeOriginalEntriesPtr != 0);

    uintptr_t sourceEntries = usingInjectedEntries ? g_nativeOriginalEntriesPtr : wndEntries;
    if (!sourceEntries)
        return;

    if (!usingInjectedEntries)
    {
        g_nativeOriginalEntriesPtr = sourceEntries;
        if (g_nativeInjectedSkillWnd == skillWndThis && g_nativeInjectedEntriesPtr != 0 && wndEntries != g_nativeInjectedEntriesPtr)
        {
            g_nativeInjectedEntriesPtr = 0;
            g_nativeInjectedSkillWnd = 0;
        }
    }

    int entryArrayCount = 0;
    if (sourceEntries >= sizeof(int))
        SafeReadValue(sourceEntries - sizeof(int), entryArrayCount);

    const bool listCountValid = (listCount > 0 && listCount <= kMaxReasonableSkillCount);
    const bool arrayCountValid = (entryArrayCount > 0 && entryArrayCount <= kMaxReasonableSkillCount);
    int totalCount = 0;
    if (arrayCountValid)
    {
        // The entries array has its own element count stored immediately before
        // the data block. That count tracks the real skill list length.
        // listView->count is the native view-row count in current clients, and
        // writing filtered totals back into it leaves partially broken rows
        // behind (for example a dangling '+' button).
        totalCount = entryArrayCount;
    }
    else if (listCountValid)
    {
        totalCount = listCount;
    }

    if (totalCount <= 0)
    {
        if (usingInjectedEntries)
            SafeWriteValue(skillWndThis + kOff_SkillWnd_Entries, sourceEntries);
        g_nativeInjectedEntriesPtr = 0;
        g_nativeInjectedSkillWnd = 0;
        return;
    }

    int removedCount = 0;
    int removedBeforeFirstVisible = 0;

    // Diagnostic: log all skill IDs found in entries (throttled)
    static DWORD s_lastDiagTick = 0;
    const DWORD diagNow = GetTickCount();
    const bool doDiag = (diagNow - s_lastDiagTick > 5000);
    if (doDiag)
    {
        s_lastDiagTick = diagNow;
        char diagBuf[1024] = {};
        int diagLen = 0;
        diagLen += sprintf_s(diagBuf + diagLen, sizeof(diagBuf) - diagLen,
            "[FilterDiag] entries=0x%08X count=%d arrayCount=%d ids=[",
            (DWORD)sourceEntries, totalCount, entryArrayCount);
        for (int d = 0; d < totalCount && d < 20; ++d)
        {
            const uintptr_t dSlot = sourceEntries + static_cast<uintptr_t>(d) * 8;
            int dId = 0;
            SafeReadValue(dSlot, dId);
            if (diagLen < (int)sizeof(diagBuf) - 16)
                diagLen += sprintf_s(diagBuf + diagLen, sizeof(diagBuf) - diagLen, "%s%d", d > 0 ? "," : "", dId);
        }
        if (diagLen < (int)sizeof(diagBuf) - 4)
            diagLen += sprintf_s(diagBuf + diagLen, sizeof(diagBuf) - diagLen, "]");
        WriteLogFmt("%s", diagBuf);
    }

    std::vector<NativeSkillSlot> visibleSlots;
    visibleSlots.reserve(static_cast<size_t>(totalCount) + g_nativeSkillInjectionsBySkillId.size());

    for (int i = 0; i < totalCount; ++i)
    {
        const uintptr_t rowSlot = sourceEntries + static_cast<uintptr_t>(i) * 8;
        int slotSkillId = 0;
        uintptr_t rowData = 0;
        if (!SafeReadValue(rowSlot, slotSkillId))
            continue;
        if (!SafeReadValue(rowSlot + 4, rowData) || !rowData)
            continue;

        int rowSkillId = 0;
        if (!SafeReadValue(rowData, rowSkillId) || rowSkillId == 0)
            rowSkillId = slotSkillId;

        SuperSkillDefinition definition = {};
        const bool hideSkill =
            FindSuperSkillDefinition(rowSkillId, definition) &&
            ShouldHideSuperSkillInNativeList(rowSkillId, definition);
        if (hideSkill)
        {
            ++removedCount;
            if (i < firstVisibleIndex)
                ++removedBeforeFirstVisible;
            continue;
        }

        NativeSkillSlot slot = {};
        slot.slotSkillId = slotSkillId;
        slot.rowData = rowData;
        visibleSlots.push_back(slot);
    }

    // Inject skills from native_skill_injections.json.
    int injectedCount = 0;
    if (!g_nativeSkillInjectionsBySkillId.empty() && !visibleSlots.empty())
    {
        const int neededBlocks = (int)g_nativeSkillInjectionsBySkillId.size();
        if ((int)g_nativeInjectedRowBlocks.size() < neededBlocks)
            g_nativeInjectedRowBlocks.resize(neededBlocks);
        g_nativeInjectedNames.clear();

        int injectIndex = 0;
        for (std::map<int, NativeSkillInjectionDefinition>::const_iterator it = g_nativeSkillInjectionsBySkillId.begin();
             it != g_nativeSkillInjectionsBySkillId.end();
             ++it)
        {
            const NativeSkillInjectionDefinition& def = it->second;
            if (!def.enabled || def.skillId <= 0)
                continue;

            HiddenSkillDefinition hiddenDefinition = {};
            if (FindHiddenSkillDefinition(def.skillId, hiddenDefinition) &&
                hiddenDefinition.hideFromNativeSkillWnd)
            {
                continue;
            }

            SuperSkillDefinition superDefinition = {};
            if (FindSuperSkillDefinition(def.skillId, superDefinition) &&
                ShouldHideSuperSkillInNativeList(def.skillId, superDefinition))
            {
                continue;
            }

            // Already visible? Skip duplicates.
            bool alreadyPresent = false;
            for (size_t k = 0; k < visibleSlots.size(); ++k)
            {
                if (visibleSlots[k].slotSkillId == def.skillId)
                {
                    alreadyPresent = true;
                    break;
                }
            }
            if (alreadyPresent)
                continue;

            // Find donor rowData among currently visible rows.
            const int donorId = (def.donorSkillId > 0) ? def.donorSkillId : def.skillId;
            uintptr_t donorRowData = 0;
            for (size_t k = 0; k < visibleSlots.size(); ++k)
            {
                const NativeSkillSlot& slot = visibleSlots[k];
                int rowSkillId = 0;
                if (!slot.rowData || !SafeReadValue(slot.rowData, rowSkillId) || rowSkillId <= 0)
                    rowSkillId = slot.slotSkillId;
                if (slot.slotSkillId == donorId || rowSkillId == donorId)
                {
                    donorRowData = slot.rowData;
                    break;
                }
            }
            if (!donorRowData || SafeIsBadReadPtr((void*)donorRowData, kNativeSkillRowCloneBytes))
                continue;

            std::vector<unsigned char>& block = g_nativeInjectedRowBlocks[injectIndex];
            block.resize(kNativeSkillRowCloneBytes, 0);

            memcpy(block.data(), (const void*)donorRowData, kNativeSkillRowCloneBytes);

            *(int*)(block.data()) = def.skillId;

            std::string skillName;
            if (!SkillLocalDataGetName(def.skillId, skillName) || skillName.empty())
            {
                char fallbackName[16] = {};
                sprintf_s(fallbackName, "#%07d", def.skillId);
                skillName = fallbackName;
            }
            if (FindSuperSkillDefinition(def.skillId, superDefinition))
                skillName = BuildSuperSkillDisplayName(skillName, superDefinition);
            g_nativeInjectedNames.push_back(skillName);

            if (block.size() >= 8)
            {
                const uintptr_t namePtr = reinterpret_cast<uintptr_t>(g_nativeInjectedNames.back().c_str());
                *(uintptr_t*)(block.data() + 4) = namePtr;
            }

            NativeSkillSlot injectedSlot = {};
            injectedSlot.slotSkillId = def.skillId;
            injectedSlot.rowData = reinterpret_cast<uintptr_t>(block.data());
            visibleSlots.push_back(injectedSlot);

            ++injectIndex;
        }

        injectedCount = injectIndex;
    }

    const int finalCount = static_cast<int>(visibleSlots.size());
    const bool hasViewTransform = (removedCount > 0 || injectedCount > 0);

    if (hasViewTransform)
    {
        const size_t blockSize = sizeof(int) + static_cast<size_t>(finalCount) * 8;
        g_nativeInjectedEntriesBlock.resize(blockSize, 0);
        *(int*)g_nativeInjectedEntriesBlock.data() = finalCount;

        const uintptr_t injectedEntries = reinterpret_cast<uintptr_t>(g_nativeInjectedEntriesBlock.data() + sizeof(int));
        for (int i = 0; i < finalCount; ++i)
        {
            const uintptr_t slotAddr = injectedEntries + static_cast<uintptr_t>(i) * 8;
            SafeWriteValue(slotAddr, visibleSlots[i].slotSkillId);
            SafeWriteValue(slotAddr + 4, visibleSlots[i].rowData);
        }

        g_nativeOriginalEntriesPtr = sourceEntries;
        g_nativeInjectedEntriesPtr = injectedEntries;
        g_nativeInjectedSkillWnd = skillWndThis;

        SafeWriteValue(skillWndThis + kOff_SkillWnd_Entries, injectedEntries);
        SafeWriteValue(injectedEntries - sizeof(int), finalCount);
    }
    else
    {
        if (usingInjectedEntries)
            SafeWriteValue(skillWndThis + kOff_SkillWnd_Entries, sourceEntries);

        g_nativeOriginalEntriesPtr = sourceEntries;
        g_nativeInjectedEntriesPtr = 0;
        g_nativeInjectedSkillWnd = 0;
    }

    int adjustedFirstVisibleIndex = firstVisibleIndex;
    if (removedBeforeFirstVisible > 0)
        adjustedFirstVisibleIndex -= removedBeforeFirstVisible;
    if (adjustedFirstVisibleIndex < 0)
        adjustedFirstVisibleIndex = 0;

    if (finalCount <= 0)
        SafeWriteValue(pageObj + kOff_PageObj_FirstVisibleIndex, 0);
    else if (adjustedFirstVisibleIndex >= finalCount)
        SafeWriteValue(pageObj + kOff_PageObj_FirstVisibleIndex, finalCount - 1);
    else if (adjustedFirstVisibleIndex != firstVisibleIndex)
        SafeWriteValue(pageObj + kOff_PageObj_FirstVisibleIndex, adjustedFirstVisibleIndex);

    const DWORD now = GetTickCount();
    if (s_lastLoggedSkillWnd != skillWndThis ||
        s_lastLoggedRemovedCount != removedCount ||
        s_lastLoggedInjectedCount != injectedCount ||
        now - s_lastLogTick > 3000)
    {
        s_lastLoggedSkillWnd = skillWndThis;
        s_lastLoggedRemovedCount = removedCount;
        s_lastLoggedInjectedCount = injectedCount;
        s_lastLogTick = now;
        WriteLogFmt(
            "[SuperSkill] native filter removed=%d injected=%d kept=%d final=%d source=0x%08X view=0x%08X skillWnd=0x%08X",
            removedCount,
            injectedCount,
            finalCount - injectedCount,
            finalCount,
            (DWORD)sourceEntries,
            (DWORD)g_nativeInjectedEntriesPtr,
            (DWORD)skillWndThis);
    }
}

void SkillOverlayBridgeReloadNativeInjectionConfig()
{
    LoadNativeSkillInjectionRegistry();
}

bool SkillOverlayBridgeShouldHideFromNativeList(int skillId)
{
    if (skillId <= 0)
        return false;

    HiddenSkillDefinition hiddenDefinition = {};
    if (FindHiddenSkillDefinition(skillId, hiddenDefinition) &&
        hiddenDefinition.hideFromNativeSkillWnd)
    {
        return true;
    }

    if (g_superSkillsBySkillId.empty())
        return false;
    SuperSkillDefinition def = {};
    if (!FindSuperSkillDefinition(skillId, def))
        return false;
    return ShouldHideSuperSkillInNativeList(skillId, def);
}

uintptr_t SkillOverlayBridgePrepareLocalIndependentPotentialBuffer(uintptr_t sourcePtr)
{
    return PrepareLocalIndependentPotentialCombined(sourcePtr);
}

uintptr_t SkillOverlayBridgePrepareLocalIndependentPotentialDisplayBuffer(uintptr_t sourcePtr)
{
    return PrepareLocalIndependentPotentialDisplayCombined(sourcePtr);
}

int SkillOverlayBridgeGetLocalIndependentPotentialDeltaValue(int offset)
{
    RefreshIndependentBuffRuntimeOwnerBinding();
    if (offset < 0 || offset + static_cast<int>(sizeof(int)) > kLocalIndependentPotentialBufferBytes)
        return 0;

    const size_t index = static_cast<size_t>(offset / static_cast<int>(sizeof(int)));
    if (index >= g_localIndependentPotentialDeltaBuffer.size())
        return 0;

    return g_localIndependentPotentialDeltaBuffer[index];
}

bool SkillOverlayBridgeHasLocalIndependentPotentialBonuses()
{
    RefreshIndependentBuffRuntimeOwnerBinding();
    return !g_activeLocalIndependentPotentialBySkillId.empty();
}

bool SkillOverlayBridgeHasIndependentBuffOverlayEntries()
{
    RefreshIndependentBuffRuntimeOwnerBinding();
    if (!IsIndependentBuffGameplaySceneActive())
        return false;

    const DWORD now = GetTickCount();
    for (std::map<int, IndependentBuffOverlayState>::iterator it = g_independentBuffVirtualStates.begin();
         it != g_independentBuffVirtualStates.end();)
    {
        const IndependentBuffOverlayState& state = it->second;
        if (state.durationMs > 0)
        {
            const DWORD elapsed = now - state.startTick;
            if (elapsed >= static_cast<DWORD>(state.durationMs))
            {
                it = g_independentBuffVirtualStates.erase(it);
                continue;
            }
        }

        return true;
    }

    for (std::map<int, IndependentBuffOverlayState>::iterator it = g_independentBuffOverlayStates.begin();
         it != g_independentBuffOverlayStates.end();)
    {
        const IndependentBuffOverlayState& state = it->second;
        if (state.durationMs > 0)
        {
            const DWORD elapsed = now - state.startTick;
            if (elapsed >= static_cast<DWORD>(state.durationMs))
            {
                it = g_independentBuffOverlayStates.erase(it);
                continue;
            }
        }

        return true;
    }

    return false;
}

int SkillOverlayBridgeGetNativeVisibleBuffVisualCount()
{
    return CountActiveNativeVisibleBuffStates();
}

namespace
{
    bool TrySendIndependentBuffCancelPacketInternal(int skillId)
    {
        if (skillId <= 0)
            return false;

        WriteLogFmt("[IndependentBuffOverlay] cancel send deferred skillId=%d (direct raw send disabled to avoid crash)",
            skillId);
        return false;
    }
}

void SkillOverlayBridgeGetIndependentBuffOverlayEntries(std::vector<IndependentBuffOverlayEntry>& outEntries)
{
    RefreshIndependentBuffRuntimeOwnerBinding();
    outEntries.clear();
    if (!IsIndependentBuffGameplaySceneActive())
        return;

    const DWORD now = GetTickCount();
    struct OrderedOverlayEntry
    {
        IndependentBuffOverlayEntry entry;
        int slotIndex = 0;
        unsigned long long activationOrder = 0;
    };
    std::vector<OrderedOverlayEntry> orderedEntries;
    auto populateEntryMetadata = [](IndependentBuffOverlayEntry& entry)
    {
        std::string name;
        if (SkillLocalDataGetName(entry.skillId, name) && !name.empty())
            entry.name = name;

        int maxLevel = 0;
        if (SkillLocalDataGetMaxLevel(entry.skillId, maxLevel) && maxLevel > 0)
            entry.maxLevel = maxLevel;

        SkillLocalTooltipText tooltip = {};
        if (SkillLocalDataGetTooltipText(entry.skillId, tooltip))
        {
            entry.tooltipPreview = tooltip.previewUtf8;
            entry.tooltipDescription = tooltip.descriptionUtf8;
            entry.tooltipDetail = tooltip.detailUtf8;

            const std::string rawTooltipPreview = entry.tooltipPreview;
            const std::string rawTooltipDescription = entry.tooltipDescription;
            const std::string rawTooltipDetail = entry.tooltipDetail;
            const int currentTooltipLevel = (std::max)(1, GetTrackedSkillLevel(entry.skillId));
            std::string formattedText;
            if (SkillLocalDataFormatTooltipText(entry.skillId, currentTooltipLevel, rawTooltipPreview, formattedText))
                entry.tooltipPreview = formattedText;
            if (SkillLocalDataFormatTooltipText(entry.skillId, currentTooltipLevel, rawTooltipDescription, formattedText))
                entry.tooltipDescription = formattedText;
            if (SkillLocalDataFormatTooltipText(entry.skillId, currentTooltipLevel, rawTooltipDetail, formattedText))
                entry.tooltipDetail = formattedText;
        }
    };

    if (!g_independentBuffVirtualStates.empty())
    {
        std::vector<IndependentBuffOverlayState> virtualStates;
        for (std::map<int, IndependentBuffOverlayState>::iterator it = g_independentBuffVirtualStates.begin();
             it != g_independentBuffVirtualStates.end();)
        {
            const IndependentBuffOverlayState& state = it->second;
            if (state.durationMs > 0)
            {
                const DWORD elapsed = now - state.startTick;
                if (elapsed >= static_cast<DWORD>(state.durationMs))
                {
                    it = g_independentBuffVirtualStates.erase(it);
                    continue;
                }
            }
            virtualStates.push_back(state);
            ++it;
        }
        PruneExpiredNativeVisibleBuffStates();
        std::vector<NativeVisibleBuffState> nativeStates;
        nativeStates.reserve(g_nativeVisibleBuffStates.size());
        for (std::map<unsigned long long, NativeVisibleBuffState>::const_iterator it = g_nativeVisibleBuffStates.begin();
             it != g_nativeVisibleBuffStates.end();
             ++it)
        {
            nativeStates.push_back(it->second);
        }

        std::sort(virtualStates.begin(), virtualStates.end(),
            [](const IndependentBuffOverlayState& left, const IndependentBuffOverlayState& right)
            {
                if (left.activationOrder != right.activationOrder)
                    return left.activationOrder < right.activationOrder;
                return left.skillId < right.skillId;
            });
        std::sort(nativeStates.begin(), nativeStates.end(),
            [](const NativeVisibleBuffState& left, const NativeVisibleBuffState& right)
            {
                if (left.activationOrder != right.activationOrder)
                    return left.activationOrder < right.activationOrder;
                if (left.startTick != right.startTick)
                    return left.startTick < right.startTick;
                if (left.skillId != right.skillId)
                    return left.skillId < right.skillId;
                if (left.position != right.position)
                    return left.position < right.position;
                return left.value < right.value;
            });

        size_t virtualIndex = 0;
        size_t nativeIndex = 0;
        while (virtualIndex < virtualStates.size() || nativeIndex < nativeStates.size())
        {
            bool useVirtual = false;
            if (virtualIndex < virtualStates.size() && nativeIndex < nativeStates.size())
            {
                const IndependentBuffOverlayState& virtualState = virtualStates[virtualIndex];
                const NativeVisibleBuffState& nativeState = nativeStates[nativeIndex];
                if (virtualState.activationOrder != nativeState.activationOrder)
                {
                    useVirtual = virtualState.activationOrder < nativeState.activationOrder;
                }
                else if (virtualState.startTick != nativeState.startTick)
                {
                    useVirtual = virtualState.startTick < nativeState.startTick;
                }
                else
                {
                    useVirtual = virtualState.skillId < nativeState.skillId;
                }
            }
            else if (virtualIndex < virtualStates.size())
            {
                useVirtual = true;
            }

            if (!useVirtual)
            {
                ++nativeIndex;
                continue;
            }

            const IndependentBuffOverlayState& state = virtualStates[virtualIndex];
            int remainingMs = 0;
            if (state.durationMs > 0)
            {
                const DWORD elapsed = now - state.startTick;
                remainingMs = state.durationMs - static_cast<int>(elapsed);
                if (remainingMs < 0)
                    remainingMs = 0;
            }

            IndependentBuffOverlayEntry entry = {};
            entry.skillId = state.skillId;
            entry.iconSkillId = state.iconSkillId > 0 ? state.iconSkillId : state.skillId;
            entry.slotIndex = (int)virtualIndex;
            entry.remainingMs = remainingMs;
            entry.totalDurationMs = state.durationMs;
            entry.cancelable = state.cancelable;
            populateEntryMetadata(entry);

            OrderedOverlayEntry ordered = {};
            ordered.entry = entry;
            ordered.slotIndex = entry.slotIndex;
            ordered.activationOrder = state.activationOrder;
            orderedEntries.push_back(ordered);
            ++virtualIndex;
        }
    }
    else
    {

        for (std::map<int, IndependentBuffOverlayState>::iterator it = g_independentBuffOverlayStates.begin();
             it != g_independentBuffOverlayStates.end();)
        {
            const IndependentBuffOverlayState& state = it->second;
            int remainingMs = 0;
            if (state.durationMs > 0)
            {
                const DWORD elapsed = now - state.startTick;
                remainingMs = state.durationMs - static_cast<int>(elapsed);
                if (remainingMs <= 0)
                {
                    it = g_independentBuffOverlayStates.erase(it);
                    continue;
                }
            }

            IndependentBuffOverlayEntry entry = {};
            entry.skillId = state.skillId;
            entry.iconSkillId = state.iconSkillId > 0 ? state.iconSkillId : state.skillId;
            entry.slotIndex = state.slotIndex >= 0 ? state.slotIndex : 0;
            entry.remainingMs = remainingMs;
            entry.totalDurationMs = state.durationMs;
            entry.cancelable = state.cancelable;
            populateEntryMetadata(entry);

            OrderedOverlayEntry ordered = {};
            ordered.entry = entry;
            ordered.slotIndex = entry.slotIndex;
            ordered.activationOrder = state.activationOrder;
            orderedEntries.push_back(ordered);
            ++it;
        }
    }

    std::sort(orderedEntries.begin(), orderedEntries.end(),
        [](const OrderedOverlayEntry& left, const OrderedOverlayEntry& right)
        {
            if (left.activationOrder != right.activationOrder)
                return left.activationOrder > right.activationOrder;
            if (left.slotIndex != right.slotIndex)
                return left.slotIndex < right.slotIndex;
            return left.entry.skillId < right.entry.skillId;
        });

    outEntries.reserve(orderedEntries.size());
    for (size_t i = 0; i < orderedEntries.size(); ++i)
    {
        IndependentBuffOverlayEntry entry = orderedEntries[i].entry;
        entry.slotIndex = (int)i;
        outEntries.push_back(entry);
    }
}

bool SkillOverlayBridgeCancelIndependentBuff(int skillId)
{
    if (skillId <= 0)
        return false;

    SuperSkillDefinition definition = {};
    if (!TryResolveIndependentBuffDefinitionForCancel(skillId, definition))
    {
        WriteLogFmt("[IndependentBuffOverlay] cancel local-only miss skillId=%d", skillId);
        return false;
    }

    int cancelSkillId = definition.independentNativeDisplaySkillId > 0
        ? definition.independentNativeDisplaySkillId
        : definition.skillId;
    if (ShouldSuppressNativeVisibleBuffSkillId(cancelSkillId))
        cancelSkillId = definition.skillId;
    const DWORD now = GetTickCount();
    std::map<int, DWORD>::const_iterator recentCancelIt = g_recentIndependentBuffClientCancelTickBySkillId.find(definition.skillId);
    if (recentCancelIt != g_recentIndependentBuffClientCancelTickBySkillId.end() &&
        now - recentCancelIt->second <= 150)
    {
        WriteLogFmt("[IndependentBuffOverlay] cancel request dedup skillId=%d delta=%u",
            definition.skillId,
            (unsigned int)(now - recentCancelIt->second));
        return false;
    }

    g_recentIndependentBuffClientCancelTickBySkillId[definition.skillId] = now;
    const bool sentCancelPacket = TrySendIndependentBuffCancelPacketInternal(cancelSkillId);
    ClearIndependentBuffVirtualState(definition.skillId);
    const bool changedLocalState = CancelIndependentBuffLocalState(definition);
    if (sentCancelPacket || changedLocalState)
        ForceRefreshIndependentBuffUi("cancel");
    WriteLogFmt("[IndependentBuffOverlay] cancel request result skillId=%d cancelSkillId=%d sent=%d changed=%d",
        definition.skillId,
        cancelSkillId,
        sentCancelPacket ? 1 : 0,
        changedLocalState ? 1 : 0);
    return sentCancelPacket || changedLocalState;
}

DWORD SkillOverlayBridgeResolveNativeReleaseJumpTarget(int skillId)
{
    if (skillId <= 0)
        return 0;

    CustomSkillUseRoute route = {};
    int mountedCustomSkillId = 0;
    int mountedProxyMountItemId = 0;
    bool remappedMountedDemonProxy = false;
    if (!FindRouteByCustomSkillId(skillId, route))
    {
        if (TryResolveMountedRuntimeProxyCustomSkillId(
                skillId,
                MountedRuntimeSkillKind_DemonJump,
                mountedCustomSkillId,
                &mountedProxyMountItemId) &&
            FindRouteByCustomSkillId(mountedCustomSkillId, route))
        {
            remappedMountedDemonProxy = true;
            static DWORD s_lastMountedReleaseJumpRemapLogTick = 0;
            const DWORD nowTick = GetTickCount();
            if (nowTick - s_lastMountedReleaseJumpRemapLogTick > 1000)
            {
                s_lastMountedReleaseJumpRemapLogTick = nowTick;
                WriteLogFmt(
                    "[MountDemonJump] release jump remap observed=%d -> custom=%d mount=%d route=%s releaseClass=%s",
                    skillId,
                    mountedCustomSkillId,
                    mountedProxyMountItemId,
                    PacketRouteToString(route.packetRoute),
                    ReleaseClassToString(route.releaseClass));
            }
            skillId = mountedCustomSkillId;
        }
    }

    if (!remappedMountedDemonProxy && !FindRouteByCustomSkillId(skillId, route))
    {
        // 同上：只在不属于当前活跃释放链时才清除
        if (!ShouldKeepContextForImmediateProxyClassifierPass(skillId))
        {
            ClearActiveNativeReleaseContext();
            ClearRecentNativePresentationContext();
        }
        return 0;
    }

    if (!RouteUsesNativeReleaseClass(route))
        return 0;

    if (TryApplyMountedRuntimeSkillStableProxyOverride(route, true))
    {
        return 0;
    }

    if (ShouldPreserveActiveContextForImmediateProxyRoute(skillId, route))
    {
        WriteLogFmt("[SkillRoute] keep native release context custom=%d donor=%d ignore=%d route=%s releaseClass=%s",
            g_activeNativeRelease.customSkillId,
            g_activeNativeRelease.classifierProxySkillId,
            route.skillId,
            PacketRouteToString(g_activeNativeRelease.packetRoute),
            ReleaseClassToString(g_activeNativeRelease.releaseClass));
        return 0;
    }

    const DWORD jumpTarget = ReleaseClassToJumpAddress(route.releaseClass);
    if (!jumpTarget)
        return 0;

    ArmActiveNativeReleaseContext(route);
    WriteLogFmt("[SkillRoute] arm native release custom=%d route=%s releaseClass=%s jump=0x%08X",
        route.skillId,
        PacketRouteToString(route.packetRoute),
        ReleaseClassToString(route.releaseClass),
        jumpTarget);
    return jumpTarget;
}

int SkillOverlayBridgeResolveNativeClassifierOverrideSkillId(int skillId)
{
    if (skillId <= 0)
        return 0;

    const int observedSkillId = skillId;
    CustomSkillUseRoute route = {};
    int mountedCustomSkillId = 0;
    int mountedProxyMountItemId = 0;
    bool remappedMountedDemonProxy = false;
    if (!FindRouteByCustomSkillId(skillId, route))
    {
        if (TryResolveMountedRuntimeProxyCustomSkillId(
                skillId,
                MountedRuntimeSkillKind_DemonJump,
                mountedCustomSkillId,
                &mountedProxyMountItemId) &&
            FindRouteByCustomSkillId(mountedCustomSkillId, route))
        {
            remappedMountedDemonProxy = true;
            static DWORD s_lastMountedClassifierRemapLogTick = 0;
            const DWORD nowTick = GetTickCount();
            if (nowTick - s_lastMountedClassifierRemapLogTick > 1000)
            {
                s_lastMountedClassifierRemapLogTick = nowTick;
                WriteLogFmt(
                    "[MountDemonJump] classifier remap observed=%d -> custom=%d mount=%d route=%s releaseClass=%s donor=%d",
                    skillId,
                    mountedCustomSkillId,
                    mountedProxyMountItemId,
                    PacketRouteToString(route.packetRoute),
                    ReleaseClassToString(route.releaseClass),
                    route.proxySkillId);
            }
            skillId = mountedCustomSkillId;
        }
    }

    if (!remappedMountedDemonProxy && !FindRouteByCustomSkillId(skillId, route))
    {
        // 非自定义技能进入分类器：如果残留的 presentation context 不属于
        // 当前仍活跃的释放链（即 proxy 不等于本次 skillId），才清除。
        // 这样避免链式分类器（Root替换skillId后Branch再进来）误清自己的 context。
        if (!ShouldKeepContextForImmediateProxyClassifierPass(skillId))
        {
            ClearActiveNativeReleaseContext();
            ClearRecentNativePresentationContext();
        }
        return 0;
    }

    if (!RouteUsesNativeClassifierProxy(route))
        return 0;

    if (TryApplyMountedRuntimeSkillStableProxyOverride(route, true))
    {
        if (remappedMountedDemonProxy)
        {
            ArmActiveNativeReleaseContext(route);
            const bool preserveMountedDemonChildClassifier =
                route.skillId == 30010110 &&
                IsMountedDemonJumpRuntimeChildSkillId(observedSkillId);
            if (preserveMountedDemonChildClassifier)
            {
                static DWORD s_lastMountedClassifierKeepChildLogTick = 0;
                const DWORD nowTick = GetTickCount();
                if (nowTick - s_lastMountedClassifierKeepChildLogTick > 1000)
                {
                    s_lastMountedClassifierKeepChildLogTick = nowTick;
                    WriteLogFmt(
                        "[MountDemonJump] classifier keep child observed=%d custom=%d mount=%d route=%s releaseClass=%s",
                        observedSkillId,
                        route.skillId,
                        mountedProxyMountItemId,
                        PacketRouteToString(route.packetRoute),
                        ReleaseClassToString(route.releaseClass));
                }
                return 0;
            }

            static DWORD s_lastMountedClassifierForceCustomLogTick = 0;
            const DWORD nowTick = GetTickCount();
            if (nowTick - s_lastMountedClassifierForceCustomLogTick > 1000)
            {
                s_lastMountedClassifierForceCustomLogTick = nowTick;
                WriteLogFmt(
                    "[MountDemonJump] classifier root force custom observed=%d -> custom=%d mount=%d route=%s releaseClass=%s",
                    observedSkillId,
                    route.skillId,
                    mountedProxyMountItemId,
                    PacketRouteToString(route.packetRoute),
                    ReleaseClassToString(route.releaseClass));
            }
            return route.skillId;
        }
        return 0;
    }

    if (ShouldPreserveActiveContextForImmediateProxyRoute(skillId, route))
    {
        WriteLogFmt("[SkillRoute] keep native classifier context custom=%d donor=%d ignore=%d route=%s releaseClass=%s",
            g_activeNativeRelease.customSkillId,
            g_activeNativeRelease.classifierProxySkillId,
            route.skillId,
            PacketRouteToString(g_activeNativeRelease.packetRoute),
            ReleaseClassToString(g_activeNativeRelease.releaseClass));
        return 0;
    }

    ArmActiveNativeReleaseContext(route);
    WriteLogFmt("[SkillRoute] inherit native classifier custom=%d donor=%d route=%s releaseClass=%s",
        route.skillId,
        route.proxySkillId,
        PacketRouteToString(route.packetRoute),
        ReleaseClassToString(route.releaseClass));
    return route.proxySkillId;
}

int SkillOverlayBridgeResolveNativePresentationOverrideSkillId(int skillId)
{
    if (skillId <= 0)
        return 0;

    CustomSkillUseRoute route = {};
    if (FindRouteByCustomSkillId(skillId, route))
    {
        // visualSkillId: force a specific presentation template (any native skill effect).
        if (route.visualSkillId > 0)
        {
            if (route.visualSkillId == skillId)
                return 0;
            return route.visualSkillId;
        }

        // custom skillId already arrived at presentation stage; keep native behavior.
        return 0;
    }

    // If presentation arrived with proxy skillId, remap back to recent custom release.
    int remappedSkillId = 0;
    if (TryResolveRecentNativePresentationOverride(skillId, remappedSkillId))
        return remappedSkillId;

    return 0;
}

bool SkillOverlayBridgeShouldKeepPresentationOverrideAfterDispatch(int observedSkillId, int overriddenSkillId)
{
    if (observedSkillId <= 0 || overriddenSkillId <= 0 || observedSkillId == overriddenSkillId)
        return false;

    if (!IsRecentNativePresentationContextFresh())
        return false;

    if (g_recentNativePresentation.proxySkillId != observedSkillId)
        return false;

    // generation 已被消费则不保持
    if (g_recentNativePresentation.generation <= g_lastConsumedGeneration)
        return false;

    if (g_recentNativePresentation.visualSkillId > 0)
        return g_recentNativePresentation.visualSkillId == overriddenSkillId;

    return g_recentNativePresentation.customSkillId == overriddenSkillId;
}

int SkillOverlayBridgeResolveNativePresentationDesiredSkillId(int observedSkillId)
{
    if (observedSkillId <= 0)
        return 0;

    const int overriddenSkillId = SkillOverlayBridgeResolveNativePresentationOverrideSkillId(observedSkillId);
    if (overriddenSkillId > 0)
        return overriddenSkillId;

    int mountedCustomSkillId = 0;
    int mountItemId = 0;
    if (TryResolveMountedRuntimeProxyCustomSkillId(
            observedSkillId,
            MountedRuntimeSkillKind_DemonJump,
            mountedCustomSkillId,
            &mountItemId) &&
        GameLookupSkillEntryPointer(mountedCustomSkillId))
    {
        static DWORD s_lastMountedPresentationRemapLogTick = 0;
        const DWORD nowTick = GetTickCount();
        if (nowTick - s_lastMountedPresentationRemapLogTick > 1000)
        {
            s_lastMountedPresentationRemapLogTick = nowTick;
            WriteLogFmt("[MountDemonJump] presentation remap observed=%d -> custom=%d mount=%d",
                observedSkillId,
                mountedCustomSkillId,
                mountItemId);
        }
        return mountedCustomSkillId;
    }

    CustomSkillUseRoute route = {};
    if (!FindRouteByCustomSkillId(observedSkillId, route))
        return 0;

    if (route.visualSkillId > 0)
        return route.visualSkillId;

    if (RouteUsesNativeClassifierProxy(route))
    {
        if (route.borrowDonorVisual && route.proxySkillId > 0)
            return route.proxySkillId;
        return route.skillId;
    }

    return 0;
}

int SkillOverlayBridgeResolveNativeGateSkillId(int skillId)
{
    if (skillId <= 0)
        return skillId;

    CustomSkillUseRoute route = {};
    if (!FindRouteByCustomSkillId(skillId, route))
        return skillId;

    // Mounted double-jump needs its route intent armed as early as the native
    // gate lookup stage; otherwise the first mounted jump can miss the recent
    // intent window and still surface one or more "搭乘中无法使用" prompts
    // before the later release/classifier hooks get a chance to warm up.
    if (TryApplyMountedRuntimeSkillStableProxyOverride(route, true))
    {
        return skillId;
    }

    // Some deep native branches (post animation/effect dispatch) still hard-check
    // donor ids (e.g., 1001003 family). Keep those checks on donor while release
    // and packet rewrite continue to use custom skill id outside those gates.
    if ((RouteUsesNativeClassifierProxy(route) || RouteUsesLegacyProxyPacketRewrite(route)) &&
        route.proxySkillId > 0)
    {
        return route.proxySkillId;
    }

    return skillId;
}

bool SkillOverlayBridgeShouldForceNativeGateAllow(int skillId)
{
    if (skillId <= 0)
        return false;

    if (IsNativeFlyingMountSkillGateFamily(skillId))
        return true;

    SuperSkillDefinition superDefinition = {};
    if (FindSuperSkillDefinition(skillId, superDefinition))
        return true;

    CustomSkillUseRoute route = {};
    if (FindRouteByCustomSkillId(skillId, route))
        return true;

    NativeSkillInjectionDefinition injectionDefinition = {};
    if (FindNativeSkillInjectionDefinition(skillId, injectionDefinition) &&
        injectionDefinition.enabled)
    {
        return true;
    }

    return false;
}

namespace
{
    int ResolveMountedRuntimeSkillId(
        int mountItemId,
        MountedRuntimeSkillKind kind)
    {
        SuperSkillDefinition definition = {};
        if (!FindMountedRuntimeSkillDefinitionByMountItemId(mountItemId, kind, definition))
            return 0;
        return ResolveMountedRuntimeSkillIdFromDefinition(definition, kind);
    }

    bool CanUseMountedRuntimeSkill(
        int mountItemId,
        int skillId,
        MountedRuntimeSkillKind kind)
    {
        if (mountItemId <= 0 || skillId <= 0)
            return false;

        return ResolveMountedRuntimeSkillId(mountItemId, kind) == skillId;
    }

    bool CanUseMountedRuntimeSkillWithRouteProxy(
        int mountItemId,
        int skillId,
        MountedRuntimeSkillKind kind)
    {
        if (mountItemId <= 0 || skillId <= 0)
            return false;

        const int configuredSkillId = ResolveMountedRuntimeSkillId(mountItemId, kind);
        if (configuredSkillId <= 0)
            return false;

        if (configuredSkillId == skillId)
            return true;

        if (IsMountedRuntimeSkillLinkedChild(
                configuredSkillId,
                skillId,
                kind))
        {
            return true;
        }

        CustomSkillUseRoute route = {};
        if (!FindRouteByCustomSkillId(configuredSkillId, route))
            return false;

        if ((RouteUsesNativeClassifierProxy(route) || RouteUsesLegacyProxyPacketRewrite(route)) &&
            route.proxySkillId > 0 &&
            route.proxySkillId == skillId)
        {
            return true;
        }

        return false;
    }

    bool TryGetRecentMountedRuntimeSkillRouteArmMountItemId(
        MountedRuntimeSkillKind kind,
        int* mountItemIdOut,
        DWORD maxAgeMs)
    {
        if (!mountItemIdOut || !kEnableMountedRuntimeSkillRouteArm)
        {
            return false;
        }

        const LONG recentMountItemId = InterlockedCompareExchange(
            &g_recentMountedRuntimeSkillRouteArmItemId[kind],
            0,
            0);
        if (recentMountItemId <= 0)
        {
            return false;
        }

        const LONG recentTick = InterlockedCompareExchange(
            &g_recentMountedRuntimeSkillRouteArmTick[kind],
            0,
            0);
        if (recentTick <= 0)
        {
            return false;
        }

        const DWORD nowTick = GetTickCount();
        const DWORD allowedAgeMs =
            maxAgeMs > 0 ? maxAgeMs : kMountedDoubleJumpRouteArmTimeoutMs;
        if (nowTick - static_cast<DWORD>(recentTick) > allowedAgeMs)
        {
            return false;
        }

        *mountItemIdOut = static_cast<int>(recentMountItemId);
        return true;
    }

    bool HasRecentMountedRuntimeSkillRouteArm(
        MountedRuntimeSkillKind kind,
        int mountItemId,
        DWORD maxAgeMs)
    {
        int recentMountItemId = 0;
        if (!TryGetRecentMountedRuntimeSkillRouteArmMountItemId(
                kind,
                &recentMountItemId,
                maxAgeMs))
        {
            return false;
        }

        return mountItemId <= 0 || recentMountItemId == mountItemId;
    }

    bool TryResolveMountedRuntimeProxyCustomSkillId(
        int runtimeSkillId,
        MountedRuntimeSkillKind kind,
        int& outCustomSkillId,
        int* mountItemIdOut,
        DWORD maxAgeMs)
    {
        outCustomSkillId = 0;
        if (mountItemIdOut)
        {
            *mountItemIdOut = 0;
        }

        if (runtimeSkillId <= 0)
        {
            return false;
        }

        int mountItemId = 0;
        const char* source = nullptr;
        if (TryGetRecentMountedRuntimeSkillRouteArmMountItemId(
                kind,
                &mountItemId,
                maxAgeMs))
        {
            source = "recent-route-arm";
        }
        else if (!TryResolveActiveMountedRuntimeSkillMountItemId(
                     runtimeSkillId,
                     kind,
                     mountItemId,
                     &source))
        {
            return false;
        }

        if (!CanUseMountedRuntimeSkillWithRouteProxy(
                mountItemId,
                runtimeSkillId,
                kind))
        {
            return false;
        }

        const int configuredSkillId = ResolveMountedRuntimeSkillId(mountItemId, kind);
        if (configuredSkillId <= 0 || configuredSkillId == runtimeSkillId)
        {
            return false;
        }

        outCustomSkillId = configuredSkillId;
        if (mountItemIdOut)
        {
            *mountItemIdOut = mountItemId;
        }

        static DWORD s_lastMountedRuntimeProxyResolveLogTick[MountedRuntimeSkillKind_Count] = {0};
        const DWORD nowTick = GetTickCount();
        if (nowTick - s_lastMountedRuntimeProxyResolveLogTick[kind] > 1000)
        {
            s_lastMountedRuntimeProxyResolveLogTick[kind] = nowTick;
            WriteLogFmt(
                "[%s] proxy remap source=%s runtime=%d -> custom=%d mount=%d",
                GetMountedRuntimeSkillLogTag(kind),
                source ? source : "unknown",
                runtimeSkillId,
                configuredSkillId,
                mountItemId);
        }
        return true;
    }

    bool TryResolveActiveMountedRuntimeSkillMountItemId(
        int routeSkillId,
        MountedRuntimeSkillKind kind,
        int& outMountItemId,
        const char** sourceOut)
    {
        outMountItemId = 0;
        if (sourceOut)
        {
            *sourceOut = nullptr;
        }

        if (routeSkillId <= 0)
        {
            return false;
        }

        DWORD userLocal = 0;
        int mountItemId = 0;
        if (SafeReadValue(ADDR_UserLocal, userLocal) &&
            userLocal &&
            SafeReadValue(static_cast<uintptr_t>(userLocal) + 0x454u, mountItemId) &&
            mountItemId > 0 &&
            CanUseMountedRuntimeSkillWithRouteProxy(
                mountItemId,
                routeSkillId,
                kind))
        {
            outMountItemId = mountItemId;
            if (sourceOut)
            {
                *sourceOut = "user";
            }
            return true;
        }

        if (TryGetRecentMountedMovementOverrideMountItemId(
                mountItemId,
                kMountedDoubleJumpMountSelectionFallbackTimeoutMs) &&
            CanUseMountedRuntimeSkillWithRouteProxy(
                mountItemId,
                routeSkillId,
                kind))
        {
            outMountItemId = mountItemId;
            if (sourceOut)
            {
                *sourceOut = "recent-mount-selection";
            }
            return true;
        }

        return false;
    }

    bool TryResolveActiveMountedRuntimeSkillContext(
        int routeSkillId,
        MountedRuntimeSkillKind& outKind,
        int& outMountItemId,
        const char** sourceOut)
    {
        if (TryResolveActiveMountedRuntimeSkillMountItemId(
                routeSkillId,
                MountedRuntimeSkillKind_DoubleJump,
                outMountItemId,
                sourceOut))
        {
            outKind = MountedRuntimeSkillKind_DoubleJump;
            return true;
        }

        if (TryResolveActiveMountedRuntimeSkillMountItemId(
                routeSkillId,
                MountedRuntimeSkillKind_DemonJump,
                outMountItemId,
                sourceOut))
        {
            outKind = MountedRuntimeSkillKind_DemonJump;
            return true;
        }

        return false;
    }

    void ObserveMountedRuntimeSkillRouteArm(
        MountedRuntimeSkillKind kind,
        int mountItemId)
    {
        if (mountItemId <= 0)
        {
            return;
        }

        InterlockedExchange(&g_recentMountedRuntimeSkillRouteArmItemId[kind], mountItemId);
        InterlockedExchange(
            &g_recentMountedRuntimeSkillRouteArmTick[kind],
            static_cast<LONG>(GetTickCount()));
        static DWORD s_lastMountedRuntimeSkillRouteArmLogTick[MountedRuntimeSkillKind_Count] = {0};
        const DWORD nowTick = GetTickCount();
        if (nowTick - s_lastMountedRuntimeSkillRouteArmLogTick[kind] > 1000)
        {
            s_lastMountedRuntimeSkillRouteArmLogTick[kind] = nowTick;
            WriteLogFmt("[%s] early route intent mount=%d",
                GetMountedRuntimeSkillLogTag(kind),
                mountItemId);
        }
    }

    bool TryApplyMountedRuntimeSkillStableProxyOverride(
        CustomSkillUseRoute& route,
        bool armRouteIntent)
    {
        if (!kEnableMountedRuntimeSkillRouteArm)
        {
            return false;
        }

        if (route.skillId <= 0 ||
            route.packetRoute != CustomSkillPacketRoute_SpecialMove ||
            route.releaseClass != CustomSkillReleaseClass_NativeClassifierProxy ||
            route.proxySkillId != route.skillId)
        {
            return false;
        }

        int mountItemId = 0;
        const char* source = nullptr;
        MountedRuntimeSkillKind kind = MountedRuntimeSkillKind_DoubleJump;
        if (!TryResolveActiveMountedRuntimeSkillContext(
                route.skillId,
                kind,
                mountItemId,
                &source))
        {
            return false;
        }

        if (armRouteIntent)
        {
            ObserveMountedRuntimeSkillRouteArm(kind, mountItemId);
        }

        static DWORD s_lastMountedRuntimeSkillStableProxyLogTick[MountedRuntimeSkillKind_Count] = {0};
        const DWORD nowTick = GetTickCount();
        if (nowTick - s_lastMountedRuntimeSkillStableProxyLogTick[kind] > 1000)
        {
            s_lastMountedRuntimeSkillStableProxyLogTick[kind] = nowTick;
            WriteLogFmt("[%s] stable proxy custom=%d mount=%d source=%s",
                GetMountedRuntimeSkillLogTag(kind),
                route.skillId,
                mountItemId,
                source ? source : "unknown");
        }

        // Mounted movement skills need their real native family to stay intact
        // during classifier / special-move routing. Rewriting them onto an
        // unrelated donor can make mounted gates pass, but it also swaps the
        // action branch and breaks the actual jump / glide behavior.
        return true;
    }

    bool IsConfiguredMountedDemonJumpRootSkill(int skillId)
    {
        if (skillId <= 0)
        {
            return false;
        }

        for (std::map<int, SuperSkillDefinition>::const_iterator it = g_superSkillsBySkillId.begin();
             it != g_superSkillsBySkillId.end();
             ++it)
        {
            const SuperSkillDefinition& definition = it->second;
            if (definition.mountItemId <= 0 ||
                !definition.mountedDemonJumpEnabled ||
                definition.mountedDemonJumpSkillId <= 0)
            {
                continue;
            }

            if (definition.mountedDemonJumpSkillId == skillId)
            {
                return true;
            }
        }

        return false;
    }

    bool ShouldKeepPassiveMountedDemonJumpRoute(const CustomSkillUseRoute& route)
    {
        return route.skillId > 0 &&
               route.skillId == route.proxySkillId &&
               route.packetRoute == CustomSkillPacketRoute_SpecialMove &&
               route.releaseClass == CustomSkillReleaseClass_NativeClassifierProxy &&
               IsConfiguredMountedDemonJumpRootSkill(route.skillId);
    }

    void EnsureMountedDemonJumpSyntheticRoutes()
    {
        for (std::map<int, SuperSkillDefinition>::const_iterator it = g_superSkillsBySkillId.begin();
             it != g_superSkillsBySkillId.end();
             ++it)
        {
            const SuperSkillDefinition& definition = it->second;
            if (definition.mountItemId <= 0 ||
                !definition.mountedDemonJumpEnabled ||
                definition.mountedDemonJumpSkillId <= 0)
            {
                continue;
            }

            const int routeSkillId = definition.mountedDemonJumpSkillId;
            if (g_customRoutesBySkillId.find(routeSkillId) != g_customRoutesBySkillId.end())
            {
                continue;
            }

            CustomSkillUseRoute route = {};
            route.skillId = routeSkillId;
            route.proxySkillId = routeSkillId;
            route.packetRoute = CustomSkillPacketRoute_SpecialMove;
            route.releaseClass = CustomSkillReleaseClass_NativeClassifierProxy;
            route.borrowDonorVisual = false;
            route.visualSkillId = 0;
            g_customRoutesBySkillId[route.skillId] = route;

            WriteLogFmt(
                "[SkillRoute] synth mounted demon root custom=%d mountSkill=%d mountItem=%d proxy=%d route=%s releaseClass=%s",
                route.skillId,
                definition.skillId,
                definition.mountItemId,
                route.proxySkillId,
                PacketRouteToString(route.packetRoute),
                ReleaseClassToString(route.releaseClass));
        }
    }
}

int SkillOverlayBridgeResolveMountedDoubleJumpSkillId(int mountItemId)
{
    return ResolveMountedRuntimeSkillId(
        mountItemId,
        MountedRuntimeSkillKind_DoubleJump);
}

int SkillOverlayBridgeResolveMountedDemonJumpSkillId(int mountItemId)
{
    return ResolveMountedRuntimeSkillId(
        mountItemId,
        MountedRuntimeSkillKind_DemonJump);
}

bool SkillOverlayBridgeResolveMountedMovementOverride(int mountItemId, int tamingMobId, MountedMovementOverride& outOverride)
{
    outOverride = MountedMovementOverride();

    SuperSkillDefinition definition = {};
    if (!FindMountedMovementOverrideDefinition(mountItemId, tamingMobId, definition))
        return false;

    PopulateMountedMovementOverrideFromDefinition(definition, tamingMobId, outOverride);

    return outOverride.matched;
}

bool SkillOverlayBridgeResolveMountedSoaringOverride(int mountItemId, int tamingMobId, MountedMovementOverride& outOverride)
{
    outOverride = MountedMovementOverride();
    if (mountItemId <= 0 && tamingMobId <= 0)
        return false;

    int preferredSkillId = 0;
    if (TryGetPreferredMountedMovementOverrideSkillId(mountItemId, preferredSkillId))
    {
        SuperSkillDefinition preferredDefinition = {};
        if (TryResolveMountedMovementOverrideDefinitionForSkill(
                preferredSkillId,
                mountItemId,
                tamingMobId,
                preferredDefinition))
        {
            PopulateMountedMovementOverrideFromDefinition(preferredDefinition, tamingMobId, outOverride);
            return outOverride.matched;
        }
    }

    bool hasLearnedMatch = false;
    bool hasJobMatch = false;
    SuperSkillDefinition learnedDefinition = {};
    SuperSkillDefinition jobDefinition = {};
    double learnedScore = -1.0;
    double jobScore = -1.0;

    for (std::map<int, SuperSkillDefinition>::const_iterator it = g_superSkillsBySkillId.begin();
         it != g_superSkillsBySkillId.end();
         ++it)
    {
        const SuperSkillDefinition& definition = it->second;
        if (!DefinitionParticipatesInMountMovementSelection(definition))
            continue;
        if (!DoesMountedMovementDefinitionMatch(definition, mountItemId, tamingMobId))
            continue;

        double candidateScore = 0.0;
        if (definition.hasMountSwimOverride && definition.mountSwimOverride > candidateScore)
            candidateScore = definition.mountSwimOverride;
        if (definition.hasMountFsOverride && definition.mountFsOverride > candidateScore)
            candidateScore = definition.mountFsOverride;
        const bool jobMatches = DoesSuperSkillMatchCurrentJob(definition);
        const bool learned = GameGetSkillLevel(definition.skillId) > 0;

        if (jobMatches && learned && candidateScore > learnedScore)
        {
            learnedDefinition = definition;
            learnedScore = candidateScore;
            hasLearnedMatch = true;
        }

        if (jobMatches && candidateScore > jobScore)
        {
            jobDefinition = definition;
            jobScore = candidateScore;
            hasJobMatch = true;
        }
    }

    if (hasLearnedMatch)
    {
        PopulateMountedMovementOverrideFromDefinition(learnedDefinition, tamingMobId, outOverride);
        return true;
    }
    if (hasJobMatch)
    {
        PopulateMountedMovementOverrideFromDefinition(jobDefinition, tamingMobId, outOverride);
        return true;
    }

    return false;
}

bool SkillOverlayBridgeCanUseMountedDoubleJumpSkill(int mountItemId, int skillId)
{
    return CanUseMountedRuntimeSkill(
        mountItemId,
        skillId,
        MountedRuntimeSkillKind_DoubleJump);
}

bool SkillOverlayBridgeCanUseMountedDemonJumpSkill(int mountItemId, int skillId)
{
    return CanUseMountedRuntimeSkill(
        mountItemId,
        skillId,
        MountedRuntimeSkillKind_DemonJump);
}

bool SkillOverlayBridgeCanUseMountedDoubleJumpRuntimeSkill(int mountItemId, int skillId)
{
    return CanUseMountedRuntimeSkillWithRouteProxy(
        mountItemId,
        skillId,
        MountedRuntimeSkillKind_DoubleJump);
}

bool SkillOverlayBridgeCanUseMountedDemonJumpRuntimeSkill(int mountItemId, int skillId)
{
    return CanUseMountedRuntimeSkillWithRouteProxy(
        mountItemId,
        skillId,
        MountedRuntimeSkillKind_DemonJump);
}

bool SkillOverlayBridgeHasRecentMountedDoubleJumpRouteArm(int mountItemId, DWORD maxAgeMs)
{
    return HasRecentMountedRuntimeSkillRouteArm(
        MountedRuntimeSkillKind_DoubleJump,
        mountItemId,
        maxAgeMs);
}

bool SkillOverlayBridgeHasRecentMountedDemonJumpRouteArm(int mountItemId, DWORD maxAgeMs)
{
    return HasRecentMountedRuntimeSkillRouteArm(
        MountedRuntimeSkillKind_DemonJump,
        mountItemId,
        maxAgeMs);
}

bool SkillOverlayBridgeTryGetRecentMountedDoubleJumpRouteArmMountItemId(
    int* mountItemIdOut,
    DWORD maxAgeMs)
{
    return TryGetRecentMountedRuntimeSkillRouteArmMountItemId(
        MountedRuntimeSkillKind_DoubleJump,
        mountItemIdOut,
        maxAgeMs);
}

bool SkillOverlayBridgeTryGetRecentMountedDemonJumpRouteArmMountItemId(
    int* mountItemIdOut,
    DWORD maxAgeMs)
{
    return TryGetRecentMountedRuntimeSkillRouteArmMountItemId(
        MountedRuntimeSkillKind_DemonJump,
        mountItemIdOut,
        maxAgeMs);
}

int SkillOverlayBridgeResolveNativeLevelLookupSkillId(int skillId)
{
    if (skillId <= 0)
        return skillId;

    if (IsMountedDemonJumpRuntimeChildSkillId(skillId))
    {
        static DWORD s_lastMountedChildLevelKeepLogTick = 0;
        const DWORD nowTick = GetTickCount();
        if (nowTick - s_lastMountedChildLevelKeepLogTick > 1000)
        {
            s_lastMountedChildLevelKeepLogTick = nowTick;
            WriteLogFmt("[MountDemonJump] level keep child query=%d",
                skillId);
        }
        return skillId;
    }

    int mountedCustomSkillId = 0;
    int mountItemId = 0;
    if (TryResolveMountedRuntimeProxyCustomSkillId(
            skillId,
            MountedRuntimeSkillKind_DemonJump,
            mountedCustomSkillId,
            &mountItemId))
    {
        if (GameLookupSkillEntryPointer(mountedCustomSkillId))
        {
            static DWORD s_lastMountedProxyLevelRemapLogTick = 0;
            const DWORD nowTick = GetTickCount();
            if (nowTick - s_lastMountedProxyLevelRemapLogTick > 1000)
            {
                s_lastMountedProxyLevelRemapLogTick = nowTick;
                WriteLogFmt("[MountDemonJump] level remap query=%d -> custom=%d mount=%d",
                    skillId,
                    mountedCustomSkillId,
                    mountItemId);
            }
            return mountedCustomSkillId;
        }

        static DWORD s_lastMissingMountedProxyLevelEntryLogTick = 0;
        const DWORD nowTick = GetTickCount();
        if (nowTick - s_lastMissingMountedProxyLevelEntryLogTick > 1000)
        {
            s_lastMissingMountedProxyLevelEntryLogTick = nowTick;
            WriteLogFmt("[MountDemonJump] level remap keep proxy query=%d custom=%d mount=%d (entry missing)",
                skillId,
                mountedCustomSkillId,
                mountItemId);
        }
    }

    // During native classifier proxy release, many deep checks query donor skill level.
    // Remap those queries back to custom skill id so cross-job donor can still pass.
    if (IsActiveNativeReleaseContextFresh() &&
        g_activeNativeRelease.classifierProxySkillId > 0 &&
        g_activeNativeRelease.customSkillId > 0 &&
        skillId == g_activeNativeRelease.classifierProxySkillId)
    {
        const int customSkillId = g_activeNativeRelease.customSkillId;
        if (GameLookupSkillEntryPointer(customSkillId))
            return customSkillId;

        static DWORD s_lastMissingLevelEntryLogTick = 0;
        const DWORD nowTick = GetTickCount();
        if (nowTick - s_lastMissingLevelEntryLogTick > 1000)
        {
            s_lastMissingLevelEntryLogTick = nowTick;
            WriteLogFmt("[SkillLevelBridge] fallback keep donor level lookup: donor=%d custom=%d (custom entry missing)",
                skillId,
                customSkillId);
        }
        return skillId;
    }

    return skillId;
}

void SkillOverlayBridgeObserveLevelQueryContext(void* skillDataMgr, DWORD playerObj)
{
    const DWORD mgr = (DWORD)(uintptr_t)skillDataMgr;
    if (mgr && !SafeIsBadReadPtr((void*)mgr, 4))
        g_lastObservedSkillDataMgr = mgr;

    if (playerObj && !SafeIsBadReadPtr((void*)playerObj, 4))
        g_lastObservedLevelContext = playerObj;
}

void SkillOverlayBridgeObserveLevelResult(int skillId, int level, bool isBaseLevel)
{
    if (skillId <= 0 || level < 0)
        return;

    SkillManager* manager = GetBridgeManager();
    SkillItem* item = FindManagerSkillItem(manager, skillId);
        const bool suppressResetFallback =
            level == 0 &&
            ShouldSuppressNonNativeSuperSkillLevelFallback(skillId);
        if (suppressResetFallback)
        {
            ClearTrackedNonNativeSuperSkillLevel(
                skillId,
                isBaseLevel ? "observe-base-reset-sync" : "observe-current-reset-sync");
        }

        std::map<int, int>& actualObservedLevels =
            isBaseLevel ? g_observedActualBaseLevelsBySkillId : g_observedActualCurrentLevelsBySkillId;
        actualObservedLevels[skillId] = level;

    PendingOptimisticSuperSkillLevelHold optimisticHold;
    const bool hasFreshOptimisticHold =
        TryGetFreshPendingOptimisticSuperSkillLevelHold(skillId, optimisticHold);
    int persistentNonNativeSuperSkillLevel = 0;
    bool preservePersistentNonNativeSuperSkillLevel =
        TryResolvePersistentNonNativeSuperSkillLevel(
            skillId,
            level,
            item ? item->level : 0,
            persistentNonNativeSuperSkillLevel);
    bool preserveOptimisticNonNativeSuperSkillLevel =
        item &&
        item->isSuperSkill &&
        !item->hasNativeUpgradeState &&
        hasFreshOptimisticHold &&
        optimisticHold.expectedLevel > 0 &&
        item->level >= optimisticHold.expectedLevel &&
        level < optimisticHold.expectedLevel;
    bool preserveLocalNonNativeSuperSkillLevel =
        preservePersistentNonNativeSuperSkillLevel ||
        preserveOptimisticNonNativeSuperSkillLevel;
    int preservedNonNativeSuperSkillLevel =
        preserveOptimisticNonNativeSuperSkillLevel && item
            ? item->level
            : persistentNonNativeSuperSkillLevel;

    if (!suppressResetFallback && !preserveLocalNonNativeSuperSkillLevel && level == 0)
    {
        SuperSkillDefinition definition = {};
        const bool isKnownNonNativeSuperSkill =
            FindSuperSkillDefinition(skillId, definition) &&
            (!item || !item->hasNativeUpgradeState);
        if (isKnownNonNativeSuperSkill)
        {
            int keepLevel = item ? item->level : 0;
            int persistentLevel = 0;
            if (TryGetPersistentSuperSkillLevel(skillId, persistentLevel) && persistentLevel > keepLevel)
                keepLevel = persistentLevel;
            const int observedBaseLevel = GetObservedBaseSkillLevel(skillId);
            if (observedBaseLevel > keepLevel)
                keepLevel = observedBaseLevel;
            const int observedCurrentLevel = GetObservedCurrentSkillLevel(skillId);
            if (observedCurrentLevel > keepLevel)
                keepLevel = observedCurrentLevel;

            if (keepLevel > 0)
            {
                preservePersistentNonNativeSuperSkillLevel = true;
                preserveLocalNonNativeSuperSkillLevel = true;
                preservedNonNativeSuperSkillLevel = keepLevel;

                static LONG s_ignoreZeroNonNativeSuperSkillLevelLogBudget = 48;
                const LONG budgetAfterDecrement = InterlockedDecrement(&s_ignoreZeroNonNativeSuperSkillLevelLogBudget);
                if (budgetAfterDecrement >= 0)
                {
                    WriteLogFmt("[SkillLevelBridge] ignore zero for non-native super skill skillId=%d keep=%d item=%d persistent=%d base=%d current=%d mode=%s",
                        skillId,
                        keepLevel,
                        item ? item->level : 0,
                        persistentLevel,
                        observedBaseLevel,
                        observedCurrentLevel,
                        isBaseLevel ? "base" : "current");
                }
            }
        }
    }

    std::map<int, int>& observedLevels = isBaseLevel ? g_observedBaseLevelsBySkillId : g_observedCurrentLevelsBySkillId;
    if (IsKnownSuperSkillCarrierSkillId(skillId))
    {
        std::map<int, int>::iterator observedIt = observedLevels.find(skillId);
        if (observedIt == observedLevels.end())
        {
            observedLevels[skillId] = level;
        }
        else if (level == 0 || (level > observedIt->second && level != 1))
        {
            observedIt->second = level;
        }
    }
    else
    {
        if (preserveLocalNonNativeSuperSkillLevel)
        {
            observedLevels[skillId] = preservedNonNativeSuperSkillLevel;
            RecordPersistentSuperSkillLevel(skillId, preservedNonNativeSuperSkillLevel, "observe-preserve");
        }
        else
        {
            observedLevels[skillId] = level;
            if (level > 0)
                RecordPersistentSuperSkillLevel(skillId, level, isBaseLevel ? "observe-base" : "observe-current");
        }
    }

    if (!item)
        return;

    if (preserveLocalNonNativeSuperSkillLevel)
    {
        if (preservedNonNativeSuperSkillLevel > item->level)
            item->level = preservedNonNativeSuperSkillLevel;

        static LONG s_preserveLocalSuperSkillLevelLogBudget = 32;
        const LONG budgetAfterDecrement = InterlockedDecrement(&s_preserveLocalSuperSkillLevelLogBudget);
        if (budgetAfterDecrement >= 0)
        {
            WriteLogFmt("[SkillLevelBridge] preserve non-native super skill level skillId=%d observed=%d keep=%d expected=%d mode=%s base=%d",
                skillId,
                level,
                preservedNonNativeSuperSkillLevel,
                optimisticHold.expectedLevel,
                preserveOptimisticNonNativeSuperSkillLevel ? "optimistic" : "persistent",
                isBaseLevel ? 1 : 0);
        }
    }
    else if (isBaseLevel)
    {
        if (item->level < level)
            item->level = level;
    }
    else
    {
        item->level = level;
    }

    if (item->level < 0)
        item->level = 0;
    if (item->maxLevel > 0 && item->level > item->maxLevel)
        item->level = item->maxLevel;

    if (hasFreshOptimisticHold && level >= optimisticHold.expectedLevel)
    {
        ClearPendingOptimisticSuperSkillLevelHold(skillId);
    }
    else if (!preserveLocalNonNativeSuperSkillLevel)
    {
        ClearPendingOptimisticSuperSkillLevelHold(skillId);
    }

    RefreshSkillNativeState(*item);
}

uintptr_t SkillOverlayBridgeLookupSkillEntryPointer(int skillId)
{
    return GameLookupSkillEntryPointer(skillId);
}

void SkillOverlayBridgeApplyConfiguredPassiveEffectBonuses(uintptr_t skillEntryPtr, int level, uintptr_t effectPtr, const char* sourceTag)
{
    if (!skillEntryPtr || !effectPtr || level <= 0)
        return;
    if (SafeIsBadReadPtr(reinterpret_cast<void*>(skillEntryPtr), sizeof(DWORD)))
        return;

    const int targetSkillId = *reinterpret_cast<int*>(skillEntryPtr);
    if (targetSkillId <= 0 || targetSkillId > kIndependentBuffMaxReasonableSkillId)
        return;

    RememberPassiveEffectRuntimeContext(targetSkillId, level, effectPtr);

    const int damageBonus = ResolveConfiguredPassiveDamagePercentBonusForSkill(targetSkillId);
    const int ignoreMobpdpRBonus = ResolveConfiguredPassiveIgnoreDefensePercentBonusForSkill(targetSkillId);
    const int attackCountBonus = ResolveConfiguredPassiveAttackCountBonusForSkill(targetSkillId);
    const int mobCountBonus = ResolveConfiguredPassiveMobCountBonusForSkill(targetSkillId);

    const size_t kEffectBytesNeededForIgnoreMobpdpR = 0x45C;
    if (SafeIsBadReadPtr(reinterpret_cast<void*>(effectPtr), kEffectBytesNeededForIgnoreMobpdpR) ||
        SafeIsBadWritePtr(reinterpret_cast<void*>(effectPtr), kEffectBytesNeededForIgnoreMobpdpR))
    {
        static DWORD s_lastBadEffectPtrLogTick = 0;
        const DWORD nowTick = GetTickCount();
        if (nowTick - s_lastBadEffectPtrLogTick > 1000)
        {
            s_lastBadEffectPtrLogTick = nowTick;
            WriteLogFmt("[SuperPassiveEffect] skip unreadable effect source=%s target=%d level=%d entry=0x%08X effect=0x%08X",
                sourceTag ? sourceTag : "?",
                targetSkillId,
                level,
                (DWORD)skillEntryPtr,
                (DWORD)effectPtr);
        }
        return;
    }

    DWORD* effectBase = reinterpret_cast<DWORD*>(effectPtr);
    std::map<uintptr_t, PassiveEffectPatchSnapshot>::iterator snapshotIt =
        g_passiveEffectPatchSnapshotsByEffectPtr.find(effectPtr);
    if (damageBonus == 0 && ignoreMobpdpRBonus == 0 && attackCountBonus == 0 && mobCountBonus == 0)
    {
        if (snapshotIt != g_passiveEffectPatchSnapshotsByEffectPtr.end() &&
            snapshotIt->second.initialized &&
            snapshotIt->second.skillId == targetSkillId &&
            snapshotIt->second.level == level &&
            (snapshotIt->second.lastDamageBonus != 0 ||
             snapshotIt->second.lastMobCountBonus != 0 ||
             snapshotIt->second.lastAttackCountBonus != 0 ||
             snapshotIt->second.lastIgnoreMobpdpRBonus != 0))
        {
            PassiveEffectPatchSnapshot& snapshot = snapshotIt->second;
            if (snapshot.hasDamageLocal)
                WriteEncryptedTripletValueLocal(effectBase, 74, snapshot.originalDamageLocal);
            if (snapshot.hasDamage)
                WriteEncryptedTripletValueLocal(effectBase, 117, snapshot.originalDamage);
            if (snapshot.hasDamageAlt)
                WriteEncryptedTripletValueLocal(effectBase, 120, snapshot.originalDamageAlt);
            if (snapshot.hasMobCount)
                WriteEncryptedTripletValueLocal(effectBase, 101, snapshot.originalMobCount);
            if (snapshot.hasAttackCount)
                WriteEncryptedTripletValueLocal(effectBase, 104, snapshot.originalAttackCount);
            if (snapshot.hasAttackCountAlt)
                WriteEncryptedTripletValueLocal(effectBase, 114, snapshot.originalAttackCountAlt);
            if (snapshot.hasIgnoreMobpdpR)
                WriteEncryptedTripletValueLocal(effectBase, 276, snapshot.originalIgnoreMobpdpR);

            WriteLogFmt("[SuperPassiveEffect] restore source=%s target=%d level=%d entry=0x%08X effect=0x%08X damageLocal=%d damage=%d damageAlt=%d mobCount=%d attackCount=%d ignoreMobpdpR=%d",
                sourceTag ? sourceTag : "?",
                targetSkillId,
                level,
                (DWORD)skillEntryPtr,
                (DWORD)effectPtr,
                snapshot.originalDamageLocal,
                snapshot.originalDamage,
                snapshot.originalDamageAlt,
                snapshot.originalMobCount,
                snapshot.originalAttackCount,
                snapshot.originalIgnoreMobpdpR);

            snapshot.lastDamageBonus = 0;
            snapshot.lastMobCountBonus = 0;
            snapshot.lastAttackCountBonus = 0;
            snapshot.lastIgnoreMobpdpRBonus = 0;
        }
        return;
    }

    PassiveEffectPatchSnapshot& snapshot =
        (snapshotIt != g_passiveEffectPatchSnapshotsByEffectPtr.end())
            ? snapshotIt->second
            : g_passiveEffectPatchSnapshotsByEffectPtr[effectPtr];
    if (!snapshot.initialized || snapshot.skillId != targetSkillId || snapshot.level != level)
    {
        snapshot = PassiveEffectPatchSnapshot{};
        snapshot.initialized = true;
        snapshot.skillId = targetSkillId;
        snapshot.level = level;
        snapshot.hasDamageLocal = ReadEncryptedTripletValueLocal(effectBase, 74, &snapshot.originalDamageLocal);
        snapshot.hasDamage = ReadEncryptedTripletValueLocal(effectBase, 117, &snapshot.originalDamage);
        snapshot.hasDamageAlt = ReadEncryptedTripletValueLocal(effectBase, 120, &snapshot.originalDamageAlt);
        snapshot.hasMobCount = ReadEncryptedTripletValueLocal(effectBase, 101, &snapshot.originalMobCount);
        snapshot.hasAttackCount = ReadEncryptedTripletValueLocal(effectBase, 104, &snapshot.originalAttackCount);
        snapshot.hasAttackCountAlt = ReadEncryptedTripletValueLocal(effectBase, 114, &snapshot.originalAttackCountAlt);
        snapshot.hasIgnoreMobpdpR = ReadEncryptedTripletValueLocal(effectBase, 276, &snapshot.originalIgnoreMobpdpR);
    }

    LogPassiveEffectDecodedValueCandidates(effectPtr, targetSkillId, level);

    if (!snapshot.hasDamageLocal &&
        !snapshot.hasDamage &&
        !snapshot.hasDamageAlt &&
        !snapshot.hasMobCount &&
        !snapshot.hasAttackCount &&
        !snapshot.hasAttackCountAlt &&
        !snapshot.hasIgnoreMobpdpR)
    {
        g_passiveEffectPatchSnapshotsByEffectPtr.erase(effectPtr);
        return;
    }

    int nextDamageLocal = snapshot.originalDamageLocal;
    int nextDamage = snapshot.originalDamage;
    int nextDamageAlt = snapshot.originalDamageAlt;
    int nextMobCount = snapshot.originalMobCount;
    int nextAttackCount = snapshot.originalAttackCount;
    int nextIgnoreMobpdpR = snapshot.originalIgnoreMobpdpR;
    int appliedDamageBonus = 0;
    int appliedMobCountBonus = 0;
    int appliedAttackCountBonus = 0;
    int appliedIgnoreMobpdpRBonus = 0;
    bool wroteAny = false;

    int desiredValue = 0;
    int baseValue = 0;
    int resolvedBonus = 0;
    if (TryResolveDesiredPassiveEffectValueFromSnapshot(
            snapshot,
            "damage",
            nullptr,
            desiredValue,
            baseValue,
            resolvedBonus) &&
        ((snapshot.hasDamageLocal &&
          desiredValue != snapshot.originalDamageLocal &&
          WriteEncryptedTripletValueLocal(effectBase, 74, desiredValue)) |
         (snapshot.hasDamage &&
          desiredValue != snapshot.originalDamage &&
          WriteEncryptedTripletValueLocal(effectBase, 117, desiredValue)) |
         (snapshot.hasDamageAlt &&
          desiredValue != snapshot.originalDamageAlt &&
          WriteEncryptedTripletValueLocal(effectBase, 120, desiredValue))))
    {
        if (snapshot.hasDamageLocal)
            nextDamageLocal = desiredValue;
        if (snapshot.hasDamage)
            nextDamage = desiredValue;
        if (snapshot.hasDamageAlt)
            nextDamageAlt = desiredValue;
        appliedDamageBonus = resolvedBonus;
        RememberPassiveEffectDamageWrite(targetSkillId);
        wroteAny = true;
    }

    // Do not raise client-side mobCount here. Selecting additional monster OIDs in
    // the native client can desync from server-side validation and disconnect.
    (void)mobCountBonus;

    desiredValue = 0;
    baseValue = 0;
    resolvedBonus = 0;
    if (TryResolveDesiredPassiveEffectValueFromSnapshot(
            snapshot,
            "attackCount",
            nullptr,
            desiredValue,
            baseValue,
            resolvedBonus) &&
        ((snapshot.hasAttackCount &&
          WriteEncryptedTripletValueLocal(effectBase, 104, desiredValue)) |
         (snapshot.hasAttackCountAlt &&
          WriteEncryptedTripletValueLocal(effectBase, 114, desiredValue))))
    {
        nextAttackCount = desiredValue;
        appliedAttackCountBonus = resolvedBonus;
        wroteAny = true;
    }

    desiredValue = 0;
    baseValue = 0;
    resolvedBonus = 0;
    if (TryResolveDesiredPassiveEffectValueFromSnapshot(
            snapshot,
            "ignoreMobpdpR",
            nullptr,
            desiredValue,
            baseValue,
            resolvedBonus) &&
        WriteEncryptedTripletValueLocal(effectBase, 276, desiredValue))
    {
        nextIgnoreMobpdpR = desiredValue;
        appliedIgnoreMobpdpRBonus = resolvedBonus;
        wroteAny = true;
    }

    const bool bonusSignatureChanged =
        snapshot.lastDamageBonus != appliedDamageBonus ||
        snapshot.lastMobCountBonus != appliedMobCountBonus ||
        snapshot.lastAttackCountBonus != appliedAttackCountBonus ||
        snapshot.lastIgnoreMobpdpRBonus != appliedIgnoreMobpdpRBonus;

    if (wroteAny && bonusSignatureChanged)
    {
        WriteLogFmt("[SuperPassiveEffect] source=%s target=%d level=%d entry=0x%08X effect=0x%08X damageLocal=%d->%d(+%d) damage=%d->%d(+%d) damageAlt=%d->%d(+%d) mobCount=%d->%d(+%d) attackCount=%d->%d(+%d) ignoreMobpdpR=%d->%d(+%d)",
            sourceTag ? sourceTag : "?",
            targetSkillId,
            level,
            (DWORD)skillEntryPtr,
            (DWORD)effectPtr,
            snapshot.originalDamageLocal,
            nextDamageLocal,
            appliedDamageBonus,
            snapshot.originalDamage,
            nextDamage,
            appliedDamageBonus,
            snapshot.originalDamageAlt,
            nextDamageAlt,
            appliedDamageBonus,
            snapshot.originalMobCount,
            nextMobCount,
            appliedMobCountBonus,
            snapshot.originalAttackCount,
            nextAttackCount,
            appliedAttackCountBonus,
            snapshot.originalIgnoreMobpdpR,
            nextIgnoreMobpdpR,
            appliedIgnoreMobpdpRBonus);
    }

    snapshot.lastDamageBonus = appliedDamageBonus;
    snapshot.lastMobCountBonus = appliedMobCountBonus;
    snapshot.lastAttackCountBonus = appliedAttackCountBonus;
    snapshot.lastIgnoreMobpdpRBonus = appliedIgnoreMobpdpRBonus;
}

int SkillOverlayBridgeOverridePassiveEffectGetterValue(uintptr_t effectPtr, int originalValue, const char* getterTag, const char* getterSourceTag)
{
    if (!effectPtr || !getterTag || !getterTag[0])
        return originalValue;

    int resolvedSkillId = 0;
    int resolvedLevel = 0;
    int baseValue = originalValue;
    int bonusValue = 0;
    int overriddenValue = originalValue;
    bool resolved = false;
    bool changed = false;

    std::map<uintptr_t, PassiveEffectPatchSnapshot>::iterator snapshotIt =
        g_passiveEffectPatchSnapshotsByEffectPtr.find(effectPtr);
    if (snapshotIt != g_passiveEffectPatchSnapshotsByEffectPtr.end())
    {
        resolvedSkillId = snapshotIt->second.skillId;
        resolvedLevel = snapshotIt->second.level;
        if (TryResolveDesiredPassiveEffectValueFromSnapshot(
                snapshotIt->second,
                getterTag,
                getterSourceTag,
                overriddenValue,
                baseValue,
                bonusValue))
        {
            resolved = bonusValue != 0;
            changed = overriddenValue != originalValue;
        }
    }

    if (!resolved)
    {
        std::map<uintptr_t, PassiveEffectRuntimeContext>::iterator contextIt =
            g_passiveEffectRuntimeContextsByEffectPtr.find(effectPtr);
        if (contextIt == g_passiveEffectRuntimeContextsByEffectPtr.end())
            return originalValue;

        const PassiveEffectRuntimeContext& context = contextIt->second;
        if (context.skillId <= 0 || context.level <= 0)
            return originalValue;

        resolvedSkillId = context.skillId;
        resolvedLevel = context.level;
        overriddenValue = ResolvePassiveEffectGetterOverrideValue(
            context.skillId,
            context.level,
            originalValue,
            getterTag,
            getterSourceTag,
            baseValue,
            bonusValue);
        if (bonusValue == 0)
            return originalValue;

        resolved = true;
        changed = overriddenValue != originalValue;
    }

    static std::map<std::string, DWORD> s_lastGetterOverrideLogTickByKey;
    const char* logSourceTag =
        (getterSourceTag && getterSourceTag[0]) ? getterSourceTag : getterTag;
    char keyBuffer[96] = {};
    std::snprintf(keyBuffer, sizeof(keyBuffer), "%d:%s:%s", resolvedSkillId, getterTag, logSourceTag);
    DWORD& lastLogTick = s_lastGetterOverrideLogTickByKey[keyBuffer];
    const DWORD nowTick = GetTickCount();
    if (nowTick - lastLogTick > 1000)
    {
        lastLogTick = nowTick;
        WriteLogFmt("[SuperPassiveGetter] %s via=%s target=%d level=%d effect=0x%08X native=%d base=%d bonus=%d -> %d changed=%d",
            getterTag,
            logSourceTag,
            resolvedSkillId,
            resolvedLevel,
            (DWORD)effectPtr,
            originalValue,
            baseValue,
            bonusValue,
            overriddenValue,
            changed ? 1 : 0);
    }

    if (_stricmp(getterTag, "damage") == 0)
        RememberPassiveEffectDamageGetterUse(resolvedSkillId);
    else if (_stricmp(getterTag, "attackCount") == 0)
        RememberPassiveEffectAttackCountGetterUse(resolvedSkillId);

    return changed ? overriddenValue : originalValue;
}

void SkillOverlayBridgeInspectOutgoingPacketMutable(void** packetDataSlot, int* packetLenSlot, uintptr_t callerRetAddr)
{
    if (!packetDataSlot || !packetLenSlot || !*packetDataSlot || *packetLenSlot < 8)
        return;

    BYTE* packet = static_cast<BYTE*>(*packetDataSlot);
    int packetLen = *packetLenSlot;
    const unsigned short opcode = ReadPacketWord(packet);

    if (opcode == kClientCancelBuffPacketOpcode && packetLen >= 6)
    {
        const int cancelSkillId = ReadPacketInt(packet, 2);
        const DWORD now = GetTickCount();
        int rewrittenCancelSkillId = cancelSkillId;
        bool matchedIndependentBuff = false;
        for (std::map<int, SuperSkillDefinition>::const_iterator it = g_superSkillsBySkillId.begin();
             it != g_superSkillsBySkillId.end();
             ++it)
        {
            const SuperSkillDefinition& definition = it->second;
            if (!definition.independentBuffEnabled)
                continue;

            const int displaySkillId = definition.independentNativeDisplaySkillId > 0
                ? definition.independentNativeDisplaySkillId
                : definition.skillId;
            int proxySkillId = 0;
            CustomSkillUseRoute route = {};
            if (FindRouteByCustomSkillId(definition.skillId, route))
                proxySkillId = route.proxySkillId;

            if (cancelSkillId == definition.skillId ||
                cancelSkillId == displaySkillId ||
                cancelSkillId == definition.behaviorSkillId ||
                (proxySkillId > 0 && cancelSkillId == proxySkillId))
            {
                matchedIndependentBuff = true;
                g_recentIndependentBuffClientCancelTickBySkillId[definition.skillId] = now;
                if (cancelSkillId != definition.skillId)
                    rewrittenCancelSkillId = definition.skillId;
            }
        }
        if (matchedIndependentBuff && rewrittenCancelSkillId != cancelSkillId)
        {
            WritePacketInt(packet, 2, rewrittenCancelSkillId);
            WriteLogFmt("[IndependentBuffClient] cancel rewrite outgoing old=%d new=%d len=%d caller=0x%08X",
                cancelSkillId,
                rewrittenCancelSkillId,
                packetLen,
                (DWORD)(uintptr_t)callerRetAddr);
        }
        if (matchedIndependentBuff)
        {
            WriteLogFmt("[IndependentBuffClient] cancel send skillId=%d len=%d caller=0x%08X",
                rewrittenCancelSkillId,
                packetLen,
                (DWORD)(uintptr_t)callerRetAddr);
        }
    }

    if (TryRewritePendingSuperSkillUpgradePacket(packet, packetLen, opcode, callerRetAddr))
        return;

    int skillIdOffset = -1;
    int skillLevelOffset = -1;
    const CustomSkillPacketRoute packetRoute = ResolvePacketRoute(opcode, skillIdOffset, skillLevelOffset);
    if (packetRoute == CustomSkillPacketRoute_None)
    {
        TryLogUnmappedCustomSkillPacket(packet, packetLen, opcode, callerRetAddr);
        return;
    }

    if (skillIdOffset < 0 || packetLen < skillIdOffset + 4)
        return;

    const int observedSkillId = ReadPacketInt(packet, skillIdOffset);
    if (observedSkillId <= 0)
        return;

    int observedLevel = 0;
    if (skillLevelOffset >= 0 && packetLen > skillLevelOffset)
        observedLevel = packet[skillLevelOffset];

    if (ShouldLogObservedSkillPacket(observedSkillId))
    {
        WriteLogFmt("[SkillPacket] observe opcode=0x%X route=%s skillId=%d level=%d len=%d caller=0x%08X",
            (unsigned int)opcode,
            PacketRouteToString(packetRoute),
            observedSkillId,
            observedLevel,
            packetLen,
            (DWORD)(uintptr_t)callerRetAddr);
    }

    LogConfiguredPassiveSemanticBonusesForSkill(
        observedSkillId,
        packetRoute,
        opcode,
        callerRetAddr);
    if (ResolveConfiguredPassiveDamagePercentBonusForSkill(observedSkillId) != 0 ||
        ResolveConfiguredPassiveIgnoreDefensePercentBonusForSkill(observedSkillId) != 0 ||
        ResolveConfiguredPassiveAttackCountBonusForSkill(observedSkillId) != 0 ||
        ResolveConfiguredPassiveMobCountBonusForSkill(observedSkillId) != 0)
    {
        RememberRecentPassiveAttackProbe(observedSkillId);
    }

    int independentBuffSkillId = 0;
    if (TryResolveObservedIndependentBuffSkillId(observedSkillId, packetRoute, independentBuffSkillId))
    {
        MarkRecentIndependentBuffManualUse(
            independentBuffSkillId,
            observedSkillId,
            packetRoute,
            opcode,
            callerRetAddr);
    }

    std::vector<BYTE> expandedPacket;
    if (TryApplyConfiguredPassiveAttackCountPacketExpansion(
            packet,
            packetLen,
            packetRoute,
            skillIdOffset,
            observedSkillId,
            callerRetAddr,
            opcode,
            expandedPacket))
    {
        g_outgoingPacketRewriteBuffer.swap(expandedPacket);
        *packetDataSlot = g_outgoingPacketRewriteBuffer.empty()
            ? *packetDataSlot
            : &g_outgoingPacketRewriteBuffer[0];
        *packetLenSlot = (int)g_outgoingPacketRewriteBuffer.size();
        packet = static_cast<BYTE*>(*packetDataSlot);
        packetLen = *packetLenSlot;
    }

    TryApplyConfiguredPassiveDamagePacketRewrite(
        packet,
        packetLen,
        packetRoute,
        skillIdOffset,
        observedSkillId,
        callerRetAddr,
        opcode);

    if (TryRewritePacketFromActiveNativeRelease(
            packet,
            packetLen,
            packetRoute,
            skillIdOffset,
            skillLevelOffset,
            observedSkillId,
            callerRetAddr,
            opcode))
    {
        return;
    }

    if (packetRoute == CustomSkillPacketRoute_SpecialMove)
    {
        int mountedCustomSkillId = 0;
        int mountItemId = 0;
        if (TryResolveMountedRuntimeProxyCustomSkillId(
                observedSkillId,
                MountedRuntimeSkillKind_DemonJump,
                mountedCustomSkillId,
                &mountItemId))
        {
            WritePacketInt(packet, skillIdOffset, mountedCustomSkillId);

            int customLevel = GetTrackedSkillLevel(mountedCustomSkillId);
            if (customLevel <= 0)
                customLevel = 1;
            if (customLevel > 255)
                customLevel = 255;

            if (skillLevelOffset >= 0 && packetLen > skillLevelOffset)
                packet[skillLevelOffset] = (BYTE)customLevel;

            WriteLogFmt("[SkillPacket] mounted-runtime rewrite opcode=0x%X route=%s proxy=%d -> custom=%d mount=%d level=%d len=%d caller=0x%08X",
                (unsigned int)opcode,
                PacketRouteToString(packetRoute),
                observedSkillId,
                mountedCustomSkillId,
                mountItemId,
                customLevel,
                packetLen,
                (DWORD)(uintptr_t)callerRetAddr);
            return;
        }
    }

    CustomSkillUseRoute route = {};
    if (!FindRouteByProxySkillId(observedSkillId, packetRoute, route))
        return;

    WritePacketInt(packet, skillIdOffset, route.skillId);

    int customLevel = GetTrackedSkillLevel(route.skillId);
    if (customLevel <= 0)
        customLevel = 1;
    if (customLevel > 255)
        customLevel = 255;

    if (skillLevelOffset >= 0 && packetLen > skillLevelOffset)
        packet[skillLevelOffset] = (BYTE)customLevel;

    WriteLogFmt("[SkillPacket] rewrite opcode=0x%X route=%s proxy=%d -> custom=%d level=%d len=%d caller=0x%08X",
        (unsigned int)opcode,
        PacketRouteToString(packetRoute),
        observedSkillId,
        route.skillId,
        customLevel,
        packetLen,
        (DWORD)(uintptr_t)callerRetAddr);
}

void SkillOverlayBridgeInspectOutgoingPacket(void* packetData, int packetLen, uintptr_t callerRetAddr)
{
    void* mutablePacketData = packetData;
    int mutablePacketLen = packetLen;
    SkillOverlayBridgeInspectOutgoingPacketMutable(&mutablePacketData, &mutablePacketLen, callerRetAddr);
}

    void TryRewriteIndependentBuffGivePacket(BYTE* payload, int payloadLen, uintptr_t callerRetAddr)
    {
        if (!payload || payloadLen < kBuffMaskByteCount + 2 + 4)
            return;

        const unsigned short originalDisplayValue = ReadPacketShort(payload, kBuffMaskByteCount);
        int packetSkillId = 0;
        if (!TryReadSingleStatGiveBuffSkillId(payload, payloadLen, packetSkillId))
            return;
        int durationMs = 0;
        TryReadSingleStatGiveBuffDurationMs(payload, payloadLen, durationMs);

        for (std::map<int, SuperSkillDefinition>::const_iterator it = g_superSkillsBySkillId.begin();
             it != g_superSkillsBySkillId.end();
             ++it)
        {
            const SuperSkillDefinition& definition = it->second;
            const DWORD now = GetTickCount();
            std::map<int, DWORD>::const_iterator cancelIt = g_recentIndependentBuffClientCancelTickBySkillId.find(definition.skillId);
            if (cancelIt != g_recentIndependentBuffClientCancelTickBySkillId.end() &&
                now - cancelIt->second <= kIndependentBuffRefreshCancelIgnoreMs)
            {
                bool hasNewerManualUse = false;
                DWORD useDelta = 0;
                std::map<int, DWORD>::const_iterator useIt = g_recentIndependentBuffClientUseTickBySkillId.find(definition.skillId);
                if (useIt != g_recentIndependentBuffClientUseTickBySkillId.end() &&
                    useIt->second >= cancelIt->second)
                {
                    hasNewerManualUse = true;
                    useDelta = now - useIt->second;
                }

                if (!hasNewerManualUse)
                {
                    WriteLogFmt("[IndependentBuffClient] ignore refresh-give skillId=%d delta=%u caller=0x%08X",
                        definition.skillId,
                        (unsigned int)(now - cancelIt->second),
                        (DWORD)(uintptr_t)callerRetAddr);
                    continue;
                }

                WriteLogFmt("[IndependentBuffClient] allow give after manual-use skillId=%d cancelDelta=%u useDelta=%u caller=0x%08X",
                    definition.skillId,
                    (unsigned int)(now - cancelIt->second),
                    (unsigned int)useDelta,
                    (DWORD)(uintptr_t)callerRetAddr);
            }

            ActiveIndependentBuffRewriteState state;
            if (!BuildIndependentBuffRewriteState(definition, state))
                continue;
            if (packetSkillId != definition.skillId)
                continue;
            if (!PacketMaskHasValue(payload, payloadLen, state.carrierMaskPosition, state.carrierMaskValue))
                continue;

            UpdateLocalIndependentPotentialStateForDefinition(definition, true);
            UpdateIndependentBuffOverlayStateForDefinition(definition, durationMs);
            g_activeIndependentBuffRewriteStates[MakeIndependentBuffMaskKey(state.carrierMaskPosition, state.carrierMaskValue)] = state;

            if (!state.rewriteToNative)
            {
                WriteLogFmt("[IndependentBuffClient] give keep-carrier skillId=%d carrier=(%d,0x%08X) durationMs=%d caller=0x%08X",
                    definition.skillId,
                    state.carrierMaskPosition,
                    state.carrierMaskValue,
                    durationMs,
                    (DWORD)(uintptr_t)callerRetAddr);
                return;
            }

            if (!RewritePacketMaskValue(
                    payload,
                    payloadLen,
                    state.carrierMaskPosition,
                    state.carrierMaskValue,
                    state.nativeMaskPosition,
                    state.nativeMaskValue))
            {
                continue;
            }

            if (definition.independentNativeValueSpec.type != PassiveValueSpecType_None)
            {
                int sourceSkillId = definition.independentSourceSkillId > 0 ? definition.independentSourceSkillId : definition.skillId;
                int sourceSkillLevel = GetRuntimeAppliedSkillLevel(definition.skillId);
                if (sourceSkillLevel <= 0 && sourceSkillId != definition.skillId)
                    sourceSkillLevel = GetRuntimeAppliedSkillLevel(sourceSkillId);
                int nativeValue = 0;
                if (sourceSkillId > 0 &&
                    sourceSkillLevel > 0 &&
                    ResolvePassiveValueForLevel(sourceSkillId, sourceSkillLevel, definition.independentNativeValueSpec, nativeValue) &&
                    nativeValue >= 0 &&
                    nativeValue <= 0xFFFF)
                {
                    WritePacketShort(payload, kBuffMaskByteCount, static_cast<unsigned short>(nativeValue));
                }
                else
                {
                    WriteLogFmt("[IndependentBuffClient] give value lookup miss skillId=%d sourceSkillId=%d level=%d field=%s original=%u caller=0x%08X",
                        definition.skillId,
                        sourceSkillId,
                        sourceSkillLevel,
                        definition.independentNativeValueSpec.skillFieldName.c_str(),
                        static_cast<unsigned int>(originalDisplayValue),
                        (DWORD)(uintptr_t)callerRetAddr);
                }
            }

            const int displaySkillId = definition.independentNativeDisplaySkillId > 0
                ? definition.independentNativeDisplaySkillId
                : definition.skillId;
            WritePacketInt(payload, kBuffMaskByteCount + 2, displaySkillId);
            WriteLogFmt("[IndependentBuffClient] give rewrite skillId=%d carrier=(%d,0x%08X) native=(%d,0x%08X) packetSkillId=%d displaySkillId=%d value=%u->%u caller=0x%08X",
                definition.skillId,
                state.carrierMaskPosition,
                state.carrierMaskValue,
                state.nativeMaskPosition,
                state.nativeMaskValue,
                packetSkillId,
                displaySkillId,
                static_cast<unsigned int>(originalDisplayValue),
                static_cast<unsigned int>(ReadPacketShort(payload, kBuffMaskByteCount)),
                (DWORD)(uintptr_t)callerRetAddr);
            return;
        }
    }

    void TryRewriteIndependentBuffCancelPacket(BYTE* payload, int payloadLen, uintptr_t callerRetAddr)
    {
        if (!payload || payloadLen < kBuffMaskByteCount || g_activeIndependentBuffRewriteStates.empty())
            return;

        for (std::map<unsigned long long, ActiveIndependentBuffRewriteState>::iterator it = g_activeIndependentBuffRewriteStates.begin();
             it != g_activeIndependentBuffRewriteStates.end();)
        {
            const ActiveIndependentBuffRewriteState& state = it->second;
            const bool matchesCarrier =
                state.active &&
                PacketMaskHasValue(payload, payloadLen, state.carrierMaskPosition, state.carrierMaskValue);
            const bool matchesNative =
                state.active &&
                state.rewriteToNative &&
                PacketMaskHasValue(payload, payloadLen, state.nativeMaskPosition, state.nativeMaskValue);
            if (!matchesCarrier && !matchesNative)
            {
                ++it;
                continue;
            }

            const DWORD now = GetTickCount();
            bool recentClientCancel = false;
            std::map<int, DWORD>::iterator cancelIt = g_recentIndependentBuffClientCancelTickBySkillId.find(state.skillId);
            if (cancelIt != g_recentIndependentBuffClientCancelTickBySkillId.end())
            {
                if (now - cancelIt->second <= kIndependentBuffClientCancelWindowMs)
                {
                    recentClientCancel = true;
                }
                g_recentIndependentBuffClientCancelTickBySkillId.erase(cancelIt);
            }

            DWORD activationTick = state.activatedTick;
            std::map<int, DWORD>::iterator activationIt = g_recentIndependentBuffActivationTickBySkillId.find(state.skillId);
            if (activationIt != g_recentIndependentBuffActivationTickBySkillId.end())
                activationTick = activationIt->second;

            if (!recentClientCancel &&
                activationTick != 0 &&
                now - activationTick <= kIndependentBuffRefreshCancelIgnoreMs)
            {
                WriteLogFmt("[IndependentBuffClient] ignore refresh-cancel skillId=%d delta=%u carrier=(%d,0x%08X) caller=0x%08X",
                    state.skillId,
                    (unsigned int)(now - activationTick),
                    state.carrierMaskPosition,
                    state.carrierMaskValue,
                    (DWORD)(uintptr_t)callerRetAddr);
                return;
            }

            if (state.rewriteToNative)
            {
                if (matchesCarrier)
                {
                    RewritePacketMaskValue(
                        payload,
                        payloadLen,
                        state.carrierMaskPosition,
                        state.carrierMaskValue,
                        state.nativeMaskPosition,
                        state.nativeMaskValue);
                }
            }

            SuperSkillDefinition definition = {};
            if (FindSuperSkillDefinition(state.skillId, definition))
            {
                UpdateLocalIndependentPotentialStateForDefinition(definition, false);
                ClearIndependentBuffOverlayState(definition.skillId);
            }

            if (state.rewriteToNative)
            {
                WriteLogFmt("[IndependentBuffClient] cancel rewrite skillId=%d match=%s carrier=(%d,0x%08X) native=(%d,0x%08X) caller=0x%08X",
                    state.skillId,
                    matchesCarrier ? "carrier" : "native",
                    state.carrierMaskPosition,
                    state.carrierMaskValue,
                    state.nativeMaskPosition,
                    state.nativeMaskValue,
                    (DWORD)(uintptr_t)callerRetAddr);
            }
            else
            {
                WriteLogFmt("[IndependentBuffClient] cancel carrier-only skillId=%d carrier=(%d,0x%08X) caller=0x%08X",
                    state.skillId,
                    state.carrierMaskPosition,
                    state.carrierMaskValue,
                    (DWORD)(uintptr_t)callerRetAddr);
            }
            it = g_activeIndependentBuffRewriteStates.erase(it);
            return;
        }
    }

    void TryRewriteMountBuffGivePacket(BYTE* payload, int payloadLen, uintptr_t callerRetAddr)
    {
        int mountItemId = 0;
        int packetSkillId = 0;
        if (!TryReadMonsterRidingGiveBuffData(payload, payloadLen, mountItemId, packetSkillId))
            return;

        const int displaySkillId = ResolveMountBuffDisplaySkillId(mountItemId, packetSkillId);
        const int resolvedSkillId = displaySkillId > 0 ? displaySkillId : packetSkillId;
        int selectionSkillId = 0;
        if (!TryResolveMountedMovementSelectionSkillId(
                mountItemId,
                resolvedSkillId,
                0,
                selectionSkillId))
        {
            selectionSkillId = resolvedSkillId;
        }
        SuperSkillDefinition observedDefinition = {};
        if (selectionSkillId > 0 &&
            FindSuperSkillDefinition(selectionSkillId, observedDefinition) &&
            DefinitionParticipatesInMountMovementSelection(observedDefinition))
        {
            ObserveMountedMovementOverrideSelection(mountItemId, selectionSkillId);
        }

        if (displaySkillId <= 0 || displaySkillId == packetSkillId)
            return;

        WritePacketInt(payload, kMountGiveBuffSkillIdOffset, displaySkillId);
        WriteLogFmt("[MountBuffDisplay] rewrite mountItem=%d packetSkillId=%d displaySkillId=%d caller=0x%08X",
            mountItemId,
            packetSkillId,
            displaySkillId,
            (DWORD)(uintptr_t)callerRetAddr);
    }

void SkillOverlayBridgeInspectIncomingPacket(void* inPacket, int opcode, uintptr_t callerRetAddr)
{
    if (!inPacket)
        return;

    if (opcode == (int)kGiveBuffPacketOpcode || opcode == (int)kCancelBuffPacketOpcode)
    {
        BYTE* payload = nullptr;
        int payloadLen = 0;
        if (TryReadIncomingPacketPayload(inPacket, payload, payloadLen))
        {
            if (opcode == (int)kGiveBuffPacketOpcode)
            {
                TryRewriteIndependentBuffGivePacket(payload, payloadLen, callerRetAddr);
                TryRewriteMountBuffGivePacket(payload, payloadLen, callerRetAddr);
                RegisterNativeVisibleBuffStatesFromPayload(payload, payloadLen);
            }
            else
            {
                TryRewriteIndependentBuffCancelPacket(payload, payloadLen, callerRetAddr);
                RemoveNativeVisibleBuffStatesFromPayload(payload, payloadLen);
            }
        }
    }

    if (opcode == (int)kIndependentBuffVirtualGivePacketOpcode ||
        opcode == (int)kIndependentBuffVirtualCancelPacketOpcode)
    {
        BYTE* payload = nullptr;
        int payloadLen = 0;
        if (!TryReadIncomingPacketPayload(inPacket, payload, payloadLen) || !payload)
        {
            WriteLogFmt("[IndependentBuffVirtual] packet read FAIL opcode=0x%X caller=0x%08X",
                opcode,
                (DWORD)(uintptr_t)callerRetAddr);
            return;
        }

        if (opcode == (int)kIndependentBuffVirtualGivePacketOpcode)
        {
            if (payloadLen < 12)
            {
                WriteLogFmt("[IndependentBuffVirtual] give short len=%d caller=0x%08X",
                    payloadLen,
                    (DWORD)(uintptr_t)callerRetAddr);
                return;
            }

            const int skillId = ReadPacketInt(payload, 0);
            const int iconSkillId = ReadPacketInt(payload, 4);
            const int durationMs = ReadPacketInt(payload, 8);
            UpdateIndependentBuffVirtualState(skillId, iconSkillId, durationMs);
        }
        else
        {
            if (payloadLen < 4)
            {
                WriteLogFmt("[IndependentBuffVirtual] cancel short len=%d caller=0x%08X",
                    payloadLen,
                    (DWORD)(uintptr_t)callerRetAddr);
                return;
            }

            const int skillId = ReadPacketInt(payload, 0);
            ClearIndependentBuffVirtualState(skillId);
        }
        return;
    }

    BYTE* payload = nullptr;
    int payloadLen = 0;
    if (TryReadIncomingPacketPayload(inPacket, payload, payloadLen))
    {
        TryLogIncomingPassiveAttackProbe(opcode, payload, payloadLen, callerRetAddr);
        TryLogIncomingCloseRangeAttackPacket(opcode, payload, payloadLen, callerRetAddr);
    }

    if (opcode == (int)kSuperSkillLevelSyncPacketOpcode)
    {
        if (g_superSkillsBySkillId.empty())
            LoadSuperSkillRegistry();

        if (!payload || payloadLen < 4)
        {
            WriteLogFmt("[SuperSkill] level sync packet short opcode=0x%X len=%d caller=0x%08X",
                opcode,
                payloadLen,
                (DWORD)(uintptr_t)callerRetAddr);
            return;
        }

        const int entryCount = ReadPacketInt(payload, 0);
        if (entryCount < 0 || entryCount > kMaxReasonableSkillCount)
        {
            WriteLogFmt("[SuperSkill] level sync packet bad count=%d opcode=0x%X len=%d caller=0x%08X",
                entryCount,
                opcode,
                payloadLen,
                (DWORD)(uintptr_t)callerRetAddr);
            return;
        }

        const int expectedBytes = 4 + entryCount * 8;
        if (payloadLen < expectedBytes)
        {
            WriteLogFmt("[SuperSkill] level sync packet truncated count=%d len=%d need=%d caller=0x%08X",
                entryCount,
                payloadLen,
                expectedBytes,
                (DWORD)(uintptr_t)callerRetAddr);
            return;
        }

        int positiveCount = 0;
        for (int i = 0, cursor = 4; i < entryCount; ++i, cursor += 8)
        {
            const int skillId = ReadPacketInt(payload, cursor);
            int level = ReadPacketInt(payload, cursor + 4);
            if (level < 0)
                level = 0;
            if (level > 0)
                ++positiveCount;
            ApplyAuthoritativeSuperSkillLevelSync(skillId, level, "packet-sync");
        }

        const bool closedResetSyncWindow = IsSuperSkillResetLevelSyncWindowActive();
        if (closedResetSyncWindow)
            g_superSkillResetLevelSyncUntilTick = 0;

        g_lastRefreshTick = 0;
        g_fastRefreshUntilTick = GetTickCount() + kPendingRefreshWindowMs;
        WriteLogFmt("[SuperSkill] level sync recv count=%d positive=%d closeResetSync=%d opcode=0x%X caller=0x%08X",
            entryCount,
            positiveCount,
            closedResetSyncWindow ? 1 : 0,
            opcode,
            (DWORD)(uintptr_t)callerRetAddr);
        return;
    }

    if (opcode != (int)kSuperSkillResetPreviewPacketOpcode)
        return;

    uintptr_t base = 0;
    WORD length = 0;
    DWORD cursor = 0;
    const uintptr_t packetPtr = (uintptr_t)inPacket;
    if (!SafeReadValue(packetPtr + 0x8, base) ||
        !SafeReadValue(packetPtr + 0xC, length) ||
        !SafeReadValue(packetPtr + 0x14, cursor) ||
        !base)
    {
        WriteLogFmt("[SuperSkill] reset preview packet read FAIL opcode=0x%X inPacket=0x%08X caller=0x%08X",
            opcode,
            (DWORD)packetPtr,
            (DWORD)(uintptr_t)callerRetAddr);
        return;
    }

    if (cursor + 8 > (DWORD)length || SafeIsBadReadPtr((void*)(base + cursor), 8))
    {
        WriteLogFmt("[SuperSkill] reset preview packet short opcode=0x%X len=%u cursor=%u base=0x%08X",
            opcode,
            (unsigned int)length,
            (unsigned int)cursor,
            (DWORD)base);
        return;
    }

    int spentSp = 0;
    int costMeso = 0;
    int currentMeso = 0;
    bool hasCurrentMeso = false;
    if (!SafeReadValue(base + cursor, spentSp) ||
        !SafeReadValue(base + cursor + 4, costMeso))
    {
        WriteLogFmt("[SuperSkill] reset preview payload read FAIL opcode=0x%X len=%u cursor=%u",
            opcode,
            (unsigned int)length,
            (unsigned int)cursor);
        return;
    }

    if (cursor + 12 <= (DWORD)length &&
        !SafeIsBadReadPtr((void*)(base + cursor + 8), sizeof(int)) &&
        SafeReadValue(base + cursor + 8, currentMeso))
    {
        hasCurrentMeso = true;
    }

    if (spentSp < 0)
        spentSp = 0;
    if (costMeso < 0)
        costMeso = 0;
    if (currentMeso < 0)
        currentMeso = 0;

    g_superSkillResetPreviewSpentSp = spentSp;
    g_superSkillResetPreviewCostMeso = costMeso;
    g_superSkillResetPreviewCurrentMeso = hasCurrentMeso ? currentMeso : 0;
    g_superSkillResetPreviewHasCurrentMeso = hasCurrentMeso ? 1 : 0;
    InterlockedIncrement(&g_superSkillResetPreviewRevision);
    SafeWriteValue(packetPtr + 0x14, cursor + (hasCurrentMeso ? 12 : 8));

    WriteLogFmt("[SuperSkill] reset preview recv spentSp=%d cost=%d meso=%d hasMeso=%d opcode=0x%X rev=%ld caller=0x%08X",
        spentSp,
        costMeso,
        currentMeso,
        hasCurrentMeso ? 1 : 0,
        opcode,
        (long)g_superSkillResetPreviewRevision,
        (DWORD)(uintptr_t)callerRetAddr);
}

bool TryReleaseSkillViaNativeB2F370(int skillId);

bool SkillOverlayBridgeUseSkill(int skillId)
{
    if (skillId <= 0)
        return false;

    SkillManager* mgr = nullptr;
    if (g_bridge.managerSource.userData)
    {
        struct MSS { SkillManager* m; };
        MSS* mss = static_cast<MSS*>(g_bridge.managerSource.userData);
        mgr = mss->m;
    }

    if (!mgr)
        return false;

    for (int t = 0; t < mgr->tabCount; ++t)
    {
        SkillTab& tab = mgr->tabs[t];
        for (int i = 0; i < tab.count; ++i)
        {
            if (tab.skills[i].skillID == skillId)
            {
                SkillItem& skill = tab.skills[i];
                if (!skill.isEnabled || skill.level <= 0)
                {
                    WriteLogFmt("[SkillBridge] use BLOCKED skillId=%d (disabled or level 0)", skillId);
                    return false;
                }
                if (skill.isPassive)
                {
                    WriteLogFmt("[SkillBridge] use BLOCKED skillId=%d (passive)", skillId);
                    return false;
                }
                if (skill.IsOnCooldown())
                {
                    WriteLogFmt("[SkillBridge] use BLOCKED skillId=%d (cooldown)", skillId);
                    return false;
                }

                g_recentIndependentBuffClientUseTickBySkillId[skillId] = GetTickCount();
                g_recentIndependentBuffClientCancelTickBySkillId.erase(skillId);
                ClearIndependentBuffVirtualState(skillId);

                SuperSkillDefinition observedDefinition = {};
                if (FindSuperSkillDefinition(skillId, observedDefinition) &&
                    DefinitionParticipatesInMountMovementSelection(observedDefinition) &&
                    observedDefinition.mountItemId > 0)
                {
                    ObserveMountedMovementOverrideSelection(
                        observedDefinition.mountItemId,
                        skillId);
                }

                const bool releaseOk = TryReleaseSkillViaNativeB2F370(skillId);
                if (!releaseOk)
                {
                    WriteLogFmt("[SkillBridge] use FAILED skillId=%d level=%d tab=%d (release failed)",
                        skillId,
                        skill.level,
                        t);
                    return false;
                }

                skill.Use();
                WriteLogFmt("[SkillBridge] use skillId=%d level=%d tab=%d release=OK", skillId, skill.level, t);
                return true;
            }
        }
    }

    WriteLogFmt("[SkillBridge] use FAILED skillId=%d (not found)", skillId);
    return false;
}

// ============================================================================
// Native quickslot assignment
//
// 证据级别：A — IDA asm/pseudo 直接确认
//
// 数据结构：
//   keyArray = *(DWORD*)0xF617A8  -- 89项×5字节: [type:1byte][action:4bytes]
//   slotTable = *(DWORD*)0xF619D0 -- DWORD数组, [slot+1] = key_index
//   entry = keyArray + 5 * key_index + 4
//
// 调用约定：
//   sub_B9A5D0: __thiscall(ECX=StatusBar_this, push 0), retn 4
//   sub_5E6F90: __thiscall(ECX=keyArray), bare retn
//
// 这次改动属于 overlay 本地逻辑 + 游戏原生 call
// 涉及原生 call，证据为 A 级 (asm 直接确认)
// 做完后需验证：技能是否真正出现在游戏快捷栏、服务端是否收到 CHANGE_KEYMAP
// 本轮完成了"拖到技能栏 → 调用游戏原生赋值"完整子功能闭环
// ============================================================================
    bool SkillOverlayBridgeAssignSkillToQuickSlot(int slotIndex, int skillId)
    {
        if (slotIndex < 0 || slotIndex >= SKILL_BAR_TOTAL_SLOTS || skillId <= 0)
            return false;

    int nativeSkillId = skillId;
    CustomSkillUseRoute route = {};
    if (FindRouteByCustomSkillId(skillId, route))
    {
        if (RouteUsesNativeReleaseClass(route))
        {
            nativeSkillId = skillId;
            WriteLogFmt("[SkillBridge] assignSlot native-keymap custom=%d route=%s releaseClass=%s",
                skillId,
                PacketRouteToString(route.packetRoute),
                ReleaseClassToString(route.releaseClass));
        }
        else if (RouteUsesProxySkill(route))
        {
            if (route.releaseClass == CustomSkillReleaseClass_NativeClassifierProxy)
            {
                nativeSkillId = skillId;
                WriteLogFmt("[SkillBridge] assignSlot native-keymap custom=%d route=%s releaseClass=%s",
                    skillId,
                    PacketRouteToString(route.packetRoute),
                    ReleaseClassToString(route.releaseClass));
            }
            else
            {
                nativeSkillId = route.proxySkillId;
                WriteLogFmt("[SkillBridge] assignSlot route custom=%d proxy=%d route=%s",
                    skillId, nativeSkillId, PacketRouteToString(route.packetRoute));
            }
        }
    }

    // 1. 读全局指针
    DWORD keyArray = *(DWORD*)ADDR_KeyArrayPtr;
    DWORD slotTable = *(DWORD*)ADDR_SlotTablePtr;

    if (!keyArray || !slotTable)
    {
        WriteLogFmt("[SkillBridge] assignSlot FAIL: keyArray=0x%08X slotTable=0x%08X", keyArray, slotTable);
        return false;
    }

    // Startup/login guard: defer native assignment until core runtime objects exist.
    DWORD statusBarReady = *(DWORD*)ADDR_StatusBar;
    DWORD contextReady = GetSkillContext();
    DWORD skillMgrReady = GetSkillDataMgr();
    if (!contextReady || !skillMgrReady)
    {
        WriteLogFmt("[SkillBridge] assignSlot WARN: slot=%d skillId=%d statusBar=0x%08X context=0x%08X mgr=0x%08X (continuing with keymap write)",
            slotIndex, skillId, statusBarReady, contextReady, skillMgrReady);
    }

    // 安全检查
    if (SafeIsBadReadPtr((void*)keyArray, 89 * 5 + 4) || SafeIsBadReadPtr((void*)slotTable, 9 * 4))
    {
        WriteLogFmt("[SkillBridge] assignSlot FAIL: bad pointer keyArray=0x%08X slotTable=0x%08X", keyArray, slotTable);
        return false;
    }

    // 2. slot_index → key_index
    //    slotTable layout: [0]=?, [1..8]=slot 0~7 的 key_index
    int keyIndex = *(int*)(slotTable + 4 * (slotIndex + 1));
    if (keyIndex == 54) keyIndex = 42;  // 游戏内特殊重映射

    if (keyIndex < 0 || keyIndex >= 89)
    {
        WriteLogFmt("[SkillBridge] assignSlot FAIL: slot=%d → keyIndex=%d (out of range)", slotIndex, keyIndex);
        return false;
    }

    // 3. 清除旧绑定：遍历 89 个 entry，清除 type==1 && action==skillId 的条目
    for (int i = 0; i < 89; ++i)
    {
        DWORD entryAddr = keyArray + 5 * i + 4;
        BYTE entryType = *(BYTE*)entryAddr;
        DWORD entryAction = *(DWORD*)(entryAddr + 1);
        if ((entryType == 1 || entryType == 8) &&
            ((int)entryAction == nativeSkillId || (int)entryAction == skillId))
        {
            *(BYTE*)entryAddr = 0;
            *(DWORD*)(entryAddr + 1) = 0;
        }
    }

    // 4. 写入新绑定
    DWORD targetEntry = keyArray + 5 * keyIndex + 4;
    *(BYTE*)targetEntry = 1;              // type = 1 (skill)
    *(DWORD*)(targetEntry + 1) = (DWORD)nativeSkillId;

    WriteLogFmt("[SkillBridge] assignSlot OK: slot=%d keyIndex=%d skillId=%d native=%d entry=0x%08X",
        slotIndex, keyIndex, skillId, nativeSkillId, targetEntry);

    // 5. 刷新 UI：优先使用全局 StatusBar，拿不到时回退到 runtime hook 观察到的实例
    DWORD globalStatusBar = 0;
    SafeReadValue(ADDR_StatusBar, globalStatusBar);
    DWORD observedStatusBar = static_cast<DWORD>(SkillOverlayBridgeGetObservedStatusBarPtr());
    DWORD statusBar = 0;
    const char* statusSource = "none";
    if (globalStatusBar && !SafeIsBadReadPtr((void*)globalStatusBar, 0xB30 + 4))
    {
        statusBar = globalStatusBar;
        statusSource = "global";
        SkillOverlayBridgeSetObservedStatusBarPtr(statusBar);
    }
    else if (observedStatusBar && !SafeIsBadReadPtr((void*)observedStatusBar, 0xB30 + 4))
    {
        statusBar = observedStatusBar;
        statusSource = "observed";
    }

    const uintptr_t skillWndThis = GetLiveSkillWndThis();
    if (statusBar)
    {
        __try {
            typedef void(__thiscall* tStatusBarRefreshInternalFn)(uintptr_t thisPtr);
            ((tStatusBarRefreshInternalFn)ADDR_StatusBarRefreshInternal)(statusBar);
        } __except(EXCEPTION_EXECUTE_HANDLER) {
            WriteLogFmt("[SkillBridge] EXCEPTION in status bar internal refresh: 0x%08X", GetExceptionCode());
        }

        DWORD fnRefresh = ADDR_B9A5D0;
        __try {
            __asm {
                push 0
                mov ecx, statusBar
                call fnRefresh
            }
        } __except(EXCEPTION_EXECUTE_HANDLER) {
            WriteLogFmt("[SkillBridge] EXCEPTION in sub_B9A5D0: 0x%08X", GetExceptionCode());
        }

        if (skillWndThis && !SafeIsBadReadPtr(reinterpret_cast<void*>(skillWndThis), 0x40))
        {
            __try
            {
                typedef int(__thiscall* tSkillWndRefreshFn)(uintptr_t thisPtr);
                ((tSkillWndRefreshFn)ADDR_9E1770)(skillWndThis);
            }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
                WriteLogFmt("[SkillBridge] EXCEPTION in skill wnd refresh: 0x%08X", GetExceptionCode());
            }
        }

        const HWND hwnd = GetForegroundWindow();
        DWORD hwndPid = 0;
        if (hwnd)
            GetWindowThreadProcessId(hwnd, &hwndPid);
        if (hwnd && hwndPid == GetCurrentProcessId())
        {
            InvalidateRect(hwnd, nullptr, FALSE);
            UpdateWindow(hwnd);
        }

        WriteLogFmt("[SkillBridge] refresh UI after assign statusBar=0x%08X source=%s skillWnd=0x%08X",
            statusBar,
            statusSource,
            (DWORD)skillWndThis);
    }
    else
    {
        WriteLogFmt("[SkillBridge] WARN: StatusBar global=0x%08X observed=0x%08X, skip UI refresh",
            globalStatusBar,
            observedStatusBar);
    }

    // 6. 发包 CHANGE_KEYMAP: sub_5E6F90(__thiscall, ECX=keyArray)
    {
        DWORD fnSendKeymap = ADDR_5E6F90;
        DWORD ecxVal = keyArray;
        __try {
            __asm {
                mov ecx, ecxVal
                call fnSendKeymap
            }
        } __except(EXCEPTION_EXECUTE_HANDLER) {
            WriteLogFmt("[SkillBridge] EXCEPTION in sub_5E6F90: 0x%08X", GetExceptionCode());
            return false;
        }
    }

    return true;
}

    bool TryReleaseSkillViaNativeB2F370(int skillId)
    {
        if (skillId <= 0)
            return false;

        if (SafeIsBadReadPtr((void*)ADDR_UserLocal, 4))
        {
            WriteLogFmt("[SkillBridge] release via native dblclick FAIL skillId=%d (UserLocal ptr unreadable)", skillId);
            return false;
        }

        const DWORD userLocal = *(DWORD*)ADDR_UserLocal;
        if (!userLocal || SafeIsBadReadPtr((void*)userLocal, 4))
        {
            WriteLogFmt("[SkillBridge] release via native dblclick FAIL skillId=%d userLocal=0x%08X",
                skillId,
                userLocal);
            return false;
        }

        DWORD fnRelease = ADDR_B2F370;
        DWORD savedEsp = 0;
        DWORD result = 0;
        bool ok = false;
        __try
        {
            __asm
            {
                mov savedEsp, esp
                push 0
                push 0
                push 0
                push skillId
                mov ecx, userLocal
                mov edx, fnRelease
                call edx
                mov result, eax
                mov esp, savedEsp
            }
            ok = true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            WriteLogFmt("[SkillBridge] release via native dblclick EXCEPTION skillId=%d userLocal=0x%08X code=0x%08X",
                skillId,
                userLocal,
                GetExceptionCode());
            return false;
        }

        WriteLogFmt("[SkillBridge] release via native dblclick skillId=%d userLocal=0x%08X eax=0x%08X",
            skillId,
            userLocal,
            result);
        return ok;
    }
