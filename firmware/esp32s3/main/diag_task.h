// Low-priority task that prints sensor and statistics lines. All console output lives here: the real-time tasks
// never log in steady state.
#pragma once
#include "engine_task.h"

namespace epdj {
namespace app {

void startDiagTask(const Runtime* rt);

}  // namespace app
}  // namespace epdj
