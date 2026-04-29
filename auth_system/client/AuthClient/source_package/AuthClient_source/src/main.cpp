#define NOMINMAX
#include <windows.h>
#include <commctrl.h>
#include <windowsx.h>
#include <winhttp.h>
#include <wincrypt.h>

#include <cctype>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <cwctype>
#include <filesystem>
#include <fstream>
#include <optional>
#include <sstream>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include "auth_codec.h"

// -----------------------------------------------------------------------------
// 编译器指令：启用现代视觉样式并强制隐藏 CMD 黑框
// -----------------------------------------------------------------------------
#pragma comment(linker, "/SUBSYSTEM:WINDOWS")
#pragma comment(linker, "\"/manifestdependency:type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")

#pragma comment(lib, "winhttp.lib")
#pragma comment(lib, "crypt32.lib")

namespace {

#ifndef AUTH_CLIENT_DEFAULT_SERVER_URL
#define AUTH_CLIENT_DEFAULT_SERVER_URL L"http://159.75.226.54:8080"
#endif

constexpr int kWindowWidth = 420;
constexpr int kWindowHeight = 240;

constexpr int IDC_LABEL_TITLE = 1000;
constexpr int IDC_LABEL_IP = 1002;
constexpr int IDC_EDIT_IP = 1003;
constexpr int IDC_BUTTON_ACTIVATE = 1005;
constexpr int IDC_BUTTON_CLEAR = 1006;

constexpr UINT WM_APP_IP_READY = WM_APP + 1;
constexpr const wchar_t kDefaultServerUrl[] = AUTH_CLIENT_DEFAULT_SERVER_URL;

struct AppConfig {
    std::wstring serverUrl = kDefaultServerUrl;
    std::wstring internalServerUrl;
    std::wstring outputPath = L"C:\\Windows\\hfy";
    DWORD timeoutMs = 5000;
};

struct AppState {
    AppConfig config;
    std::filesystem::path exeDir;
    HWND titleLabel = nullptr;
    HWND ipLabel = nullptr;
    HWND ipEdit = nullptr;
    HWND activateButton = nullptr;
    HWND clearButton = nullptr;
    
    HFONT titleFont = nullptr;
    HFONT normalFont = nullptr;
    HBRUSH bgBrush = nullptr;
    HBRUSH editBrush = nullptr;
};

struct HttpResponse {
    DWORD statusCode = 0;
    std::string body;
    std::wstring error;
};

std::wstring TrimWide(std::wstring value) {
    const auto isSpace = [](wchar_t ch) { return std::iswspace(ch) != 0; };
    while (!value.empty() && isSpace(value.front())) {
        value.erase(value.begin());
    }
    while (!value.empty() && isSpace(value.back())) {
        value.pop_back();
    }
    return value;
}

std::string TrimUtf8(std::string value) {
    const auto isSpace = [](unsigned char ch) { return std::isspace(ch) != 0; };
    while (!value.empty() && isSpace(static_cast<unsigned char>(value.front()))) {
        value.erase(value.begin());
    }
    while (!value.empty() && isSpace(static_cast<unsigned char>(value.back()))) {
        value.pop_back();
    }
    return value;
}

std::wstring Utf8ToWide(const std::string& text) {
    if (text.empty()) {
        return std::wstring();
    }
    int length = MultiByteToWideChar(CP_UTF8, 0, text.data(),
                                     static_cast<int>(text.size()), nullptr, 0);
    if (length <= 0) {
        return std::wstring();
    }
    std::wstring out(static_cast<std::size_t>(length), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()),
                        out.data(), length);
    return out;
}

std::string WideToUtf8(const std::wstring& text) {
    if (text.empty()) {
        return std::string();
    }
    int length = WideCharToMultiByte(CP_UTF8, 0, text.data(),
                                     static_cast<int>(text.size()), nullptr, 0,
                                     nullptr, nullptr);
    if (length <= 0) {
        return std::string();
    }
    std::string out(static_cast<std::size_t>(length), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()),
                        out.data(), length, nullptr, nullptr);
    return out;
}

std::wstring GetLastErrorMessage(DWORD errorCode) {
    LPWSTR buffer = nullptr;
    const DWORD size = FormatMessageW(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
            FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr, errorCode, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
        reinterpret_cast<LPWSTR>(&buffer), 0, nullptr);
    std::wstring message =
        (size > 0 && buffer) ? std::wstring(buffer, buffer + size) : L"未知错误";
    if (buffer) {
        LocalFree(buffer);
    }
    return TrimWide(message);
}

std::filesystem::path GetExecutableDir() {
    std::wstring buffer(MAX_PATH, L'\0');
    DWORD length = GetModuleFileNameW(nullptr, buffer.data(),
                                      static_cast<DWORD>(buffer.size()));
    while (length == buffer.size()) {
        buffer.resize(buffer.size() * 2);
        length = GetModuleFileNameW(nullptr, buffer.data(),
                                    static_cast<DWORD>(buffer.size()));
    }
    buffer.resize(length);
    return std::filesystem::path(buffer).parent_path();
}

std::optional<std::string> ReadTextFileUtf8(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        return std::nullopt;
    }
    std::ostringstream buffer;
    buffer << input.rdbuf();
    std::string text = buffer.str();
    if (text.size() >= 3 && static_cast<unsigned char>(text[0]) == 0xEF &&
        static_cast<unsigned char>(text[1]) == 0xBB &&
        static_cast<unsigned char>(text[2]) == 0xBF) {
        text.erase(0, 3);
    }
    return text;
}

bool ReadBinaryFile(const std::filesystem::path& path,
                    std::vector<std::uint8_t>* out) {
    if (!out) {
        return false;
    }
    out->clear();
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        return false;
    }
    input.seekg(0, std::ios::end);
    const auto size = input.tellg();
    input.seekg(0, std::ios::beg);
    if (size < 0) {
        return false;
    }
    out->resize(static_cast<std::size_t>(size));
    if (!out->empty()) {
        input.read(reinterpret_cast<char*>(out->data()),
                   static_cast<std::streamsize>(out->size()));
    }
    return input.good() || input.eof();
}

bool WriteBinaryFile(const std::filesystem::path& path,
                     const std::vector<std::uint8_t>& data) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output) {
        return false;
    }
    if (!data.empty()) {
        output.write(reinterpret_cast<const char*>(data.data()),
                     static_cast<std::streamsize>(data.size()));
    }
    return output.good();
}

std::wstring ToLowerCopy(std::wstring value) {
    for (auto& ch : value) {
        ch = static_cast<wchar_t>(std::towlower(ch));
    }
    return value;
}

AppConfig LoadConfig(const std::filesystem::path& exeDir) {
    AppConfig config;
    const auto configPath = exeDir / "auth_client.ini";
    const auto text = ReadTextFileUtf8(configPath);
    if (!text) {
        return config;
    }

    std::istringstream input(*text);
    std::string line;
    while (std::getline(input, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        const auto trimmed = TrimUtf8(line);
        if (trimmed.empty() || trimmed[0] == '#' || trimmed[0] == ';') {
            continue;
        }

        const auto pos = trimmed.find('=');
        if (pos == std::string::npos) {
            continue;
        }

        const std::string key = TrimUtf8(trimmed.substr(0, pos));
        const std::string value = TrimUtf8(trimmed.substr(pos + 1));

        if (key == "server_url") {
            config.serverUrl = Utf8ToWide(value);
        } else if (key == "internal_server_url") {
            config.internalServerUrl = Utf8ToWide(value);
        } else if (key == "output_path") {
            config.outputPath = Utf8ToWide(value);
        } else if (key == "timeout_ms") {
            const auto parsed = std::strtoul(value.c_str(), nullptr, 10);
            if (parsed > 0) {
                config.timeoutMs = static_cast<DWORD>(parsed);
            }
        }
    }

    return config;
}

AppState* GetState(HWND hwnd) {
    return reinterpret_cast<AppState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
}

HMENU ControlIdToMenu(int controlId) {
    return reinterpret_cast<HMENU>(static_cast<INT_PTR>(controlId));
}

std::filesystem::path ResolvePath(const AppState& state, const std::wstring& rawPath) {
    std::filesystem::path path(rawPath);
    if (path.is_absolute()) {
        return path;
    }
    return state.exeDir / path;
}

std::wstring BuildUrl(const std::wstring& baseUrl, const std::wstring& suffix) {
    if (baseUrl.empty()) {
        return suffix;
    }
    if (!suffix.empty() && suffix.front() == L'/') {
        if (!baseUrl.empty() && baseUrl.back() == L'/') {
            return baseUrl.substr(0, baseUrl.size() - 1) + suffix;
        }
        return baseUrl + suffix;
    }
    if (!baseUrl.empty() && baseUrl.back() == L'/') {
        return baseUrl + suffix;
    }
    return baseUrl + L"/" + suffix;
}

std::wstring CurrentServerBaseUrl(const AppState& state) {
    return state.config.serverUrl;
}

HttpResponse HttpRequest(const std::wstring& method, const std::wstring& url,
                         const std::string& body, DWORD timeoutMs) {
    HttpResponse response;
    URL_COMPONENTS components{};
    components.dwStructSize = sizeof(components);
    components.dwSchemeLength = static_cast<DWORD>(-1);
    components.dwHostNameLength = static_cast<DWORD>(-1);
    components.dwUrlPathLength = static_cast<DWORD>(-1);
    components.dwExtraInfoLength = static_cast<DWORD>(-1);

    if (!WinHttpCrackUrl(url.c_str(), 0, 0, &components)) {
        response.error = L"解析服务器地址失败: " + GetLastErrorMessage(GetLastError());
        return response;
    }

    const std::wstring host(components.lpszHostName,
                            components.dwHostNameLength);
    std::wstring path(components.lpszUrlPath, components.dwUrlPathLength);
    if (path.empty()) {
        path = L"/";
    }
    if (components.dwExtraInfoLength > 0) {
        path.append(components.lpszExtraInfo, components.dwExtraInfoLength);
    }

    const bool secure = components.nScheme == INTERNET_SCHEME_HTTPS;
    HINTERNET session = WinHttpOpen(L"AuthClient/1.0",
                                    WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                    WINHTTP_NO_PROXY_NAME,
                                    WINHTTP_NO_PROXY_BYPASS, 0);
    if (!session) {
        response.error = L"创建 HTTP 会话失败: " + GetLastErrorMessage(GetLastError());
        return response;
    }

    WinHttpSetTimeouts(session, timeoutMs, timeoutMs, timeoutMs, timeoutMs);

    HINTERNET connection = WinHttpConnect(session, host.c_str(), components.nPort, 0);
    if (!connection) {
        response.error = L"连接服务器失败: " + GetLastErrorMessage(GetLastError());
        WinHttpCloseHandle(session);
        return response;
    }

    HINTERNET request =
        WinHttpOpenRequest(connection, method.c_str(), path.c_str(), nullptr,
                           WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
                           secure ? WINHTTP_FLAG_SECURE : 0);
    if (!request) {
        response.error = L"创建请求失败: " + GetLastErrorMessage(GetLastError());
        WinHttpCloseHandle(connection);
        WinHttpCloseHandle(session);
        return response;
    }

    const wchar_t* headers = body.empty() ? L"" : L"Content-Type: application/json\r\n";
    const DWORD headerLength =
        body.empty() ? 0 : static_cast<DWORD>(wcslen(headers));

    if (!WinHttpSendRequest(
            request, headerLength > 0 ? headers : WINHTTP_NO_ADDITIONAL_HEADERS,
            headerLength, body.empty() ? WINHTTP_NO_REQUEST_DATA
                                       : const_cast<char*>(body.data()),
            static_cast<DWORD>(body.size()), static_cast<DWORD>(body.size()), 0)) {
        response.error = L"发送请求失败: " + GetLastErrorMessage(GetLastError());
        WinHttpCloseHandle(request);
        WinHttpCloseHandle(connection);
        WinHttpCloseHandle(session);
        return response;
    }

    if (!WinHttpReceiveResponse(request, nullptr)) {
        response.error = L"接收响应失败: " + GetLastErrorMessage(GetLastError());
        WinHttpCloseHandle(request);
        WinHttpCloseHandle(connection);
        WinHttpCloseHandle(session);
        return response;
    }

    DWORD statusCode = 0;
    DWORD statusSize = sizeof(statusCode);
    WinHttpQueryHeaders(request,
                        WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                        WINHTTP_HEADER_NAME_BY_INDEX, &statusCode, &statusSize,
                        WINHTTP_NO_HEADER_INDEX);
    response.statusCode = statusCode;

    while (true) {
        DWORD available = 0;
        if (!WinHttpQueryDataAvailable(request, &available)) {
            response.error = L"读取响应失败: " + GetLastErrorMessage(GetLastError());
            break;
        }
        if (available == 0) {
            break;
        }

        std::string chunk(static_cast<std::size_t>(available), '\0');
        DWORD read = 0;
        if (!WinHttpReadData(request, chunk.data(), available, &read)) {
            response.error = L"读取响应内容失败: " + GetLastErrorMessage(GetLastError());
            break;
        }
        chunk.resize(static_cast<std::size_t>(read));
        response.body += chunk;
    }

    WinHttpCloseHandle(request);
    WinHttpCloseHandle(connection);
    WinHttpCloseHandle(session);
    return response;
}

void AppendUtf8Codepoint(std::string* out, unsigned codepoint) {
    if (!out) {
    }
    if (codepoint <= 0x7F) {
        out->push_back(static_cast<char>(codepoint));
    } else if (codepoint <= 0x7FF) {
        out->push_back(static_cast<char>(0xC0 | (codepoint >> 6U)));
        out->push_back(static_cast<char>(0x80 | (codepoint & 0x3FU)));
    } else if (codepoint <= 0xFFFF) {
        out->push_back(static_cast<char>(0xE0 | (codepoint >> 12U)));
        out->push_back(static_cast<char>(0x80 | ((codepoint >> 6U) & 0x3FU)));
        out->push_back(static_cast<char>(0x80 | (codepoint & 0x3FU)));
    } else {
        out->push_back(static_cast<char>(0xF0 | (codepoint >> 18U)));
        out->push_back(static_cast<char>(0x80 | ((codepoint >> 12U) & 0x3FU)));
        out->push_back(static_cast<char>(0x80 | ((codepoint >> 6U) & 0x3FU)));
        out->push_back(static_cast<char>(0x80 | (codepoint & 0x3FU)));
    }
}

bool ExtractJsonString(const std::string& json,
                       const std::string& key,
                       std::string* out) {
    if (!out) {
        return false;
    }
    const std::string token = "\"" + key + "\"";
    std::size_t pos = json.find(token);
    if (pos == std::string::npos) {
        return false;
    }
    pos = json.find(':', pos + token.size());
    if (pos == std::string::npos) {
        return false;
    }
    ++pos;
    while (pos < json.size() &&
           std::isspace(static_cast<unsigned char>(json[pos])) != 0) {
        ++pos;
    }
    if (pos >= json.size() || json[pos] != '"') {
        return false;
    }
    ++pos;

    std::string value;
    while (pos < json.size()) {
        const char ch = json[pos++];
        if (ch == '"') {
            *out = value;
            return true;
        }
        if (ch != '\\') {
            value.push_back(ch);
            continue;
        }
        if (pos >= json.size()) {
            break;
        }
        const char esc = json[pos++];
        switch (esc) {
            case '"':
            case '\\':
            case '/':
                value.push_back(esc);
                break;
            case 'b':
                value.push_back('\b');
                break;
            case 'f':
                value.push_back('\f');
                break;
            case 'n':
                value.push_back('\n');
                break;
            case 'r':
                value.push_back('\r');
                break;
            case 't':
                value.push_back('\t');
                break;
            case 'u': {
                if (pos + 4 > json.size()) {
                    return false;
                }
                unsigned codepoint = 0;
                for (int i = 0; i < 4; ++i) {
                    codepoint <<= 4U;
                    const char hex = json[pos++];
                    if (hex >= '0' && hex <= '9') {
                        codepoint |= static_cast<unsigned>(hex - '0');
                    } else if (hex >= 'a' && hex <= 'f') {
                        codepoint |= static_cast<unsigned>(hex - 'a' + 10);
                    } else if (hex >= 'A' && hex <= 'F') {
                        codepoint |= static_cast<unsigned>(hex - 'A' + 10);
                    } else {
                        return false;
                    }
                }
                AppendUtf8Codepoint(&value, codepoint);
                break;
            }
            default:
                return false;
        }
    }

    return false;
}

bool ExtractJsonBool(const std::string& json,
                     const std::string& key,
                     bool* out) {
    if (!out) {
        return false;
    }
    const std::string token = "\"" + key + "\"";
    std::size_t pos = json.find(token);
    if (pos == std::string::npos) {
        return false;
    }
    pos = json.find(':', pos + token.size());
    if (pos == std::string::npos) {
        return false;
    }
    ++pos;
    while (pos < json.size() &&
           std::isspace(static_cast<unsigned char>(json[pos])) != 0) {
        ++pos;
    }
    if (json.compare(pos, 4, "true") == 0) {
        *out = true;
        return true;
    }
    if (json.compare(pos, 5, "false") == 0) {
        *out = false;
        return true;
    }
    return false;
}

bool Base64Decode(const std::string& input,
                  std::vector<std::uint8_t>* out) {
    if (!out) {
        return false;
    }
    out->clear();
    DWORD length = 0;
    if (!CryptStringToBinaryA(input.c_str(), static_cast<DWORD>(input.size()),
                              CRYPT_STRING_BASE64, nullptr, &length, nullptr,
                              nullptr)) {
        return false;
    }
    out->resize(static_cast<std::size_t>(length));
    return CryptStringToBinaryA(input.c_str(), static_cast<DWORD>(input.size()),
                                CRYPT_STRING_BASE64, out->data(), &length,
                                nullptr, nullptr) != FALSE;
}

std::int64_t GetUnixTimeNow() {
    using namespace std::chrono;
    return duration_cast<seconds>(system_clock::now().time_since_epoch()).count();
}

bool ParseRfc3339ToUnix(const std::string& value,
                        std::int64_t* out) {
    if (!out || value.size() < 20) {
        return false;
    }

    auto readInt = [&](std::size_t offset, std::size_t count, int* result) -> bool {
        if (offset + count > value.size()) {
            return false;
        }
        int parsed = 0;
        for (std::size_t idx = 0; idx < count; ++idx) {
            const char ch = value[offset + idx];
            if (ch < '0' || ch > '9') {
                return false;
            }
            parsed = parsed * 10 + (ch - '0');
        }
        *result = parsed;
        return true;
    };

    int year = 0;
    int month = 0;
    int day = 0;
    int hour = 0;
    int minute = 0;
    int second = 0;

    if (!readInt(0, 4, &year) || value[4] != '-' || !readInt(5, 2, &month) ||
        value[7] != '-' || !readInt(8, 2, &day) ||
        (value[10] != 'T' && value[10] != 't') || !readInt(11, 2, &hour) ||
        value[13] != ':' || !readInt(14, 2, &minute) || value[16] != ':' ||
        !readInt(17, 2, &second)) {
        return false;
    }

    std::size_t pos = 19;
    while (pos < value.size() && value[pos] == '.') {
        ++pos;
        while (pos < value.size() &&
               std::isdigit(static_cast<unsigned char>(value[pos])) != 0) {
            ++pos;
        }
    }

    int offsetSeconds = 0;
    if (pos >= value.size()) {
        return false;
    }

    if (value[pos] == 'Z' || value[pos] == 'z') {
        ++pos;
    } else {
        if (pos + 6 > value.size()) {
            return false;
        }
        const char sign = value[pos];
        int offsetHour = 0;
        int offsetMinute = 0;
        if ((sign != '+' && sign != '-') || !readInt(pos + 1, 2, &offsetHour) ||
            value[pos + 3] != ':' || !readInt(pos + 4, 2, &offsetMinute)) {
            return false;
        }
        offsetSeconds = offsetHour * 3600 + offsetMinute * 60;
        if (sign != '+') {
            offsetSeconds = -offsetSeconds;
        }
        pos += 6;
    }

    if (pos != value.size()) {
        return false;
    }

    std::tm tmValue{};
    tmValue.tm_year = year - 1900;
    tmValue.tm_mon = month - 1;
    tmValue.tm_mday = day;
    tmValue.tm_hour = hour;
    tmValue.tm_min = minute;
    tmValue.tm_sec = second;

    const std::int64_t utc = _mkgmtime64(&tmValue);
    if (utc < 0) {
        return false;
    }

    *out = utc - offsetSeconds;
    return true;
}

bool ValidateTimeWindow(const std::string& serverTime,
                        const std::string& expiresAt) {
    std::int64_t serverEpoch = 0;
    std::int64_t expiresEpoch = 0;
    if (!ParseRfc3339ToUnix(serverTime, &serverEpoch) ||
        !ParseRfc3339ToUnix(expiresAt, &expiresEpoch)) {
        return false;
    }

    const std::int64_t localEpoch = GetUnixTimeNow();
    constexpr std::int64_t kAllowedSkewSeconds = 10 * 60;

    if (localEpoch + kAllowedSkewSeconds < serverEpoch) {
        return false;
    }
    if (localEpoch > expiresEpoch + kAllowedSkewSeconds) {
        return false;
    }
    return true;
}

std::wstring GetCurrentIpText(HWND ipEdit) {
    const int length = GetWindowTextLengthW(ipEdit);
    std::wstring text(static_cast<std::size_t>(length) + 1, L'\0');
    GetWindowTextW(ipEdit, text.data(), length + 1);
    text.resize(static_cast<std::size_t>(length));
    return text;
}

void SetIpText(HWND ipEdit, const std::wstring& text) {
    SetWindowTextW(ipEdit, text.c_str());
}

void ShowMessage(HWND hwnd, const std::wstring& text) {
    MessageBoxW(hwnd, text.c_str(), L"授权", MB_OK | MB_ICONINFORMATION);
}

void ShowError(HWND hwnd, const std::wstring& text) {
    MessageBoxW(hwnd, text.c_str(), L"授权", MB_OK | MB_ICONERROR);
}

std::string BuildActivateBody() {
    std::ostringstream out;
    out << "{\"internal\":false,\"client_time\":\"" << GetUnixTimeNow() << "\"}";
    return out.str();
}

void QueueIpRefresh(HWND hwnd) {
    auto* state = GetState(hwnd);
    if (!state) {
        return;
    }

    const std::wstring baseUrl = CurrentServerBaseUrl(*state);
    const DWORD timeoutMs = state->config.timeoutMs;

    std::thread([hwnd, baseUrl, timeoutMs]() {
        const auto response =
            HttpRequest(L"GET", BuildUrl(baseUrl, L"/api/v1/ip"), std::string(),
                        timeoutMs);
        auto* payload = new std::wstring(L"获取失败");
        if (response.error.empty() && response.statusCode == 200) {
            std::string ip;
            if (ExtractJsonString(response.body, "ip", &ip)) {
                *payload = Utf8ToWide(ip);
            }
        }
        PostMessageW(hwnd, WM_APP_IP_READY, 0, reinterpret_cast<LPARAM>(payload));
    }).detach();
}

void ActivateLicense(HWND hwnd) {
    auto* state = GetState(hwnd);
    if (!state) {
        return;
    }

    const std::wstring baseUrl = CurrentServerBaseUrl(*state);

    const auto response =
        HttpRequest(L"POST", BuildUrl(baseUrl, L"/api/v1/activate"),
                    BuildActivateBody(), state->config.timeoutMs);
    if (!response.error.empty()) {
        ShowError(hwnd, L"连接授权服务器失败:\n" + response.error);
        return;
    }

    bool ok = false;
    std::string messageUtf8;
    ExtractJsonBool(response.body, "ok", &ok);
    ExtractJsonString(response.body, "message", &messageUtf8);
    const std::wstring message = messageUtf8.empty()
                                     ? L"尚未获得授权或已经到期，请联系作者"
                                     : Utf8ToWide(messageUtf8);

    if (response.statusCode != 200 || !ok) {
        ShowError(hwnd, message);
        return;
    }

    std::string blobBase64;
    std::string serverTime;
    std::string expiresAt;
    std::string boundIp;

    ExtractJsonString(response.body, "blob_base64", &blobBase64);
    ExtractJsonString(response.body, "server_time", &serverTime);
    ExtractJsonString(response.body, "expires_at", &expiresAt);
    ExtractJsonString(response.body, "bound_ip", &boundIp);

    if (!serverTime.empty() && !expiresAt.empty() &&
        !ValidateTimeWindow(serverTime, expiresAt)) {
        ShowError(hwnd, L"尚未获得授权或已经到期，请联系作者");
        return;
    }

    if (blobBase64.empty()) {
        ShowError(hwnd, L"授权服务器未返回完整授权内容");
        return;
    }

    std::vector<std::uint8_t> outputBytes;
    if (!Base64Decode(blobBase64, &outputBytes)) {
        ShowError(hwnd, L"服务器返回的授权数据无法解码");
        return;
    }

    const auto outputPath = ResolvePath(*state, state->config.outputPath);
    if (!WriteBinaryFile(outputPath, outputBytes)) {
        ShowError(hwnd, L"写入本机授权失败，请确认程序有权限写入:\n" +
                            outputPath.wstring());
        return;
    }

    if (!boundIp.empty()) {
        SetIpText(state->ipEdit, Utf8ToWide(boundIp));
    }
    ShowMessage(hwnd, L"激活授权成功");
}

void ClearLicense(HWND hwnd) {
    auto* state = GetState(hwnd);
    if (!state) {
        return;
    }

    const auto outputPath = ResolvePath(*state, state->config.outputPath);
    std::error_code error;
    const bool removed = std::filesystem::remove(outputPath, error);
    if (removed) {
        ShowMessage(hwnd, L"本机授权已清除");
        return;
    }
    if (!std::filesystem::exists(outputPath)) {
        ShowMessage(hwnd, L"当前未发现本机授权");
        return;
    }
    ShowError(hwnd, L"清除本机授权失败:\n" + Utf8ToWide(error.message()));
}

HFONT CreateUiFont(int pointSize, int weight) {
    HDC screen = GetDC(nullptr);
    const int height = -MulDiv(pointSize, GetDeviceCaps(screen, LOGPIXELSY), 72);
    ReleaseDC(nullptr, screen);
    return CreateFontW(height, 0, 0, 0, weight, FALSE, FALSE, FALSE,
                       DEFAULT_CHARSET, OUT_OUTLINE_PRECIS, CLIP_DEFAULT_PRECIS,
                       CLEARTYPE_QUALITY, VARIABLE_PITCH, L"Microsoft YaHei UI");
}

LRESULT CALLBACK WindowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
        case WM_NCCREATE: {
            auto* createStruct = reinterpret_cast<CREATESTRUCTW*>(lParam);
            auto* state = reinterpret_cast<AppState*>(createStruct->lpCreateParams);
            SetWindowLongPtrW(hwnd, GWLP_USERDATA,
                              reinterpret_cast<LONG_PTR>(state));
            return TRUE;
        }
        case WM_CREATE: {
            auto* state = GetState(hwnd);
            if (!state) return -1;

            // 现代风格：纯净白主背景与浅色输入框背景
            state->bgBrush = CreateSolidBrush(RGB(255, 255, 255));
            state->editBrush = CreateSolidBrush(RGB(248, 250, 252));
            state->titleFont = CreateUiFont(18, FW_BOLD);
            state->normalFont = CreateUiFont(10, FW_NORMAL);

            // 醒目的主标题
            state->titleLabel = CreateWindowW(
                L"STATIC", L"设备授权验证", WS_CHILD | WS_VISIBLE | SS_CENTER,
                0, 40, kWindowWidth, 32, hwnd, ControlIdToMenu(IDC_LABEL_TITLE),
                nullptr, nullptr);

            // 输入组件调整排版
            state->ipLabel = CreateWindowW(
                L"STATIC", L"当前服务器IP :", WS_CHILD | WS_VISIBLE | SS_RIGHT,
                40, 98, 90, 24, hwnd, ControlIdToMenu(IDC_LABEL_IP),
                nullptr, nullptr);
                
            state->ipEdit = CreateWindowExW(
                WS_EX_CLIENTEDGE, L"EDIT", L"正在获取...",
                WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL | ES_READONLY,
                140, 94, 180, 28, hwnd, ControlIdToMenu(IDC_EDIT_IP),
                nullptr, nullptr);

            // 底部操作按钮居中排布
            state->activateButton = CreateWindowW(
                L"BUTTON", L"激活授权", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                100, 144, 100, 36, hwnd,
                ControlIdToMenu(IDC_BUTTON_ACTIVATE), nullptr, nullptr);
                
            state->clearButton = CreateWindowW(
                L"BUTTON", L"清除授权", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                220, 144, 100, 36, hwnd,
                ControlIdToMenu(IDC_BUTTON_CLEAR), nullptr, nullptr);

            SendMessageW(state->titleLabel, WM_SETFONT, reinterpret_cast<WPARAM>(state->titleFont), TRUE);
            
            const HWND controls[] = {state->ipLabel, state->ipEdit, state->activateButton, state->clearButton};
            for (HWND control : controls) {
                SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(state->normalFont), TRUE);
            }

            QueueIpRefresh(hwnd);
            return 0;
        }
        case WM_COMMAND: {
            switch (LOWORD(wParam)) {
                case IDC_BUTTON_ACTIVATE:
                    if (HIWORD(wParam) == BN_CLICKED) ActivateLicense(hwnd);
                    break;
                case IDC_BUTTON_CLEAR:
                    if (HIWORD(wParam) == BN_CLICKED) ClearLicense(hwnd);
                    break;
            }
            return 0;
        }
        case WM_APP_IP_READY: {
            auto* state = GetState(hwnd);
            auto* payload = reinterpret_cast<std::wstring*>(lParam);
            if (state && payload) SetIpText(state->ipEdit, *payload);
            delete payload;
            return 0;
        }
        case WM_CTLCOLORSTATIC: {
            auto* state = GetState(hwnd);
            if (!state) break;
            HDC hdc = reinterpret_cast<HDC>(wParam);
            const HWND target = reinterpret_cast<HWND>(lParam);
            
            if (target == state->titleLabel) {
                // 醒目的现代蓝色主标题 (Win10/Win11 风格蓝)
                SetTextColor(hdc, RGB(0, 120, 215));
                SetBkMode(hdc, TRANSPARENT);
                return reinterpret_cast<LRESULT>(state->bgBrush);
            } else if (target == state->ipEdit) {
                // 输入框文本与底色
                SetTextColor(hdc, RGB(30, 30, 30));
                SetBkMode(hdc, TRANSPARENT);
                return reinterpret_cast<LRESULT>(state->editBrush);
            } else {
                // 普通标签色
                SetTextColor(hdc, RGB(50, 50, 50));
                SetBkMode(hdc, TRANSPARENT);
                return reinterpret_cast<LRESULT>(state->bgBrush);
            }
        }
        case WM_ERASEBKGND: {
            auto* state = GetState(hwnd);
            if (!state || !state->bgBrush) break;
            RECT rect;
            GetClientRect(hwnd, &rect);
            FillRect(reinterpret_cast<HDC>(wParam), &rect, state->bgBrush);
            return 1;
        }
        case WM_DESTROY: {
            auto* state = GetState(hwnd);
            if (state) {
                if (state->titleFont) DeleteObject(state->titleFont);
                if (state->normalFont) DeleteObject(state->normalFont);
                if (state->bgBrush) DeleteObject(state->bgBrush);
                if (state->editBrush) DeleteObject(state->editBrush);
            }
            PostQuitMessage(0);
            return 0;
        }
        default:
            break;
    }

    return DefWindowProcW(hwnd, message, wParam, lParam);
}

}  // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int showCommand) {
    INITCOMMONCONTROLSEX commonControls{};
    commonControls.dwSize = sizeof(commonControls);
    commonControls.dwICC = ICC_STANDARD_CLASSES;
    InitCommonControlsEx(&commonControls);

    AppState state;
    state.exeDir = GetExecutableDir();
    state.config = LoadConfig(state.exeDir);

    const wchar_t className[] = L"AuthClientWindowClass";
    WNDCLASSEXW windowClass{};
    windowClass.cbSize = sizeof(windowClass);
    windowClass.lpfnWndProc = WindowProc;
    windowClass.hInstance = instance;
    windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    // 注册时使用纯白背景
    windowClass.hbrBackground = CreateSolidBrush(RGB(255, 255, 255));
    windowClass.lpszClassName = className;

    RegisterClassExW(&windowClass);

    // 获取屏幕指标并自动计算居中坐标
    int screenW = GetSystemMetrics(SM_CXSCREEN);
    int screenH = GetSystemMetrics(SM_CYSCREEN);
    int windowX = (screenW - kWindowWidth) / 2;
    int windowY = (screenH - kWindowHeight) / 2;

    HWND hwnd = CreateWindowExW(
        0, className, L"授权", WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU |
                                WS_MINIMIZEBOX | WS_CLIPCHILDREN,
        windowX, windowY, kWindowWidth, kWindowHeight, nullptr,
        nullptr, instance, &state);
        
    if (!hwnd) {
        MessageBoxW(nullptr, L"创建窗口失败", L"授权", MB_OK | MB_ICONERROR);
        return 1;
    }

    ShowWindow(hwnd, showCommand);
    UpdateWindow(hwnd);

    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }

    return static_cast<int>(message.wParam);
}
