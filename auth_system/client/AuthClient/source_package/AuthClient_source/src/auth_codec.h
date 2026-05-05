#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace auth {

std::vector<std::uint8_t> BuildEncryptedText(
    const std::string& plainTextUtf8,
    const std::string& keyAscii,
    std::string* errorMessage);

std::vector<std::uint8_t> BuildEncryptedFromTemplate(
    const std::vector<std::uint8_t>& templateBytes,
    const std::string& fixedTextUtf8,
    const std::string& keyAscii,
    std::string* errorMessage);

}  // namespace auth
