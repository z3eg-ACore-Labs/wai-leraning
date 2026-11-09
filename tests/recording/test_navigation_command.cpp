#include "../../src/recording/NavigationCommand.h"
#include <cassert>

int main()
{
    using GameplayNavigation::ValidCommand;
    assert(ValidCommand(9, 9, 3, 3, 2, 100, 100));
    assert(ValidCommand(9, 9, 3, 3, 2, 100, 1100));
    assert(!ValidCommand(9, 9, 3, 3, 2, 100, 1101));
    assert(!ValidCommand(9, 9, 3, 3, 2, 100, 99));
    assert(!ValidCommand(8, 9, 3, 3, 2, 100, 200));
    assert(!ValidCommand(0, 0, 3, 3, 2, 100, 200));
    assert(!ValidCommand(9, 9, 2, 3, 1, 100, 200));
    assert(!ValidCommand(9, 9, 4, 3, 2, 100, 200));
    assert(!ValidCommand(9, 9, 3, 3, 3, 100, 200));
    assert(!ValidCommand(9, 9, 3, 3, 4, 100, 200));
}
