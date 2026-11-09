/* Optional generic movement prerequisite; GNU GPL v2 or later. */
#ifndef AC_LABS_WAI_SPLINE_ADAPTER_H
#define AC_LABS_WAI_SPLINE_ADAPTER_H
#if __has_include("SplineValidation.h")
#include "SplineValidation.h"
#endif
#if defined(AC_SPLINE_VALIDATION_API) && AC_SPLINE_VALIDATION_API == 1
#define WAI_SPLINE_VALIDATION_AVAILABLE 1
#else
#define WAI_SPLINE_VALIDATION_AVAILABLE 0
#endif
#endif
