#pragma once

#include <windows.h>
#include <array>

namespace menu {

enum class State { Default, Classic, Incomplete, Check };
enum class Recipe { Unknown, Legacy, Dual };
using Paths = std::array<const wchar_t*, 2>;

inline constexpr Paths userPaths{
    LR"(Software\Classes\CLSID\{86ca1aa0-34aa-4e8b-a509-50c905bae2a2})",
    LR"(Software\Classes\CLSID\{51668040-5e31-4023-bbe3-149163bd80f1})",
};

struct ChangeResult {
    LSTATUS error = ERROR_SUCCESS;
    bool changed = false;
};

Recipe DetectRecipe();
State Inspect(HKEY root, const Paths& paths, Recipe recipe);
ChangeResult Change(HKEY root, const Paths& paths, Recipe recipe, bool classic);

}  // namespace menu
