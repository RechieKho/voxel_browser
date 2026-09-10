// Single translation unit that instantiates raygui (header-only; its
// implementation must be compiled exactly once for the whole client).
// Built as its own target so project warning flags do not apply to it.
#define RAYGUI_IMPLEMENTATION
#include "raygui.h"
