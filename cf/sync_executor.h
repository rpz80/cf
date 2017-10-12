#pragma once

#include "common.h"

namespace cf {
class sync_executor {
public:
  void post(detail::task_type f) {
    f();
  }
};
}
