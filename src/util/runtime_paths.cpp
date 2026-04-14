#include "util/runtime_paths.h"

#include <windows.h>

#include <vector>

namespace ssw
{
namespace path
{

std::string WideToUtf8(const std::wstring& text)
{
    if (text.empty())
        return std::string();

    const int length = ::WideCharToMultiByte(CP_UTF8, 0, text.c_str(), -1, nullptr, 0, nullptr, nullptr);
    if (length <= 1)
        return std::string();

    std::string result;
    result.resize(static_cast<size_t>(length - 1));
    ::WideCharToMultiByte(CP_UTF8, 0, text.c_str(), -1, &result[0], length, nullptr, nullptr);
    return result;
}

bool FileExists(const wchar_t* path)
{
    if (!path || !path[0])
        return false;
    const DWORD attributes = ::GetFileAttributesW(path);
    return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY) == 0;
}

bool FileExists(const std::wstring& path)
{
    return FileExists(path.c_str());
}

bool DirectoryExists(const std::wstring& path)
{
    if (path.empty())
        return false;
    const DWORD attributes = ::GetFileAttributesW(path.c_str());
    return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
}

std::wstring TrimTrailingSlash(std::wstring path)
{
    while (!path.empty() && (path[path.size() - 1] == L'\\' || path[path.size() - 1] == L'/'))
        path.resize(path.size() - 1);
    return path;
}

std::wstring Combine(const std::wstring& left, const wchar_t* right)
{
    if (left.empty())
        return right ? std::wstring(right) : std::wstring();
    if (!right || !right[0])
        return left;

    std::wstring result = TrimTrailingSlash(left);
    result += L"\\";
    result += right;
    return result;
}

std::wstring Parent(const std::wstring& path)
{
    const std::wstring trimmed = TrimTrailingSlash(path);
    const size_t slashPos = trimmed.find_last_of(L"\\/");
    if (slashPos == std::wstring::npos)
        return std::wstring();
    return trimmed.substr(0, slashPos);
}

std::wstring GetHookDllDirectory()
{
    HMODULE module = nullptr;
    if (!::GetModuleHandleExW(
            GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            reinterpret_cast<LPCWSTR>(&GetHookDllDirectory),
            &module))
    {
        return std::wstring();
    }

    wchar_t modulePath[MAX_PATH] = {};
    const DWORD length = ::GetModuleFileNameW(module, modulePath, MAX_PATH);
    if (length == 0 || length >= MAX_PATH)
        return std::wstring();

    return Parent(std::wstring(modulePath, modulePath + length));
}

bool LooksLikeSuperSkillRoot(const std::wstring& dir)
{
    if (dir.empty())
        return false;

    if (DirectoryExists(Combine(dir, L"Data\\Plugins\\SS\\Skill")))
        return true;
    if (DirectoryExists(Combine(dir, L"skill")))
        return true;
    if (DirectoryExists(Combine(dir, L"skill2")))
        return true;
    if (FileExists(Combine(dir, L"build\\Reader\\SkillImgReader.dll")))
        return true;
    if (FileExists(Combine(dir, L"Reader\\SkillImgReader.dll")))
        return true;
    if (FileExists(Combine(dir, L"build\\SkillImgReader\\SkillImgReader.dll")))
        return true;
    if (FileExists(Combine(dir, L"SkillImgReader\\SkillImgReader.dll")))
        return true;
    if (DirectoryExists(Combine(dir, L"Data")))
        return true;
    return false;
}

std::wstring ResolveRootDirectoryFromHook()
{
    const std::wstring dllDir = GetHookDllDirectory();
    if (dllDir.empty())
        return std::wstring();

    std::vector<std::wstring> candidates;
    candidates.push_back(dllDir);

    const std::wstring parent = Parent(dllDir);
    if (!parent.empty())
        candidates.push_back(parent);

    const std::wstring grandParent = parent.empty() ? std::wstring() : Parent(parent);
    if (!grandParent.empty())
        candidates.push_back(grandParent);

    for (size_t i = 0; i < candidates.size(); ++i)
    {
        if (DirectoryExists(Combine(candidates[i], L"skill")) ||
            DirectoryExists(Combine(candidates[i], L"skill2")))
            return TrimTrailingSlash(candidates[i]);
    }

    for (size_t i = 0; i < candidates.size(); ++i)
    {
        if (DirectoryExists(Combine(candidates[i], L"Data")))
            return TrimTrailingSlash(candidates[i]);
    }

    for (size_t i = 0; i < candidates.size(); ++i)
    {
        if (LooksLikeSuperSkillRoot(candidates[i]))
            return TrimTrailingSlash(candidates[i]);
    }

    return TrimTrailingSlash(dllDir);
}

std::wstring ResolveSkillConfigDir(const std::wstring& rootDir, const std::wstring& dllDir)
{
    const std::wstring primary = Combine(dllDir, L"SS\\Skill");
    if (DirectoryExists(primary))
        return TrimTrailingSlash(primary);

    const std::wstring pluginRoot = Combine(rootDir, L"Data\\Plugins\\SS\\Skill");
    if (DirectoryExists(pluginRoot))
        return TrimTrailingSlash(pluginRoot);

    const std::wstring legacyPrimary = Combine(rootDir, L"skill");
    if (DirectoryExists(legacyPrimary))
        return TrimTrailingSlash(legacyPrimary);

    const std::wstring legacy = Combine(rootDir, L"skill2");
    if (DirectoryExists(legacy))
        return TrimTrailingSlash(legacy);

    return TrimTrailingSlash(primary);
}

} // namespace path
} // namespace ssw

