#include "auth_codec.h"

#define NOMINMAX
#include <windows.h>
#include <bcrypt.h>

#include <algorithm>
#include <array>
#include <iomanip>
#include <sstream>
#include <string>
#include <vector>

#pragma comment(lib, "bcrypt.lib")

namespace auth {
namespace {

constexpr std::uint32_t kFixedMagic = 89742336U;

struct Rc4State {
    std::array<std::uint8_t, 256> s{};
    std::uint8_t i = 0;
    std::uint8_t j = 0;
};

std::wstring Utf8ToWide(const std::string& text) {
    if (text.empty()) {
        return std::wstring();
    }

    int length = MultiByteToWideChar(CP_UTF8, 0, text.data(),
                                     static_cast<int>(text.size()), nullptr, 0);
    if (length <= 0) {
        return std::wstring();
    }

    std::wstring out(length, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()),
                        out.data(), length);
    return out;
}

bool WideToCodePage(const std::wstring& text, UINT codePage,
                    std::vector<std::uint8_t>* out) {
    if (!out) {
        return false;
    }
    out->clear();
    if (text.empty()) {
        return true;
    }

    int length =
        WideCharToMultiByte(codePage, 0, text.data(), static_cast<int>(text.size()),
                            nullptr, 0, nullptr, nullptr);
    if (length <= 0) {
        return false;
    }

    out->resize(static_cast<std::size_t>(length));
    return WideCharToMultiByte(codePage, 0, text.data(),
                               static_cast<int>(text.size()),
                               reinterpret_cast<LPSTR>(out->data()), length,
                               nullptr, nullptr) > 0;
}

std::string Md5HexLower(const std::uint8_t* data, std::size_t size) {
    BCRYPT_ALG_HANDLE algorithm = nullptr;
    BCRYPT_HASH_HANDLE hash = nullptr;
    DWORD objectLength = 0;
    DWORD hashLength = 0;
    DWORD bytesWritten = 0;
    std::vector<UCHAR> hashObject;
    std::vector<UCHAR> digest;

    if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_MD5_ALGORITHM, nullptr, 0) != 0) {
        return std::string();
    }

    if (BCryptGetProperty(algorithm, BCRYPT_OBJECT_LENGTH,
                          reinterpret_cast<PUCHAR>(&objectLength),
                          sizeof(objectLength), &bytesWritten, 0) != 0) {
        BCryptCloseAlgorithmProvider(algorithm, 0);
        return std::string();
    }

    if (BCryptGetProperty(algorithm, BCRYPT_HASH_LENGTH,
                          reinterpret_cast<PUCHAR>(&hashLength),
                          sizeof(hashLength), &bytesWritten, 0) != 0) {
        BCryptCloseAlgorithmProvider(algorithm, 0);
        return std::string();
    }

    hashObject.resize(objectLength);
    digest.resize(hashLength);

    if (BCryptCreateHash(algorithm, &hash, hashObject.data(), objectLength, nullptr, 0,
                         0) != 0) {
        BCryptCloseAlgorithmProvider(algorithm, 0);
        return std::string();
    }

    if (size > 0 &&
        BCryptHashData(hash, const_cast<PUCHAR>(reinterpret_cast<const UCHAR*>(data)),
                       static_cast<ULONG>(size), 0) != 0) {
        BCryptDestroyHash(hash);
        BCryptCloseAlgorithmProvider(algorithm, 0);
        return std::string();
    }

    if (BCryptFinishHash(hash, digest.data(), hashLength, 0) != 0) {
        BCryptDestroyHash(hash);
        BCryptCloseAlgorithmProvider(algorithm, 0);
        return std::string();
    }

    BCryptDestroyHash(hash);
    BCryptCloseAlgorithmProvider(algorithm, 0);

    std::ostringstream oss;
    for (const auto value : digest) {
        oss << std::hex << std::setw(2) << std::setfill('0')
            << static_cast<int>(value);
    }
    return oss.str();
}

std::string Md5HexLower(const std::vector<std::uint8_t>& data) {
    return Md5HexLower(data.data(), data.size());
}

Rc4State Rc4Init(const std::vector<std::uint8_t>& key) {
    Rc4State state;
    for (int idx = 0; idx < 256; ++idx) {
        state.s[static_cast<std::size_t>(idx)] = static_cast<std::uint8_t>(idx);
    }

    int j = 0;
    int keyIndex = 0;
    for (int i = 0; i < 256; ++i) {
        j = (static_cast<int>(state.s[static_cast<std::size_t>(i)]) +
             static_cast<int>(key[static_cast<std::size_t>(keyIndex)]) + j) &
            0xFF;
        std::swap(state.s[static_cast<std::size_t>(i)],
                  state.s[static_cast<std::size_t>(j)]);
        keyIndex = (keyIndex + 1) % static_cast<int>(key.size());
    }

    return state;
}

void Rc4Skip(Rc4State* state, int count) {
    if (!state) {
        return;
    }

    for (int idx = 0; idx < count; ++idx) {
        state->i = static_cast<std::uint8_t>(state->i + 1);
        state->j = static_cast<std::uint8_t>(
            state->j + state->s[static_cast<std::size_t>(state->i)]);
        std::swap(state->s[static_cast<std::size_t>(state->i)],
                  state->s[static_cast<std::size_t>(state->j)]);
    }
}

std::vector<std::uint8_t> Rc4Xor(const std::vector<std::uint8_t>& data,
                                 Rc4State* state) {
    std::vector<std::uint8_t> out = data;
    if (!state) {
        return out;
    }

    for (std::size_t idx = 0; idx < out.size(); ++idx) {
        state->i = static_cast<std::uint8_t>(state->i + 1);
        state->j = static_cast<std::uint8_t>(
            state->j + state->s[static_cast<std::size_t>(state->i)]);

        const std::uint8_t tmp = state->s[static_cast<std::size_t>(state->j)];
        state->s[static_cast<std::size_t>(state->j)] =
            state->s[static_cast<std::size_t>(state->i)];
        state->s[static_cast<std::size_t>(state->i)] = tmp;

        const auto keyIndex = static_cast<std::uint8_t>(
            state->s[static_cast<std::size_t>(state->j)] + tmp);
        out[idx] ^= state->s[static_cast<std::size_t>(keyIndex)];
    }

    return out;
}

std::vector<std::uint8_t> Key32(const std::vector<std::uint8_t>& raw) {
    const std::string md5 = Md5HexLower(raw);
    std::vector<std::uint8_t> out(md5.begin(), md5.end());

    for (int i = 0; i < 32; i += 2) {
        std::swap(out[static_cast<std::size_t>(i)],
                  out[static_cast<std::size_t>(i + 1)]);
    }

    for (int i = 0; i < 16; ++i) {
        std::swap(out[static_cast<std::size_t>(i)],
                  out[static_cast<std::size_t>(31 - i)]);
    }

    return out;
}

std::uint32_t ReadLe32(const std::vector<std::uint8_t>& data, std::size_t offset) {
    return static_cast<std::uint32_t>(data[offset]) |
           (static_cast<std::uint32_t>(data[offset + 1]) << 8U) |
           (static_cast<std::uint32_t>(data[offset + 2]) << 16U) |
           (static_cast<std::uint32_t>(data[offset + 3]) << 24U);
}

void AppendLe32(std::vector<std::uint8_t>* out, std::uint32_t value) {
    if (!out) {
        return;
    }
    out->push_back(static_cast<std::uint8_t>(value & 0xFFU));
    out->push_back(static_cast<std::uint8_t>((value >> 8U) & 0xFFU));
    out->push_back(static_cast<std::uint8_t>((value >> 16U) & 0xFFU));
    out->push_back(static_cast<std::uint8_t>((value >> 24U) & 0xFFU));
}

std::vector<std::uint8_t> Crypt(const std::vector<std::uint8_t>& data,
                                const std::vector<std::uint8_t>& key,
                                int initPos) {
    std::vector<std::uint8_t> out = data;
    int a2 = 0;
    int a4 = static_cast<int>(out.size());
    int ptr = 0;

    if (initPos > 0) {
        ptr = initPos;
        a4 -= initPos;
        a2 = initPos;
    }

    Rc4State state = Rc4Init(key);
    int blockIndex = a2 / 4096;
    Rc4Skip(&state, 4 * blockIndex);

    const auto blockRandoms =
        Rc4Xor(std::vector<std::uint8_t>(4 * (a4 / 4096) + 8, 0), &state);
    std::size_t randomPos = 0;
    const int offset = a2 % 4096;
    const auto key32 = Key32(key);

    auto makeBlockState = [&](std::uint32_t rand, std::uint32_t idx) {
        std::vector<std::uint8_t> blockKey;
        blockKey.reserve(4 + key32.size() + 4);
        AppendLe32(&blockKey, rand);
        blockKey.insert(blockKey.end(), key32.begin(), key32.end());
        AppendLe32(&blockKey, rand ^ idx);
        return Rc4Init(blockKey);
    };

    if (offset > 0) {
        const std::uint32_t rand = ReadLe32(blockRandoms, randomPos);
        randomPos += 4;

        Rc4State blockState = makeBlockState(rand, static_cast<std::uint32_t>(blockIndex));
        ++blockIndex;

        Rc4Skip(&blockState, offset + 36);
        const int take = std::min(4096 - offset, a4);
        const std::vector<std::uint8_t> chunk(
            out.begin() + ptr, out.begin() + ptr + take);
        const auto encoded = Rc4Xor(chunk, &blockState);
        std::copy(encoded.begin(), encoded.end(), out.begin() + ptr);
        ptr += take;
        a4 -= take;
    }

    while (a4 > 0) {
        const std::uint32_t rand = ReadLe32(blockRandoms, randomPos);
        randomPos += 4;

        Rc4State blockState = makeBlockState(rand, static_cast<std::uint32_t>(blockIndex));
        ++blockIndex;

        Rc4Skip(&blockState, 36);
        const int take = std::min(4096, a4);
        const std::vector<std::uint8_t> chunk(
            out.begin() + ptr, out.begin() + ptr + take);
        const auto encoded = Rc4Xor(chunk, &blockState);
        std::copy(encoded.begin(), encoded.end(), out.begin() + ptr);
        ptr += take;
        a4 -= take;
    }

    return out;
}

std::vector<std::uint8_t> Decrypt(const std::vector<std::uint8_t>& data,
                                  const std::vector<std::uint8_t>& key) {
    return Crypt(data, key, 4);
}

std::vector<std::uint8_t> Encrypt(const std::vector<std::uint8_t>& data,
                                  const std::vector<std::uint8_t>& key) {
    return Crypt(data, key, 4);
}

}  // namespace

std::vector<std::uint8_t> BuildEncryptedText(
    const std::string& plainTextUtf8,
    const std::string& keyAscii,
    std::string* errorMessage) {
    if (errorMessage) {
        errorMessage->clear();
    }

    const std::wstring plainTextWide = Utf8ToWide(plainTextUtf8);
    std::vector<std::uint8_t> blob;
    if (!WideToCodePage(plainTextWide, 936, &blob)) {
        if (errorMessage) {
            *errorMessage = "文本转 GBK 失败";
        }
        return {};
    }

    const std::vector<std::uint8_t> key(keyAscii.begin(), keyAscii.end());
    const std::string digest64 = Md5HexLower(key) + Md5HexLower(blob);

    std::vector<std::uint8_t> plainOut;
    plainOut.reserve(4 + digest64.size() + 4 + blob.size());
    AppendLe32(&plainOut, kFixedMagic);
    plainOut.insert(plainOut.end(), digest64.begin(), digest64.end());
    AppendLe32(&plainOut, static_cast<std::uint32_t>(blob.size()));
    plainOut.insert(plainOut.end(), blob.begin(), blob.end());

    return Encrypt(plainOut, key);
}

std::vector<std::uint8_t> BuildEncryptedFromTemplate(
    const std::vector<std::uint8_t>& templateBytes,
    const std::string& fixedTextUtf8,
    const std::string& keyAscii,
    std::string* errorMessage) {
    if (errorMessage) {
        errorMessage->clear();
    }

    const std::vector<std::uint8_t> key(keyAscii.begin(), keyAscii.end());
    const auto plain = Decrypt(templateBytes, key);
    if (plain.size() < 72) {
        if (errorMessage) {
            *errorMessage = "template.hfy 解密后长度不足";
        }
        return {};
    }

    const std::uint32_t oldLength = ReadLe32(plain, 68);
    const std::size_t tailOffset = 72ULL + static_cast<std::size_t>(oldLength);
    if (tailOffset > plain.size()) {
        if (errorMessage) {
            *errorMessage = "template.hfy 结构异常";
        }
        return {};
    }

    const std::wstring fixedTextWide = Utf8ToWide(fixedTextUtf8);
    std::vector<std::uint8_t> newBlob;
    if (!WideToCodePage(fixedTextWide, 936, &newBlob)) {
        if (errorMessage) {
            *errorMessage = "FIXED_TEXT 转 GBK 失败";
        }
        return {};
    }

    const std::string digest64 = Md5HexLower(key) + Md5HexLower(newBlob);
    std::vector<std::uint8_t> plainOut;
    plainOut.reserve(4 + digest64.size() + 4 + newBlob.size() +
                     (plain.size() - tailOffset));

    plainOut.insert(plainOut.end(), plain.begin(), plain.begin() + 4);
    plainOut.insert(plainOut.end(), digest64.begin(), digest64.end());
    AppendLe32(&plainOut, static_cast<std::uint32_t>(newBlob.size()));
    plainOut.insert(plainOut.end(), newBlob.begin(), newBlob.end());
    plainOut.insert(plainOut.end(), plain.begin() + static_cast<std::ptrdiff_t>(tailOffset),
                    plain.end());

    return Encrypt(plainOut, key);
}

}  // namespace auth
