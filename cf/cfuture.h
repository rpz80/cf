#include <mutex>
#include <thread>
#include <chrono>
#include <condition_variable>
#include <memory>
#include <type_traits>
#include <stdexcept>
#include <functional>
#include <atomic>
#include <queue>
#include <vector>
#include <iterator>
#include <tuple>

namespace cf {

namespace detail {

// We can't use std::function as continuation holder, as it 
// requires held type to be copyable. So here is simple move-only
// callable wrapper.
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
  movable_func(movable_func<R(Args...)>&& other) = default;
  movable_func& operator = (movable_func<R(Args...)>&& other) = default;
  movable_func(const movable_func<R(Args...)>& other) = delete;
  movable_func& operator = (const movable_func<R(Args...)>& other) = delete;

  R operator() (Args... args) const {
    return held_->operator()(args...);
  }

private:
  std::unique_ptr<base_holder> held_;
};

using task_type = std::function<void()>;
}

// Various executors
class sync_executor {
public:
  void post(const detail::task_type& f) {
    f();
  }
};

class async_queued_executor {
public:
  async_queued_executor() : need_stop_(false) {
    start();
  }

  ~async_queued_executor() {
    stop();
  }

  void post(const detail::task_type& f) {
    std::lock_guard<std::mutex> lock(mutex_);
    tasks_.push(f);
    cond_.notify_all();
  }

  void stop() {
    {
      std::lock_guard<std::mutex> lock_(mutex_);
      need_stop_ = true;
    }
    cond_.notify_all();
    if (thread_.joinable())
      thread_.join();
  }

private:
  void start() {
    thread_ = std::thread([this] {
      while (true) {
        std::unique_lock<std::mutex> lock(mutex_);
        cond_.wait(lock, [this] { return need_stop_ || !tasks_.empty(); });
        if (need_stop_)
          break;
        while (!tasks_.empty()) {
          auto f = tasks_.front();
          tasks_.pop();
          lock.unlock();
          f();
          lock.lock();
        }
      }
    });
  }

private:
  std::queue<detail::task_type> tasks_;
  std::mutex mutex_;
  std::condition_variable cond_;
  std::thread thread_;
  bool need_stop_;
};

class async_thread_pool_executor {
  class worker_thread {
  public:
    worker_thread() {
      thread_ = std::thread([this] {
        while (!need_stop_) {
          std::unique_lock<std::mutex> lock(m_);
          start_cond_.wait(lock, [this] {
            return (bool)task_ || need_stop_; 
          });
          if (need_stop_)
            return;
          lock.unlock();
          task_();
          lock.lock();
          task_ = nullptr;
          completion_cb_();
        }
      });
    }

    ~worker_thread() {
      stop();
    }

    void stop() {
      {
        std::lock_guard<std::mutex> lock(m_);
        need_stop_ = true;
      }
      start_cond_.notify_all();
      if (thread_.joinable())
        thread_.join();
    }

    bool available() const {
      std::lock_guard<std::mutex> lock(m_);
      return !(bool)task_;
    }
    
    void post(const detail::task_type& task,
              const detail::task_type& completion_cb) {
      std::unique_lock<std::mutex> lock(m_);
      if (task_)
        throw std::logic_error("Worker already has a pending task");
      start_task(task, completion_cb);
    }

  private:
    void start_task(const detail::task_type& task,
                    const detail::task_type& completion_cb) {
      task_ = task;
      completion_cb_ = completion_cb;
      start_cond_.notify_one();
    }

  private:
    std::thread thread_;
    mutable std::mutex m_;
    bool need_stop_ = false;
    std::condition_variable start_cond_;
    detail::task_type task_;
    detail::task_type completion_cb_;
  };

  using tp_type = std::vector<worker_thread>;

public:
  async_thread_pool_executor(size_t size)
    : tp_(size),
      need_stop_(false) {
    manager_thread_ = std::thread([this] {
      while (!need_stop_) {
        std::unique_lock<std::mutex> lock(mutex_);
        cond_.wait(lock, [this] {
          return need_stop_ || !task_queue_.empty(); 
        });
        while (!task_queue_.empty()) {
          if (need_stop_)
            return;
          auto ready_it = std::find_if(tp_.begin(), tp_.end(), 
          [](const worker_thread& worker) {
              return worker.available();
          });
          if (ready_it == tp_.end())
            break;
          auto task = task_queue_.front();
          task_queue_.pop();
          ready_it->post(task, [this] {
            cond_.notify_one();
          });
        }
      }
    });
  }

  ~async_thread_pool_executor() {
    need_stop_ = true;
    cond_.notify_all();
    if (manager_thread_.joinable())
      manager_thread_.join();
    for (auto& worker : tp_) {
      worker.stop();
    }
  }

  size_t available() const {
    return std::count_if(tp_.begin(), tp_.end(), 
    [](const worker_thread& worker) { 
      return worker.available();
    });
  }

  void post(const detail::task_type& task) {
    {
      std::unique_lock<std::mutex> lock(mutex_);
      task_queue_.push(task);
    }
    cond_.notify_one();
  }

private:
  tp_type tp_;
  mutable std::mutex mutex_;
  std::queue<detail::task_type> task_queue_;
  std::thread manager_thread_;
  bool need_stop_;
  std::condition_variable cond_;
};

// This is the void type analogue. 
// Future<void> and promise<void> are explicitly forbidden.
// If you need future just to signal that the async operation is ready,
// use future<unit> and discard the result.
struct unit {};
inline bool operator == (unit lhs, unit rhs) { return true; }
inline bool operator != (unit lhs, unit rhs) { return false; }

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

struct future_error : public std::exception {
  future_error(errc ecode, const std::string& s)
    : ecode_(ecode),
    error_string_(s) {}

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

template<typename Derived>
class shared_state_base {
  using cb_type = movable_func<void()>;
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

  void set_ready(std::unique_lock<std::mutex>& lock) {
    satisfied_ = true;
    cond_.notify_all();
    lock.unlock();
    cb_();
  }

  template<typename F>
  void set_callback(F&& f) {
    std::unique_lock<std::mutex> lock(mutex_);
    cb_ = std::forward<F>(f);
    lock.unlock();
    if (satisfied_)
      cb_();
  }

  bool is_ready() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return satisfied_;
  }

  void set_exception(std::exception_ptr p) {
    std::unique_lock<std::mutex> lock(mutex_);
    throw_if_satisfied();
    exception_ptr_ = p;
    set_ready(lock);
  }

  bool has_exception() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return (bool)exception_ptr_;
  }

  std::exception_ptr get_exception() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return exception_ptr_;
  }

  void abandon() {
    std::unique_lock<std::mutex> lock(mutex_);
    if (satisfied_)
      return;
    exception_ptr_ = std::make_exception_ptr(
        future_error(errc::broken_promise,
                     errc_string(errc::broken_promise)));
    set_ready(lock);
  }

protected:
  void throw_if_satisfied() {
    if (satisfied_)
      throw future_error(errc::promise_already_satisfied, 
                         errc_string(errc::promise_already_satisfied));
  }

protected:
  mutable std::mutex mutex_;
  mutable std::condition_variable cond_;
  std::atomic<bool> satisfied_;
  std::exception_ptr exception_ptr_;
  cb_type cb_ = []() {};
};

template<typename T>
class shared_state : public shared_state_base<shared_state<T>>,
                     public std::enable_shared_from_this<shared_state<T>> {
  using value_type = T;
  using base_type = shared_state_base<shared_state<T>>;

public:
  template<typename U>
  void set_value(U&& value) {
    std::unique_lock<std::mutex> lock(base_type::mutex_);
    base_type::throw_if_satisfied();
    value_ = std::forward<U>(value);
    base_type::set_ready(lock);
  }

  value_type get_value() {
    base_type::wait();
    if (base_type::exception_ptr_)
      std::rethrow_exception(base_type::exception_ptr_);
    if (value_retrieved_)
      throw future_error(errc::future_already_retrieved,
                         errc_string(errc::future_already_retrieved));
    value_retrieved_ = true;
    return std::move(value_);
  }

private:
  value_type value_;
  bool value_retrieved_ = false;
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

template<typename F, typename... Args>
using callable_ret_type = std::result_of_t<std::decay_t<F>(Args...)>;

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
  future<then_arg_ret_type<T, F>> >;          // else lift it into the future type

template<typename T>
struct future_held_type;

template<typename T>
struct future_held_type<future<T>> {
  using type = std::decay_t<T>;
};

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

  future(const future<T>& other) = delete;
  future<T>& operator = (const future<T>& other) = delete;

  future(future<T>&& other)
    : state_(std::move(other.state_)) {}

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

  template<typename F, typename Executor>
  detail::then_ret_type<T, F> then(F&& f, Executor& executor);

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
  void wait_until(const std::chrono::duration<Rep, Period>& timeout) {
    check_state(state_);
    state_.wait_until(timeout);
  }

private:
  future(const detail::shared_state_ptr<T>& state)
    : state_(state) {}

  template<typename F>
  typename std::enable_if<
    detail::is_future<
      detail::then_arg_ret_type<T, F>
    >::value,
    detail::then_ret_type<T, F>
  >::type
  then_impl(F&& f);

  template<typename F>
  typename std::enable_if<
    !detail::is_future<
      detail::then_arg_ret_type<T, F>
    >::value,
    detail::then_ret_type<T, F>
  >::type
  then_impl(F&& f);

  template<typename F, typename Executor>
  typename std::enable_if<
    detail::is_future<
      detail::then_arg_ret_type<T, F>
    >::value,
    detail::then_ret_type<T, F>
  >::type
  then_impl(F&& f, Executor& executor);

  template<typename F, typename Executor>
  typename std::enable_if<
    !detail::is_future<
      detail::then_arg_ret_type<T, F>
    >::value,
    detail::then_ret_type<T, F>
  >::type
  then_impl(F&& f, Executor& executor);

  template<typename F>
  void set_callback(F&& f) {
    check_state(state_);
    state_->set_callback(std::forward<F>(f));
  }

private:
  detail::shared_state_ptr<T> state_;
};

template<>
class future<void>;

// TODO: then(f, executor) --> then(executor, f)

template<typename T>
template<typename F>
detail::then_ret_type<T, F> future<T>::then(F&& f) {
  check_state(state_);
  return then_impl<F>(std::forward<F>(f));
}

template<typename T>
template<typename F, typename Executor>
detail::then_ret_type<T, F> future<T>::then(F&& f, Executor& executor) {
  check_state(state_);
  return then_impl<F>(std::forward<F>(f), executor);
}

template<typename T>
class promise;

template<typename T>
future<T> make_ready_future(T&& t);

// future<R> F(future<T>) specialization
template<typename T>
template<typename F>
typename std::enable_if<
  detail::is_future<
    detail::then_arg_ret_type<T, F>
  >::value,
  detail::then_ret_type<T, F>
>::type
future<T>::then_impl(F&& f) {
  using R = typename detail::future_held_type<
    detail::then_arg_ret_type<T, F>
  >::type;
  promise<R> p;
  future<R> ret = p.get_future();
  set_callback([p = std::move(p), f = std::forward<F>(f), 
               state = this->state_->shared_from_this()] () mutable {
    if (state->has_exception())
      p.set_exception(state->get_exception());
    else {
      try {
        auto inner_f = f(cf::make_ready_future<T>(state->get_value()));
        p.set_value(inner_f.get());
      } catch (...) {
        p.set_exception(std::current_exception());
      }
    }
  });

  return ret;
}

// R F(future<T>) specialization
template<typename T>
template<typename F>
typename std::enable_if<
  !detail::is_future<
    detail::then_arg_ret_type<T, F>
  >::value,
  detail::then_ret_type<T, F>
>::type
future<T>::then_impl(F&& f) {
  using R = detail::then_arg_ret_type<T, F>;
  promise<R> p;
  future<R> ret = p.get_future();
  set_callback([p = std::move(p), f = std::forward<F>(f),
               state = this->state_->shared_from_this()] () mutable {
    if (state->has_exception())
      p.set_exception(state->get_exception());
    else {
      try {
        p.set_value(f(cf::make_ready_future<T>(state->get_value())));
      } catch (...) {
        p.set_exception(std::current_exception());
      }
    }
  });

  return ret;
}

// future<R> F(future<T>) specialization via executor
template<typename T>
template<typename F, typename Executor>
typename std::enable_if<
  detail::is_future<
    detail::then_arg_ret_type<T, F>
  >::value,
  detail::then_ret_type<T, F>
>::type
future<T>::then_impl(F&& f, Executor& executor) {
  using R = typename detail::future_held_type<
    detail::then_arg_ret_type<T, F>
  >::type;
  promise<R> p;
  future<R> ret = p.get_future();
  set_callback([p = std::move(p), f = std::forward<F>(f),
               state = this->state_->shared_from_this(), &executor] () mutable {
    if (state->has_exception())
      p.set_exception(state->get_exception());
    else {
      executor.post([&p, state, f = std::forward<F>(f)] () mutable {
        try {
          auto inner_f = f(cf::make_ready_future<T>(state->get_value()));
          p.set_value(inner_f.get());
        } catch (...) {
          p.set_exception(std::current_exception());
        }
      });
    }
  });

  return ret;
}

// R F(future<T>) specialization via executor
template<typename T>
template<typename F, typename Executor>
typename std::enable_if<
  !detail::is_future<
    detail::then_arg_ret_type<T, F>
  >::value,
  detail::then_ret_type<T, F>
>::type
future<T>::then_impl(F&& f, Executor& executor) {
  using R = detail::then_arg_ret_type<T, F>;
  promise<R> p;
  future<R> ret = p.get_future();
  set_callback([p = std::move(p), f = std::forward<F>(f),
               state = this->state_->shared_from_this(), &executor] () mutable {
    if (state->has_exception())
      p.set_exception(state->get_exception());
    else {
      executor.post([&p, state, f = std::forward<F>(f)] () mutable {
        try {
          p.set_value(f(cf::make_ready_future<T>(state->get_value())));
        } catch (...) {
          p.set_exception(std::current_exception());
        }
      });
    }
  });

  return ret;
}

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
    if (state_)
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

template<>
class promise<void>;

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

template<typename F, typename... Args>
future<detail::callable_ret_type<F, Args...>> async(F&& f, Args&&... args) {
  using future_inner_type = detail::callable_ret_type<F, Args...>;
  
  promise<future_inner_type> p;
  auto result = p.get_future();
  
  std::thread([p = std::move(p), f = std::forward<F>(f), args...] () mutable {
    p.set_value(std::forward<F>(f)(args...));
  }).detach();
  
  return result;
}

template<typename Executor, typename F, typename... Args>
future<detail::callable_ret_type<F, Args...>> async(Executor& executor, F&& f, Args&&... args) {
  using future_inner_type = detail::callable_ret_type<F, Args...>;
  
  auto promise_ptr = std::make_shared<promise<future_inner_type>>();
  auto result = promise_ptr->get_future();
  executor.post([promise_ptr, f = std::forward<F>(f), args...] () mutable {
    promise_ptr->set_value(std::forward<F>(f)(args...));
  });
  
  return result;
}

// TODO: Check when_all implementation (remove tmp_result? anything else?)

template<typename InputIt>
auto when_all(InputIt first, InputIt last)
-> future<std::vector<typename std::iterator_traits<InputIt>::value_type>> {
  using result_inner_type = 
    std::vector<typename std::iterator_traits<InputIt>::value_type>;
  
  struct context {
    size_t total_futures = 0;
    size_t ready_futures = 0;
    result_inner_type result;
    result_inner_type temp_result;
    std::mutex mutex;
    promise<result_inner_type> p;
  };
  
  auto shared_context = std::make_shared<context>();
  auto result_future = shared_context->p.get_future();
  shared_context->total_futures = std::distance(first, last);
  shared_context->result.resize(shared_context->total_futures);
  shared_context->temp_result.reserve(shared_context->total_futures);
  size_t index = 0;
  
  for (; first != last; ++first, ++index) {
    shared_context->temp_result.push_back(std::move(*first));
    shared_context->temp_result[index].then(
    [shared_context, index] 
    (typename std::iterator_traits<InputIt>::value_type f) mutable {
      {
        std::lock_guard<std::mutex> lock(shared_context->mutex);
        shared_context->result[index] = std::move(f);
        ++shared_context->ready_futures;
        if (shared_context->ready_futures == shared_context->total_futures)
          shared_context->p.set_value(std::move(shared_context->result));
      }
      return unit();
    });
  }
  
  return result_future;
}

namespace detail {
template<size_t I, typename Context, typename Future>
void when_inner_helper(Context context, Future&& f) {
  std::get<I>(context->temp_result) = std::move(f);
  std::get<I>(context->temp_result).then([context](Future f) {
    std::lock_guard<std::mutex> lock(context->mutex);
    ++context->ready_futures;
    std::get<I>(context->result) = std::move(f);
    if (context->ready_futures == context->total_futures)
      context->p.set_value(std::move(context->result));
    return unit();
  });
}

template<size_t I, typename Context>
void apply_helper(const Context& context) {}

template<size_t I, typename Context, typename FirstFuture, typename... Futures>
void apply_helper(const Context& context, FirstFuture&& f, Futures&&... fs) {
  detail::when_inner_helper<I>(context, std::forward<FirstFuture>(f));
  apply_helper<I+1>(context, std::forward<Futures>(fs)...);
}
}

template<typename... Futures>
auto when_all(Futures&&... futures)
-> future<std::tuple<std::decay_t<Futures>...>> {
  using result_inner_type = std::tuple<std::decay_t<Futures>...>;
  struct context {
    size_t total_futures;
    size_t ready_futures = 0;
    result_inner_type result;
    result_inner_type temp_result;
    promise<result_inner_type> p;
    std::mutex mutex;
  };
  auto shared_context = std::make_shared<context>();
  shared_context->total_futures = sizeof...(futures);
  detail::apply_helper<0>(shared_context, std::forward<Futures>(futures)...);
  return shared_context->p.get_future();
}

template<typename Sequence>
struct when_any_result {
  size_t index;
  Sequence sequence;
};

template<typename InputIt>
auto when_any(InputIt first, InputIt last)
->future<
    when_any_result<
      std::vector<
        typename std::iterator_traits<InputIt>::value_type>>> {
  using result_inner_type =
    std::vector<typename std::iterator_traits<InputIt>::value_type>;
  using future_inner_type = when_any_result<result_inner_type>;
  
  struct context {
    size_t total = 0;
    std::atomic<size_t> processed;
    future_inner_type result;
    promise<future_inner_type> p;
    bool ready = false;
    bool result_moved = false;
    std::mutex mutex;
  };
  
  auto shared_context = std::make_shared<context>();
  auto result_future = shared_context->p.get_future();
  shared_context->processed = 0;
  shared_context->total = std::distance(first, last);
  shared_context->result.sequence.reserve(shared_context->total);
  size_t index = 0;
  
  auto first_copy = first;
  for (; first_copy != last; ++first_copy) {
    shared_context->result.sequence.push_back(std::move(*first_copy));
  }
  
  for (; first != last; ++first, ++index) {
    shared_context->result.sequence[index].then(
    [shared_context, index]
    (typename std::iterator_traits<InputIt>::value_type f) mutable {
      {
        std::lock_guard<std::mutex> lock(shared_context->mutex);
        if (!shared_context->ready) {
          shared_context->result.index = index;
          shared_context->ready = true;
          shared_context->result.sequence[index] = std::move(f);
          if (shared_context->processed == shared_context->total &&
              !shared_context->result_moved) {
            shared_context->p.set_value(std::move(shared_context->result));
            shared_context->result_moved = true;
          }
        }
      }
      return unit();
    });
    ++shared_context->processed;
  }
  
  {
    std::lock_guard<std::mutex> lock(shared_context->mutex);
    if (shared_context->ready && !shared_context->result_moved) {
      shared_context->p.set_value(std::move(shared_context->result));
      shared_context->result_moved = true;
    }
  }
  
  return result_future;
}

namespace detail {
template<size_t I, typename Context, typename Future>
void when_any_inner_helper(Context context, Future& f) {
  std::get<I>(context->result.sequence).then(
  [context](Future f) {
    std::lock_guard<std::mutex> lock(context->mutex);
    if (!context->ready) {
      context->ready = true;
      context->result.index = I;
      std::get<I>(context->result.sequence) = std::move(f);
      if (context->processed == context->total &&
          !context->result_moved) {
        context->p.set_value(std::move(context->result));
        context->result_moved = true;
      }
    }
    return unit();
  });
}

template<size_t I, size_t S>
struct when_any_helper_struct {
  template<typename Context, typename... Futures>
  static void apply(const Context& context, std::tuple<Futures...>& t) {
    when_any_inner_helper<I>(context, std::get<I>(t));
    ++context->processed;
    when_any_helper_struct<I+1, S>::apply(context, t);
  }
};

template<size_t S>
struct when_any_helper_struct<S, S> {
  template<typename Context, typename... Futures>
  static void apply(const Context& context, std::tuple<Futures...>& t) {}
};

template<size_t I, typename Context>
void fill_result_helper(const Context& context) {}

template<size_t I, typename Context, typename FirstFuture, typename... Futures>
void fill_result_helper(const Context& context, FirstFuture&& f, Futures&&... fs) {
  std::get<I>(context->result.sequence) = std::move(f);
  fill_result_helper<I+1>(context, std::forward<Futures>(fs)...);
}

}

template<typename... Futures>
auto when_any(Futures&&... futures)
-> future<when_any_result<std::tuple<std::decay_t<Futures>...>>> {
  using result_inner_type = std::tuple<std::decay_t<Futures>...>;
  using future_inner_type = when_any_result<result_inner_type>;
  
  struct context {
    bool ready = false;
    bool result_moved = false;
    size_t total = 0;
    std::atomic<size_t> processed;
    future_inner_type result;
    promise<future_inner_type> p;
    std::mutex mutex;
  };
  
  auto shared_context = std::make_shared<context>();
  shared_context->processed = 0;
  shared_context->total = sizeof...(futures);
  
  detail::fill_result_helper<0>(shared_context, std::forward<Futures>(futures)...);
  detail::when_any_helper_struct<0, sizeof...(futures)>::apply(
      shared_context, shared_context->result.sequence);
  {
    std::lock_guard<std::mutex> lock(shared_context->mutex);
    if (shared_context->ready && !shared_context->result_moved) {
      shared_context->p.set_value(std::move(shared_context->result));
      shared_context->result_moved = true;
    }
  }
  return shared_context->p.get_future();
}
} // namespace cf