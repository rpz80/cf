[![Build Status](https://travis-ci.org/rpz80/cf.svg?branch=master)](https://travis-ci.org/rpz80/cf)
# Composable futures C++ library (Cf)
This is an implementation of composable, continuing c++17 like std [futures](https://en.cppreference.com/w/cpp/experimental/concurrency).
Cf library has no dependencies except c++14 compliant standard library. Cf library comes with a an extensive unit test suit written using [Catch](https://github.com/philsquared/Catch) testing framework. These tests may also be used as an a source of examples.
Cf has been tested on three major OS's. Minimum compiler requirements are: gcc-4.8 (gcc-4.9 or above is strongly recommended), clang-3.7, vs2015.

## Executors
Unlike standard futures Cf library implements the Executor concept. Executor may be an object of virtually any type which has `post(cf::movable_func<void()>)` member function. It enables `cf::future` continuations and callables passed to the `cf::async` be executed via separate thread/process/coroutine/etc execution context.
Cf comes with three executors shipped. They are: 
* `cf::sync_executor` - executes callable in place.
* `cf::async_queued_executor` - non blocking async queued executor.
* `cf::async_thread_pool_executor` - almost same as above, except posted callables may be executed on one of the free worker threads.

## Timeouts
Also Cf futures support cancelation after certain timeout expired. User provided exception is stored in relevant future in this case and propagated through all subsequent `future::then`. Timeouts are handled by `cf::time_watcher`. As with executors, TimeWatcher might be an object of any type with `::add(const std::function<void()>&, std::chrono::duration<Rep, Period> timeout)` member function.

## Cf current state
|Feature name|Standard library (including c++17)|Cf   |Standard compliance|
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
|executors|No|Yes||
|timeouts|No|Yes||

## Examples
For the basic future/promise/async examples please refer to http://en.cppreference.com/w/cpp/thread#Futures.

### async, then, then via executor
```c++
cf::async_queued_executor executor;
try {
  auto f = cf::async([] {
    http_response resp = http_request("my-site.com")();
    resp.read_headers();                     // This is executed on the separate standalone thread
    return resp;                             // Result, when it's ready, is stored in cf::future<http_response>.
  }).then([] (cf::future<http_response> f) { // Which in turn is passed to the continuation.
    auto resp = f.get();
    if (resp.code() == http::Ok)             // The continuation may be executed on different contexts.
      resp.read_body();                      // This time - on the async thread.
    else 
      throw std::domain_error("Bad response");
    return resp;                             
  }).then(executor, [] (cf::future<http_response> f) {
    auto resp = f.get();                     // And this time on the async_queued_executor context.
    process(resp.body());
    return cf::unit();                       // When you don't need result - use cf::unit.
  }).then([] (cf::future<cf::unit> f) {
    f.wait();                                // If f has exception inside, this line will let it out
    log() << "body processed" << std::endl;
  }).get();
} catch (const std::exception& e) {
  std::cerr << e.what() << std::endl;
}
```
Async itself may be called with an executor. It is one of the reasons why there are no `launch::policy` in Cf. Every possible policy (async, deferred, etc) may easily be implemented as an executor. For example:

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
### timeouts
```c++
cf::time_watcher tw;
cf::async_thread_pool_executor executor(4);

struct connect_timeout : std::domain_error { using std::domain_error::domain_error; };
struct write_timeout : std::domain_error { using std::domain_error::domain_error; };
struct read_timeout : std::domain_error { using std::domain_error::domain_error; };

try {
  auto client_future = cf::async([client = tcp_client()] () mutable {
    client.connect("mysite.com:8001");
    return client;  // client is moved from future to future. supposed to be a cheap operation
  }).timeout(std::chrono::milliseconds(500), connect_timeout("Connect timeout"), tw).then(executor,
  [](cf::future<tcp_client> client_future) mutable {
    auto client = client_future.get();
    client.write("GET /");
    return client;
  }).timeout(std::chrono::seconds(2), write_timeout("Write timeout"), tw).then(executor,
  [](cf::future<tcp_client> client_future) mutable {
    auto client = client_future.get();
    client.read_until("/r/n/r/n");
    return client;
  }).timeout(std::chrono::seconds(2), read_timeout("Read timeout"), tw);
  
  std::cout << client_future.get().data() << std::endl;

} catch (const connect_timeout& e) {
  std::cerr << e.what() << std::endl;
} catch (const write_timeout& e) {
  std::cerr << e.what() << std::endl;
} catch (const read_timeout& e) {
  std::cerr << e.what() << std::endl; 
}
```
Note though, that timeout timer starts at the point of `cf::future::timeout` invocation, i.e. all timeouts in the example above are scheduled almost at the same moment. Thus when calling `cf::future::timeout` second and subsequent times consider adding up approximate duration of the previous calls when choosing timeout values.

### when_any, when_all
`cf::when_any` and `cf::when_all` return `cf::future` which is ready when any or all of the input sequence futures become ready. These functions have iterator overloads and variadic overloads. Check [when_all](http://en.cppreference.com/w/cpp/experimental/when_all), [when_any](http://en.cppreference.com/w/cpp/experimental/when_any) for more details.
```c++
std::vector<std::string> urls = {"url1.org", "url2.org", "url3.org"};
std::vector<cf::future<http_response>> response_future_vector;
for (size_t i = 0; i < urls.size(); ++i) {
  response_future_vector.push_back(cf::async([&urls, i] {
    auto http_resp = http_request(urls[i])();
    http_resp.read_all();
    return http_resp;
  }));
}

auto result_future = cf::when_any(response_future_vector.begin(), response_future_vector.end());
auto result = result_future.get();    // blocks until one of the futures becomes ready.
                                      // result.index == ready future index
                                      // result.sequence contains original futures with sequence[index] ready

```
