#include <mutex>
#include <chrono>
#include <condition_variable>
#include <memory>
#include <stdexcept>

namespace cf {

enum class future_status {
  ready,
  timeout,
  deferred
};

struct future_error : public std::logic_error {
  future_error(const std::string& s) : error_string_(s) {}
  virtual const char* what() const override {
    return error_string_.data();
  }
  std::string error_string_;
};

namespace detail {

class shared_state_base {
protected:
  ~shared_state_base() {}

  void wait() const {
    std::unique_lock<std::mutex> lock(lock_);
    cond_.wait(lock);
  }

  template<typename Rep, typename Period>
  void wait_for(const std::chrono::duration<Rep, Period>& timeout) const {
    std::unique_lock<std::mutex> lock(lock_);
    cond_.wait(lock, timeout);
  }

  void set_ready() {
    cond_.notify_all();
  }
protected:
  std::mutex lock_;
  std::condition_variable cond_;
};

template<typename T>
class shared_state {
  using value_type = T;
protected:
  template<typename U>
  void set_value(U&& value) {
    value_ = std::forward<U>(value);
  }
protected:
  T value_;
};

template<typename T>
using shared_state_ptr = std::shared_ptr<shared_state<T>>;

template<typename T>
class future_base {
public:
  bool valid() const {
    return state_ == nullptr;
  }
  void wait() const {
    if (state_)
      state_.wait();
    else
      throw errc::no_state;
  }
  template<typename Rep, typename Period>
  void wait_for(const std::chrono::duration<Rep, Period>& timeout) {
    if (state_)
      state_.wait_for(timeout);
    else
      throw errc::no_state;
  }
protected:
  shared_state_ptr state_;
};

} // namespace detail

template<typename T>
class future : public detail::future_base {
public:
  T get() {
    if (!valid())
      throw errc::no_state;
  }
};
} // namespace cf