#include "core/project_serializer_test_support.h"

#include <utility>

namespace yauml::test_support {
namespace {

thread_local ProjectWriteFaultInjector currentInjector;

} // namespace

ScopedProjectWriteFaultInjector::ScopedProjectWriteFaultInjector(
    ProjectWriteFaultInjector injector)
    : m_previous(std::move(currentInjector)) {
  currentInjector = std::move(injector);
}

ScopedProjectWriteFaultInjector::~ScopedProjectWriteFaultInjector() {
  currentInjector = std::move(m_previous);
}

namespace detail {

bool shouldInjectProjectWriteFault(const ProjectWriteBoundary &boundary) {
  return currentInjector && currentInjector(boundary);
}

} // namespace detail
} // namespace yauml::test_support
