#pragma once

#include <QString>

#include <functional>

namespace yauml::test_support {

// Persistence writes have different recovery semantics depending on their
// purpose. Tests use these values to fail the real atomic-write path at precise
// boundaries without relying on platform-specific file locking or permissions.
enum class ProjectWritePurpose {
  RecoveryBackup,
  RecoveryMarker,
  ProjectFile,
  RecoveryRestore
};

enum class ProjectWriteStage { Open, Write, Commit };

struct ProjectWriteBoundary {
  ProjectWritePurpose purpose;
  ProjectWriteStage stage;
  QString path;
};

using ProjectWriteFaultInjector =
    std::function<bool(const ProjectWriteBoundary &)>;

// Installs a thread-local fault injector for one lexical scope. Production code
// never creates this object; keeping the hook thread-local prevents one test
// from affecting unrelated persistence work running concurrently.
class ScopedProjectWriteFaultInjector final {
public:
  explicit ScopedProjectWriteFaultInjector(ProjectWriteFaultInjector injector);
  ~ScopedProjectWriteFaultInjector();

  ScopedProjectWriteFaultInjector(const ScopedProjectWriteFaultInjector &) =
      delete;
  ScopedProjectWriteFaultInjector &
  operator=(const ScopedProjectWriteFaultInjector &) = delete;

private:
  ProjectWriteFaultInjector m_previous;
};

namespace detail {

bool shouldInjectProjectWriteFault(const ProjectWriteBoundary &boundary);

} // namespace detail

} // namespace yauml::test_support
