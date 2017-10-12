#pragma once

#include <functional>

namespace cf {
namespace detail {

template<typename F>
class movable_func;

template<typename R, typename... Args>
class movable_func<R(Args...)> {

  struct base_holder {
    virtual R operator() (Args... args) = 0;
    virtual ~base_holder() {}
  };

  template<typename F>
  struct holder : base_holder {
    holder(F f) : f_(std::move(f)) {}
    virtual R operator() (Args... args) override {
      return f_(args...);
    }

    F f_;
  };

public:
  template<typename F>
  movable_func(F f) : held_(new holder<F>(std::move(f))) {}
  movable_func() : held_(nullptr) {}
  movable_func(movable_func<R(Args...)>&&) = default;
  movable_func& operator = (std::nullptr_t) {
    held_.reset();
    return *this;
  }
  movable_func& operator = (movable_func<R(Args...)>&&) = default;
  movable_func(const movable_func<R(Args...)>&) = delete;
  movable_func& operator = (const movable_func<R(Args...)>&) = delete;
  bool empty() const { return !held_; }

  R operator() (Args... args) const {
    return held_->operator()(args...);
  }

  explicit operator bool() {
    return (bool) held_;
  }

private:
  std::unique_ptr<base_holder> held_;
};

using task_type = movable_func<void()>;

}
}
