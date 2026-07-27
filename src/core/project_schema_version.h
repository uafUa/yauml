#pragma once

namespace uuml {

// The persisted project schema has an independent version from the application
// binary. Increment this only when a corresponding migration step is added.
inline constexpr int kCurrentProjectSchemaVersion = 2;

} // namespace uuml
