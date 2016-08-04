#pragma once 

#include <cf/common.h>

namespace cf {
class sync_executor {
public:
  void post(const detail::task_type& f);
};
}
