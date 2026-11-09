#pragma once
#include <cstdlib>
#define ASSERT(condition, ...) do { if (!(condition)) std::abort(); } while (false)
#define ABORT(...) std::abort()
