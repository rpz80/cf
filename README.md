# Composable futures C++ library (Cf)
This is an implementation of composable, continuing c++17 like [futures](http://en.cppreference.com/w/cpp/experimental/concurrency#Continuations and other extensions for std::future). The most useful/interesting parts, as I personally see them, are ready. Some other, currently not implemented features, will come soon I hope, while the rest (like void future/promise specializations, launch policies) are not likely to ever emerge.

Cf library consists of just one header with no dependencies except c++14 compliant standard library. Cf library comes with a constantly growing unit test suit written using wonderful [Catch](https://github.com/philsquared/Catch) testing framework. These tests may also be used as an a source of examples.

## Executors
The most significant Cf difference from standard futures is the Executor concept. Executor may be an object of virtually any type which has `post(std::function<void()>)` member function. It enables continuations and callables passed to the `cf::async` be executed via separate thread/process/coroutine/etc execution context.
Cf comes with three executors shipped. They are: 
* `cf::sync_executor` - executes callable in place. This is just for the generic code convinience.
* `cf::async_queued_executor` - non blocking async queued executor.
* `cf::async_thread_pool_executor` - almost same as above, except posted callables may be executed on one of the free worker threads.

## Cf current state
|Feature name|Standard library (including c++17)|CF   |Compliance|
|------------|:--------------------------------:|:---:|----------|
|[future](http://en.cppreference.com/w/cpp/experimental/future)|Yes|Yes|No share() member function. No void (use cf::unit instead) and T& specializations.|
|[promise](http://en.cppreference.com/w/cpp/thread/promise)|Yes|Yes|No set_\*\*_at_thread_exit member functions. No void and T& specializations.|
|[async](http://en.cppreference.com/w/cpp/thread/async)|Yes|Yes|No launch policy.|
|[packaged_task](http://en.cppreference.com/w/cpp/thread/packaged_task)|Yes|No||
|[shared_future](http://en.cppreference.com/w/cpp/thread/shared_future)|Yes|No||
|[when_all](http://en.cppreference.com/w/cpp/experimental/when_all)|Yes|Yes||
|[when_any](http://en.cppreference.com/w/cpp/experimental/when_any)|Yes|Yes||
|[make_ready_future](http://en.cppreference.com/w/cpp/experimental/make_ready_future)|Yes|Yes||
|[make_exceptional_future](http://en.cppreference.com/w/cpp/experimental/make_exceptional_future)|Yes|Yes||

## Examples
For the basic future/promise/async examples please refer to http://en.cppreference.com/w/cpp/thread#Futures.

Async + then + then via executor
```c++
cf::async_queued_executor executor;
auto f = cf::async([] {
  http_response resp = http_request("my-site.com");
  resp.read_headers();                     // This is executed on the separate standalone thread
  return resp;                             // Result, when it's ready, is stored in cf::future<http_response>.
}).then([] (cf::future<http_response> f) { // Which in turn is passed to the continuation.
  auto resp = f.get();
  if (resp.code() == http::Ok)             // The continuation may be executed on different contexts.
    resp.read_body();                      // This time - on the async thread.
  return resp;                             
}).then(executor, [] (cf::future<http_response> f) {
  auto resp = f.get();                     // And this time on the async_queued_executor context.
  process(resp.body());
  return cf::unit();                       // When you don't need result - use cf::unit.
}).then([] (cf::future<cf::unit>) {
  log() << "body processed" << std::endl;
});

f.wait();
```
Async itself may be called with the executor. It is one of the reasons why there are no `launch::policy` in Cf. Every possible policy (async, deferred, in place) may easily be implemented as an executor. For example:

```c++
cf::sync_executor executor;
cf::async(executor, [] {
  std::this_thread::sleep_for(std::chrono::milliseconds(10)); // This is evaluated in place, in this case exactly like 
  return std::string("Hello ")                                // std::async with the std::launch::deferred policy.
}).get();
```
You can return futures from continuations or 'plain' values. The latter will be lifted in the future by Cf implicitely. I.e.

```c++
auto f = cd::make_ready_future(42);
std::is_same<
  decltype(f.then([](cf::future<int> f) { // then() return type = cf::future<int>
    return f.get() * 2;
  })), 
  decltype(f.then([](cf::future<int> f) { // same here (not cf::future<cf::future<int>>)
    return cf::async([f = std::move(f)] { 
      return f.get() * 2; 
    });}))
>::value == true;
```
when_any
