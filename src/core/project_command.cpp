#include "core/project_command.h"

#include "core/project_controller.h"

namespace uuml {

ProjectCommand::ProjectCommand(ProjectController *controller,
                               const QString &description)
    : QUndoCommand(description), m_controller(controller) {
  Q_ASSERT(m_controller);
}

void ProjectCommand::undo() { m_controller->applyCommand(*this, false); }

void ProjectCommand::redo() { m_controller->applyCommand(*this, true); }

} // namespace uuml
