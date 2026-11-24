#pragma once
#include "common.hpp"
namespace updates {
#ifdef TUBE360_UPDATE_TEST_CONFIG
inline constexpr const char* publicKeyHex = TUBE360_UPDATE_TEST_KEY;
#else
inline constexpr const char* publicKeyHex = "4fc383be545414b59cc725739834b97257f641cf440b83314022c7f904dca95b";
#endif
}
