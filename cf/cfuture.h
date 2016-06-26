#include <mutex>
#include <chrono>
#include <condition_variable>
#include <memory>
#include <type_traits>
#include <stdexcept>

namespace cf {

// This is the void type analogue 
// future<void> and promis<void> are explicitely forbidden.
// If you need future just to signal that the async operation is ready,
// use future<unit> and discard the result.
struct unit {};

enum class status {
  ready,
  timeout
};

#define ERRC_LIST(APPLY) \
  APPLY(broken_promise) \
  APPLY(future_already_retrieved) \
  APPLY(promise_already_satisfied) \
  APPLY(no_state)

#define ENUM_APPLY(value) value,

enum class errc {
  ERRC_LIST(ENUM_APPLY)
};

#define STRING_APPLY(value) case errc::value: return #value;

std::string errc_string(errc value) {
  switch (value) {
    ERRC_LIST(STRING_APPLY)
  };
  return "";
}

#undef ENUM_APPLY
#undef STRING_APPLY
#undef ERRC_LIST

struct future_error : public std::logic_error {
  future_error(errc ecode, const std::string& s) 
    : std::logic_error(s),
      ecode_(ecode),
      error_string_(s) 
  {}

  virtual const char* what() const noexcept override {
    return error_string_.data();
  }

  errc ecode() const {
    return ecode_;
  }

  errc ecode_;
  std::string error_string_;
};

template<typename T>
class future;

namespace detail {

class shared_state_base {
public:
  ~shared_state_base() {}
  shared_state_base() 
    : satisfied_(false)
  {}

  void wait() const {
    std::unique_lock<std::mutex> lock(mutex_);
    cond_.wait(lock, [this] { return satisfied_ == true; });
  }

  template<typename Rep, typename Period>
  status wait_for(const std::chrono::duration<Rep, Period>& timeout) const {
    std::unique_lock<std::mutex> lock(mutex_);
    cond_.wait_for(lock, timeout, [this] { return satisfied_ == true; });
    return satisfied_ ? status::ready : status::timeout;
  }

  template<typename Rep, typename Period>
  status wait_until(const std::chrono::duration<Rep, Period>& timeout) const {
    std::unique_lock<std::mutex> lock(mutex_);
    cond_.wait_until(lock, timeout, [this] { return satisfied_ == true; });
    return satisfied_ ? status::ready : status::timeout;
  }

  void set_ready() { 
    // No lock here, because this should be called 
    // when mutex has been already locked.
    if (satisfied_)
      throw future_error(errc::promise_already_satisfied, 
                         errc_string(errc::promise_already_satisfied));
    satisfied_ = true;
    cond_.notify_all();
  }

  bool is_ready() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return satisfied_;
  }

  void set_exception(std::exception_ptr p) {
    std::lock_guard<std::mutex> lock(mutex_);
    exception_ptr_ = p;
    set_ready();
  }

  void abandon() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (satisfied_)
      return;
    exception_ptr_ = std::make_exception_ptr(
        future_error(errc::broken_promise, 
                     errc_string(errc::broken_promise)));
    set_ready();
  }

protected:
  mutable std::mutex mutex_;
  mutable std::condition_variable cond_;
  bool satisfied_;
  std::exception_ptr exception_ptr_;
};

template<typename T>
class shared_state : public shared_state_base {
  using value_type = T;

public:
  template<typename U>
  void set_value(U&& value) {
    std::lock_guard<std::mutex> lock(mutex_);
    set_ready();
    value_ = std::forward<U>(value);
  }

  value_type get_value() const {
    wait();
    if (exception_ptr_)
      std::rethrow_exception(exception_ptr_);
    return std::move(value_);
  }

private:
  value_type value_;
};

template<typename T>
using shared_state_ptr = std::shared_ptr<shared_state<T>>;

template<typename T>
void check_state(const shared_state_ptr<T>& state) {
  if (!state)
    throw future_error(errc::no_state, errc_string(errc::no_state));
}

// various type helpers

// get the return type of a continuation callable
template<typename T, typename F>
using then_arg_ret_type = std::result_of_t<std::decay_t<F>(future<T>)>; 

template<typename T>
struct is_future {
  const static bool value = false;
};

template<typename T>
struct is_future<future<T>> {
  const static bool value = true;
};

// future<T>::then(F&& f) return type
template<typename T, typename F>
using then_ret_type = std::conditional_t<
    is_future<then_arg_ret_type<T, F>>::value,  // if f returns future<U>
    then_arg_ret_type<T, F>,                    // then leave type untouched
    future<then_arg_ret_type<T,F>> >;           // else lift it into the future type

} // namespace detail

template<typename T>
class future {
  template<typename U>
  friend class promise;

  template<typename U>
  friend future<U> make_ready_future(U&& u);

  template<typename U>
  friend future<U> make_exceptional_future(std::exception_ptr p);

public:
  future() = default;

  future(future<T>&& other)
    : state_(std::move(other.state_))
  {}

  future<T>& operator = (future<T>&& other) {
    state_ = std::move(other.state_);
    return *this;
  }

  bool valid() const {
    return state_ != nullptr;
  }

  T get() {
    check_state(state_);
    return state_->get_value();
  }

  template<typename F>
  detail::then_ret_type<T, F> then(F&& f); 
  
  bool is_ready() const {
    check_state(state_);
    return state_->is_ready();
  }

  void wait() const {
    check_state(state_);
    if (state_)
      state_->wait();
  }

  template<typename Rep, typename Period>
  void wait_for(const std::chrono::duration<Rep, Period>& timeout) {
    check_state(state_);
    state_.wait_for(timeout);
  }

  template<typename Rep, typename Period>
  void wait_until(const std::chrono::duration<Rep, Period>& timeout) 
  {
    check_state(state_);
    state_.wait_until(timeout);
  }

private:
  future(const detail::shared_state_ptr<T>& state)
    : state_(state)
  {}

  template<typename F>
  void set_callback(F&& f) {
    check_state(state_);
    state_->set_callback(std::forward<F>(f));
  }

private:
  detail::shared_state_ptr<T> state_;
};

template<typename T>
class promise {
public:
  promise() 
    : state_(std::make_shared<detail::shared_state<T>>()) {}

  promise(promise&& other)
    : state_(std::move(other.state_)) {}

  promise& operator = (promise&& other) {
    state_ = std::move(other.state_);
    return *this;
  }

  ~promise() {
    check_state(state_);
    state_->abandon();
  }

  void swap(promise& other) noexcept {
    state_.swap(other.state_);
  }

  template<typename U>
  void set_value(U&& value) {
    check_state(state_);
    state_->set_value(std::forward<U>(value));
  }

  future<T> get_future() {
    check_state(state_);
    if (state_.use_count() > 1) {
      throw future_error(errc::future_already_retrieved, 
                         errc_string(errc::future_already_retrieved));
    }
    return future<T>(state_);
  }

  void set_exception(std::exception_ptr p) {
    check_state(state_);
    state_->set_exception(p);
  }

private:
  detail::shared_state_ptr<T> state_;
};

template<typename U>
future<U> make_ready_future(U&& u) {
  detail::shared_state_ptr<U> state = 
    std::make_shared<detail::shared_state<U>>();
  state->set_value(std::forward<U>(u));
  return future<U>(state);
}

template<typename U>
future<U> make_exceptional_future(std::exception_ptr p) {
  detail::shared_state_ptr<U> state = 
    std::make_shared<detail::shared_state<U>>();
  state->set_exception(p);
  return future<U>(state);
}

template<typename T>
template<typename F>
detail::then_ret_type<T, F> future<T>::then(F&& f) {
  using R = then_arg_ret_type<T, F>;
  using this_future_type = future<T>;

  promise<R> p;
  future<R> ret = p.get_future();

  auto callback = [this, p_ = std::move(p), f_ = std::forward<F>(f)] {
    p_->set_value(f_(*this));
  };

  return ret;
}

} // namespace cf