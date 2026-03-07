#pragma once

#include <string>
#include <string_view>

namespace hybrid::utils {

std::string ExtractExtension(std::string_view path);

} // namespace hybrid::utils
