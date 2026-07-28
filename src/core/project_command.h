#pragma once

#include <QUndoCommand>

namespace yauml {

class ProjectController;
struct ProjectData;

// Polymorphic base for every undoable product action. Concrete commands own
// the minimal state needed to execute and revert their operation. The final Qt
// entry points guarantee that all commands pass through the controller's one
// notification boundary.
class ProjectCommand : public QUndoCommand {
public:
  void undo() final;
  void redo() final;

protected:
  ProjectCommand(ProjectController *controller, const QString &description);

private:
  friend class ProjectController;

  virtual void execute(ProjectData &project) = 0;
  virtual void revert(ProjectData &project) = 0;

  ProjectController *m_controller;
};

} // namespace yauml
