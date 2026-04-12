#pragma once

#include <string>
#include <variant>

namespace ally {

struct BoardState {};
struct NewTaskState {};
struct TaskDetailState {
  std::string task_id;
};
struct StageViewState {
  std::string task_id;
  std::string thread_id;
  std::string stage;
};
struct NewThreadState {
  std::string task_id;
};
struct WorkflowsState {};
struct NewWorkflowState {};
struct EditWorkflowState {
  std::string workflow_id;
};

using NavState = std::variant<BoardState, NewTaskState, TaskDetailState, StageViewState, NewThreadState, WorkflowsState,
                              NewWorkflowState, EditWorkflowState>;

}  // namespace ally
