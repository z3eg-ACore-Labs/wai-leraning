#include "../../src/recording/NavigationSnapshot.h"
#include <cassert>
#include <iostream>
#include <limits>

int main()
{
    // Test with valid values
    auto result = GameplayObservation::NavigationSnapshot(123, 456, 7890123456789, 10.1234567f, 20.1234567f,
                                                          30.1234567f, 45.6789012f, true, true, false);
    assert(result.find("combat") != std::string::npos && result.find("casting") != std::string::npos &&
           result.find("spell") == std::string::npos && result.find("health") == std::string::npos);

    // Test with quiet NaN
    float nanX = std::numeric_limits<float>::quiet_NaN();
    result = GameplayObservation::NavigationSnapshot(123, 456, 7890123456789, nanX, 20.1234567f, 30.1234567f,
                                                     45.6789012f, true, true, false);
    assert(result.find("combat") != std::string::npos && result.find("casting") != std::string::npos &&
           result.find("spell") == std::string::npos && result.find("health") == std::string::npos);

    std::cout << result << std::endl;
    return 0;
}
