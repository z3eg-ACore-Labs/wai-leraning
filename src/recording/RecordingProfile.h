#pragma once

#include <string_view>

namespace GameplayObservation
{
enum class CaptureProfile
{
    Navigation,
    Full
};

inline CaptureProfile ParseCaptureProfile(std::string_view value)
{
    if (value == "full")
    {
        return CaptureProfile::Full;
    }
    return CaptureProfile::Navigation;
}

inline char const* CaptureProfileName(CaptureProfile profile)
{
    return profile == CaptureProfile::Full ? "full" : "navigation";
}

inline bool CaptureDetailedCombat(CaptureProfile profile)
{
    return profile == CaptureProfile::Full;
}

inline bool CaptureInput(CaptureProfile profile, std::string_view category)
{
    if (profile == CaptureProfile::Full)
    {
        return true;
    }
    return category == "movement";
}
} // namespace GameplayObservation
