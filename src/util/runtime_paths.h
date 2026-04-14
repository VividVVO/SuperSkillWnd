#pragma once

#include <string>

namespace ssw
{
namespace path
{

std::string WideToUtf8(const std::wstring& text);

bool FileExists(const wchar_t* path);
bool FileExists(const std::wstring& path);
bool DirectoryExists(const std::wstring& path);

std::wstring TrimTrailingSlash(std::wstring path);
std::wstring Combine(const std::wstring& left, const wchar_t* right);
std::wstring Parent(const std::wstring& path);

std::wstring GetHookDllDirectory();
bool LooksLikeSuperSkillRoot(const std::wstring& dir);
std::wstring ResolveRootDirectoryFromHook();
std::wstring ResolveSkillConfigDir(const std::wstring& rootDir, const std::wstring& dllDir);

} // namespace path
} // namespace ssw

