# Composabe C++ futures (Cf)
This is an implementation of composable, continuing c++17 like [futures](http://en.cppreference.com/w/cpp/experimental/future). I'm done with most useful/interesting parts as I personally see it. Some other, currently not implemented features will come soon I hope, while the rest (like void future/promise specializations) are not likely to ever emerge to life.

The most significant Cf difference from standard futures is the Executor concept. Executor is is an object of virtually any class which has `post(std::function<void()>)` member function. It enables continuations and callables passed to the `cf::async` be executed via separate thread/process/coroutine/etc executor context.

## Cf current state
|Feature name|Standard library (including c++17)|CF   |Compliance|
|------------|:--------------------------------:|:---:|----------|
|[future](http://en.cppreference.com/w/cpp/experimental/future)|Yes|Yes|No share() member function. No void (use cf::unit instead) and T& specializations.|
|[promise](http://en.cppreference.com/w/cpp/thread/promise)|Yes|Yes|No set_\*\*_at_thread_exit member functions. No void ans T& specializations.|
|[async](http://en.cppreference.com/w/cpp/thread/async)|Yes|Yes|No launch policy|
|[packaged_task](http://en.cppreference.com/w/cpp/thread/packaged_task)|Yes|No||
|[shared_future](http://en.cppreference.com/w/cpp/thread/shared_future)|Yes|No||
|[when_all](http://en.cppreference.com/w/cpp/experimental/when_all)|Yes|Yes||
|[when_any](http://en.cppreference.com/w/cpp/experimental/when_any)|Yes|Yes||
|[make_ready_future](http://en.cppreference.com/w/cpp/experimental/make_ready_future)|Yes|Yes||
|[make_exceptional_future](http://en.cppreference.com/w/cpp/experimental/make_exceptional_future)|Yes|Yes||

