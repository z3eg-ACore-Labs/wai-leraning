// Test the actual optional core callback registry using real spline argument types.
#include "SplineValidation.h"
#include "MoveSplineInitArgs.h"
#include <cassert>
#include <iostream>

unsigned calls = 0;
bool Allow(Unit*, Movement::MoveSplineInitArgs const&) { ++calls; return true; }
bool Deny(Unit*, Movement::MoveSplineInitArgs const&) { ++calls; return false; }

int main()
{
    using namespace Movement;
    MoveSplineInitArgs args;
    assert(ValidateSplineLaunch(nullptr, args));
    assert(!RegisterSplineValidator(nullptr));
    assert(RegisterSplineValidator(Allow));
    assert(RegisterSplineValidator(Allow));
    assert(!RegisterSplineValidator(Deny));
    assert(ValidateSplineLaunch(nullptr, args));
    assert(calls == 1);
    UnregisterSplineValidator(Deny);
    assert(ValidateSplineLaunch(nullptr, args));
    assert(calls == 2);
    UnregisterSplineValidator(Allow);
    assert(RegisterSplineValidator(Deny));
    assert(!ValidateSplineLaunch(nullptr, args));
    assert(calls == 3);
    UnregisterSplineValidator(Deny);
    assert(ValidateSplineLaunch(nullptr, args));
    assert(calls == 3);
    std::cout << "Core spline callback registry passed\n";
}
