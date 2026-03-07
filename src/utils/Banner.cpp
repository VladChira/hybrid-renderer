#include "utils/Banner.h"

#include <fstream>
#include <sstream>

namespace hybrid::utils {

std::string LoadBannerText() {
  std::ifstream file(std::string(HYBRID_PROJECT_ROOT) + "/banner.txt", std::ios::in | std::ios::binary);
  if (!file) {
    return {};
  }

  std::ostringstream contents;
  contents << file.rdbuf();
  return contents.str();
}

} // namespace hybrid::utils
