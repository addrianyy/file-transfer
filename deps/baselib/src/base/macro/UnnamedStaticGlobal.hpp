#pragma once
#include "AnonymousName.hpp"

#define UNNAMED_STATIC_GLOBAL(type) static type ANONYMOUS_NAME(_base_unnamed_global)
