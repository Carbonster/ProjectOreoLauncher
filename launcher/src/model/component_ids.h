// component_ids.h - the names of what the launcher checks, installs and updates. Catalog
// entries, components and the page all use these, so each name is written once.
#pragma once

namespace oreo {

inline constexpr char kIdLoader[] = "loader";
inline constexpr char kIdPymhf[] = "pymhf";
inline constexpr char kIdNmspy[] = "nmspy";
inline constexpr char kIdRuntime[] = "runtime";
inline constexpr char kIdGame[] = "game";
inline constexpr char kIdLauncher[] = "launcher";

// The same three as packages, by the names the private Python knows them.
inline constexpr char kPkgPymhf[] = "pymhf";
inline constexpr char kPkgNmspy[] = "nmspy";
inline constexpr char kPkgRuntime[] = "project-oreo-runtime";

}  // namespace oreo
