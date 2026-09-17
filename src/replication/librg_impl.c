// Sole translation unit for librg's implementation (docs/replication.md).
// Built as its own target (see CMakeLists.txt) so the project's -Werror
// warning flags never touch this vendored C99 code, same pattern as
// vb_raygui_impl in src/render/CMakeLists.txt.

#define LIBRG_IMPL
#include <librg.h>
