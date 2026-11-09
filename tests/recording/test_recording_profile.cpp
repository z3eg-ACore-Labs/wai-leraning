#include "../../src/recording/RecordingProfile.h"
#include <cassert>

using namespace GameplayObservation;

int main()
{
    // Test ParseCaptureProfile
    assert(ParseCaptureProfile("full") == CaptureProfile::Full);
    assert(ParseCaptureProfile("navigation") == CaptureProfile::Navigation);
    assert(ParseCaptureProfile("unknown") == CaptureProfile::Navigation);
    assert(ParseCaptureProfile("") == CaptureProfile::Navigation);

    // Test CaptureProfileName
    assert(std::string_view(CaptureProfileName(CaptureProfile::Full)) == "full");
    assert(std::string_view(CaptureProfileName(CaptureProfile::Navigation)) == "navigation");

    // Test CaptureDetailedCombat
    assert(CaptureDetailedCombat(CaptureProfile::Full) == true);
    assert(CaptureDetailedCombat(CaptureProfile::Navigation) == false);

    // Test CaptureInput
    assert(CaptureInput(CaptureProfile::Full, "movement") == true);
    assert(CaptureInput(CaptureProfile::Full, "ability") == true);
    assert(CaptureInput(CaptureProfile::Full, "targeting") == true);
    assert(CaptureInput(CaptureProfile::Full, "interaction") == true);
    assert(CaptureInput(CaptureProfile::Full, "") == true);

    assert(CaptureInput(CaptureProfile::Navigation, "movement") == true);
    assert(CaptureInput(CaptureProfile::Navigation, "ability") == false);
    assert(CaptureInput(CaptureProfile::Navigation, "targeting") == false);
    assert(CaptureInput(CaptureProfile::Navigation, "interaction") == false);
    assert(CaptureInput(CaptureProfile::Navigation, "") == false);

    return 0;
}
