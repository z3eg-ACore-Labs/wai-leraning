#pragma once

#include <cmath>
#include <cstdint>
#include <iomanip>
#include <locale>
#include <sstream>
#include <string>

namespace GameplayObservation
{

inline std::string NavigationSnapshot(std::uint32_t map, std::uint32_t instance, std::uint64_t actor, float x, float y,
                                      float z, float facing, bool alive, bool combat, bool casting)
{
    std::ostringstream ss;
    ss.imbue(std::locale::classic());
    ss << std::setprecision(7);

    // Handle non-finite values
    float safeX = std::isfinite(x) ? x : 0.0f;
    float safeY = std::isfinite(y) ? y : 0.0f;
    float safeZ = std::isfinite(z) ? z : 0.0f;
    float safeFacing = std::isfinite(facing) ? facing : 0.0f;

    ss << "{";
    ss << "\"map\":" << map << ",";
    ss << "\"instance\":" << instance << ",";
    ss << "\"self\":{";
    ss << "\"actor\":" << actor << ",";
    ss << "\"position\":[";
    ss << safeX << ',' << safeY << ',' << safeZ << ',' << safeFacing << "],";
    ss << "\"alive\":" << (alive ? "true" : "false") << ",";
    ss << "\"combat\":" << (combat ? "true" : "false") << ",";
    ss << "\"casting\":" << (casting ? "true" : "false");
    ss << "},";
    ss << "\"party\":[],";
    ss << "\"selected\":null";
    ss << "}";

    return ss.str();
}

} // namespace GameplayObservation
