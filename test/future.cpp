#define CATCH_CONFIG_MAIN
#include "catch.hh"
#include <type_traits>
#include <cf/cfuture.h>
#include <cf/sync_executor.h>
#include <cf/async_queued_executor.h>
#include <cf/async_thread_pool_executor.h>
#include <cf/time_watcher.h>

// aux stuff for types tests
int foo(const cf::future<char>&);
double foo1(cf::future<int>);
cf::future<double> foo3(cf::future<int>);

auto foo2 = [](const cf::future<bool>&) { return 0; };

struct baz { };
struct test_struct {
  cf::unit bar1(cf::future<baz>) {return cf::unit(); };
};

TEST_CASE("Types") {
  SECTION("Callable return type") {
    test_struct ts;
    auto ts_bar1 = std::bind(&test_struct::bar1, &ts, std::placeholders::_1);

    REQUIRE((std::is_same<int, cf::detail::
        then_arg_ret_type<char, decltype(foo)>>::value) == true);
    REQUIRE((std::is_same<double, cf::detail::
        then_arg_ret_type<int, decltype(foo1)>>::value) == true);
    REQUIRE((std::is_same<int, cf::detail::
        then_arg_ret_type<bool, decltype(foo2)>>::value) == true);
    REQUIRE((std::is_same<cf::unit, cf::detail::
        then_arg_ret_type<baz, decltype(ts_bar1)>>::value) == true);
    REQUIRE((std::is_same<cf::future<double>, cf::detail::
        then_arg_ret_type<int, decltype(foo3)>>::value) == true);
  }

  SECTION("Is future check") {
    REQUIRE((cf::detail::is_future<cf::detail::
        then_arg_ret_type<char, decltype(foo)>>::value) == false);
    REQUIRE((cf::detail::is_future<cf::detail::
        then_arg_ret_type<int, decltype(foo3)>>::value) == true);
  }

  SECTION("Get return type for future::then") {
    using namespace cf::detail;
    using then_ret_type_for_foo = then_ret_type<char, decltype(foo)>;
    REQUIRE((std::is_same<then_ret_type_for_foo,
                          cf::future<int>>::value) == true);

    using then_ret_type_for_foo3 = then_ret_type<int, decltype(foo3)>;
    REQUIRE((std::is_same<then_ret_type_for_foo3,
                          cf::future<double>>::value) == true);
  }
}

TEST_CASE("Future") {
  cf::future<int> future;
  cf::promise<int> promise;

  SECTION("Basic") {
    REQUIRE(!future.valid());

    SECTION("Set value") {
      promise.set_value(56);

      SECTION("Get future") {
        future = promise.get_future();

        REQUIRE(future.valid());
        REQUIRE(future.get() == 56);

        SECTION("Get future second time") {
          try {
            promise.get_future();
            REQUIRE(false);
          } catch (const cf::future_error& error) {
            REQUIRE(error.ecode() == cf::errc::future_already_retrieved);
            REQUIRE(error.what() ==
                    cf::errc_string(cf::errc::future_already_retrieved));
          }
        }

        SECTION("Future operator =") {
          cf::future<int> future1 = std::move(future);

          REQUIRE(!future.valid());
          REQUIRE(future1.valid());
          REQUIRE_THROWS(future1.get());
        }
      }

      SECTION("Set value second time") {
        REQUIRE_THROWS(promise.set_value(42));
      }

      SECTION("Set exception second time") {
        REQUIRE_THROWS(promise.set_exception(
            std::make_exception_ptr(std::logic_error("whatever"))));
      }

      SECTION("Set value second time. Exception string.") {
        try {
          promise.set_value(42);
          REQUIRE(false);
        } catch (const cf::future_error& error) {
          REQUIRE(error.ecode() == cf::errc::promise_already_satisfied);
          REQUIRE(error.what() == 
            cf::errc_string(cf::errc::promise_already_satisfied));
        }
      }
    } // set value

    SECTION("Set exception") {
      promise.set_exception(std::make_exception_ptr(std::logic_error("test")));

      SECTION("get future") {
        future = promise.get_future();

        REQUIRE(future.valid());
        try {
          future.get();
          REQUIRE(false);
        } catch (const std::logic_error& error) {
          REQUIRE(error.what() == std::string("test"));
        }
      }

      SECTION("Set value second time") {
        REQUIRE_THROWS(promise.set_value(42));
      }

      SECTION("Set exception second time") {
        REQUIRE_THROWS(promise.set_exception(
            std::make_exception_ptr(std::logic_error("whatever"))));
      }
    } // set exception

  }

  SECTION("executors mixed with async") {
    cf::async_thread_pool_executor executor(2);
    auto start_point = std::chrono::steady_clock::now();
    
    auto f = cf::async([]{
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
      return 5;
    }).then(executor, [](cf::future<int> f) {
      std::this_thread::sleep_for(std::chrono::milliseconds(5));
      return f.get() * 5;
    }).then([](cf::future<int> f) {
      return cf::async([f = std::move(f)]() mutable {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        return f.get() * 5;
      });
    });
    
    REQUIRE(std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start_point).count() < 5);
    
    REQUIRE(f.get() == 125);
    
    auto diff = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start_point);
    
    REQUIRE(diff >= std::chrono::milliseconds(25));
    REQUIRE(diff < std::chrono::milliseconds(40));
  }

  SECTION("Simple several threads") {
    cf::promise<std::string> p;
    auto f = p.get_future();
    std::thread([p = std::move(p)] () mutable {
      std::this_thread::sleep_for(std::chrono::milliseconds(5));
      p.set_value("Hi!");
    }).detach();
    REQUIRE(f.get() == "Hi!");
  }
}

TEST_CASE("async") {
  SECTION("in a row") {
    cf::async_queued_executor executor;
    auto f = cf::async([] {
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
      return std::string("Hello ");
    }).then(executor, [] (cf::future<std::string> f) {
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
      return f.get() + "futures ";
    }).then([] (cf::future<std::string> f) {
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
      return f.get() + "world!";
    });
    REQUIRE(!f.is_ready());
    REQUIRE(f.get() == "Hello futures world!");
  }
  
  SECTION("async simple with args") {
    auto f = cf::async([](int i) { 
      std::this_thread::sleep_for(std::chrono::milliseconds(300));
      return i; 
    }, 42);
    REQUIRE(f.get() == 42);
  }

  SECTION("async simple without args") {
    auto f = cf::async([]() { 
      std::this_thread::sleep_for(std::chrono::milliseconds(300));
      return 42; 
    });
    REQUIRE(f.get() == 42);
  }
  
  SECTION("simple") {
    auto f = cf::async([] {
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
      return std::string("Hello");
    });
    REQUIRE(!f.is_ready());
    std::this_thread::sleep_for(std::chrono::milliseconds(15));
    REQUIRE(f.is_ready());
    REQUIRE(f.get() == "Hello");
  }
  
  SECTION("tp executor") {
    cf::async_thread_pool_executor executor(1);
    auto f = cf::async(executor, [] {
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
      return std::string("Hello");
    });
    REQUIRE(!f.is_ready());
    std::this_thread::sleep_for(std::chrono::milliseconds(15));
    REQUIRE(f.is_ready());
    REQUIRE(f.get() == "Hello");
  }
  
  SECTION("tp executor 2") {
    cf::async_thread_pool_executor executor(2);
    std::vector<cf::future<std::string>> v;
    
    for (size_t i = 0; i < 10; ++i) {
      v.emplace_back(cf::async(executor, [i] {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        return std::string("Hello") + std::to_string(i);
      }));
    }
    
    for (size_t i = 0; i < 10; ++i) {
      REQUIRE(!v[i].is_ready());
    }
    
    std::this_thread::sleep_for(std::chrono::milliseconds(150));
    
    for (size_t i = 0; i < 10; ++i) {
      REQUIRE(v[i].is_ready());
      REQUIRE(v[i].get() == std::string("Hello") + std::to_string(i));
    }
  }
}

TEST_CASE("Exceptions") {
  SECTION("exception in async") {
    SECTION("1") {
      auto f = cf::async([] {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        throw std::runtime_error("Exception");
        return std::string("Hello");
      });
      REQUIRE(!f.is_ready());
      std::this_thread::sleep_for(std::chrono::milliseconds(15));
      REQUIRE(f.is_ready());
      try {
        f.get();
        REQUIRE(false);
      } catch (const std::exception& e) {
        REQUIRE(e.what() == std::string("Exception"));
      }
    }
  }
  
  SECTION("exception fallthrough in then") {
    auto f = cf::async([] {
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
      throw std::runtime_error("Exception");
      return std::string("Hello");
    }).then([] (cf::future<std::string>) {
      REQUIRE(false);
      return cf::unit();
    }).then([] (cf::future<cf::unit>) {
      REQUIRE(false);
      return cf::unit();
    });
    REQUIRE(!f.is_ready());
    try {
      f.get();
      REQUIRE(false);
    } catch (const std::exception& e) {
      REQUIRE(e.what() == std::string("Exception"));
    }
  }
  
  SECTION("exception fallthrough in then via executor") {
    cf::async_thread_pool_executor executor(1);
    auto f = cf::async(executor, [] {
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
      throw std::runtime_error("Exception");
      return std::string("Hello");
    }).then(executor, [] (cf::future<std::string>) {
      REQUIRE(false);
      return cf::unit();
    }).then(executor, [] (cf::future<cf::unit>) {
      REQUIRE(false);
      return cf::unit();
    });
    REQUIRE(!f.is_ready());
    try {
      f.get();
      REQUIRE(false);
    } catch (const std::exception& e) {
      REQUIRE(e.what() == std::string("Exception"));
    }
  }
}

TEST_CASE("Time watcher") {
  cf::time_watcher tw;
  std::vector<std::chrono::time_point<std::chrono::steady_clock>> tp_vec;
  auto start_point = std::chrono::steady_clock::now();
  
  tw.add([&tp_vec] {
    tp_vec.push_back(std::chrono::steady_clock::now());
  }, std::chrono::milliseconds(100));
  
  tw.add([&tp_vec] {
    tp_vec.push_back(std::chrono::steady_clock::now());
  }, std::chrono::milliseconds(200));
  
  std::this_thread::sleep_for(std::chrono::milliseconds(500));
  REQUIRE(tp_vec.size() == 2);
  REQUIRE(tp_vec[0] - start_point < std::chrono::milliseconds(110));
  REQUIRE(tp_vec[0] - start_point > std::chrono::milliseconds(90));
  
  REQUIRE(tp_vec[1] - start_point < std::chrono::milliseconds(210));
  REQUIRE(tp_vec[1] - start_point > std::chrono::milliseconds(190));
}

TEST_CASE("Make future functions") {
  SECTION("Make ready") {
    cf::future<int> f = cf::make_ready_future(42);
    REQUIRE(f.is_ready());
    REQUIRE(f.valid());
    REQUIRE(f.get() == 42);
  }
  
  SECTION("Make excetion")
  {
    cf::future<int> f = cf::make_exceptional_future<int>(
        std::make_exception_ptr(std::logic_error("whatever")));
    REQUIRE(f.is_ready());
    REQUIRE(f.valid());
    REQUIRE_THROWS(f.get());
  }
}

cf::future<double> tfoo(cf::future<int> f) {
  return cf::make_ready_future<double>(f.get());
}

TEST_CASE("Then test") {
  SECTION("Continuation returns future") {
    auto cont = [](cf::future<double> f) -> cf::future<char> {
      return cf::make_ready_future<char>(f.get());
    };
    auto result = cf::make_ready_future<int>(5).then(
      &tfoo
    ).then(
      cont
    );
    REQUIRE(result.get() == (char)5.0);
  }

  SECTION("Continuation returns non future") {
    auto result = cf::make_ready_future<int>(42).then(
    [](cf::future<int> f) -> double {
      return (double)f.get();
    }).then([](cf::future<double> f) -> char {
      return (char)f.get();
    });
    REQUIRE(result.get() == 42);
  }

  SECTION("Continuation returns non future executor") {
    cf::sync_executor sync_executor;
    auto result = cf::make_ready_future<int>(42)
      .then(sync_executor, [](cf::future<int> f) {
        return (double)f.get();
      }).then(sync_executor, [](cf::future<double> f) {
        return (char)f.get();
      });
    REQUIRE(result.get() == 42);
  }
}

TEST_CASE("Executors") {
  SECTION("Async queued executor")
  {
    cf::async_queued_executor executor;
    int counter = 0;
    
    auto result = cf::async([&counter] {
      ++counter;
      return 42;
    }).then(executor, [&counter](cf::future<int> f) {
      ++counter;
      f.get();
      return std::string("Hello");
    }).then(executor, [&counter](cf::future<std::string> f) {
      ++counter;
      return f.get() + " world!";
    });
    
    REQUIRE(result.get() == "Hello world!");
    REQUIRE(counter == 3);
  }

  SECTION("Thread pool executor") {
    SECTION("basic") {
      cf::async_thread_pool_executor executor(2);
      REQUIRE(executor.available() == 2);
      executor.post([] {
        std::this_thread::sleep_for(std::chrono::milliseconds(400));
      });
      executor.post([] {
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
      });
      std::this_thread::sleep_for(std::chrono::milliseconds(50));
      REQUIRE(executor.available() == 0);
      std::this_thread::sleep_for(std::chrono::milliseconds(250));
      REQUIRE(executor.available() == 1);
      executor.post([] {
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
      });
      std::this_thread::sleep_for(std::chrono::milliseconds(20));
      REQUIRE(executor.available() == 0);
      std::this_thread::sleep_for(std::chrono::milliseconds(300));
      REQUIRE(executor.available() == 2);
    }

    SECTION("basic wait") {
      cf::async_thread_pool_executor executor(2);
      REQUIRE(executor.available() == 2);
      executor.post([] {
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
      });
      executor.post([] {
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
      });
      std::this_thread::sleep_for(std::chrono::milliseconds(5));
      REQUIRE(executor.available() == 0);
      executor.post([] {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
      });
      std::this_thread::sleep_for(std::chrono::milliseconds(50));
      REQUIRE(executor.available() == 0);
      executor.post([] {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
      });
      REQUIRE(executor.available() == 0);
      std::this_thread::sleep_for(std::chrono::milliseconds(700));
      REQUIRE(executor.available() == 2);
    }

    SECTION("future") {
      cf::async_thread_pool_executor executor(5);
      cf::future<int> f = cf::make_ready_future(0);
      
      for (size_t i = 0; i < 10; ++i) {
        f = f.then(executor, [i](cf::future<int> f) {
          std::this_thread::sleep_for(std::chrono::milliseconds(5 * (i + 3)));
          int val = f.get();
          REQUIRE(val == i);
          return ++val;
        });
      }
      
      REQUIRE(f.get() == 10);
    }
  }
}

template<size_t I>
struct tuple_getter {
  template<typename... Args>
  static auto apply(size_t i, std::tuple<Args...>& t) {
    if (i == I) {
      return std::get<I>(t);
    } else {
      return tuple_getter<I+1>::apply(i, t);
    }
  }
};

TEST_CASE("When all") {
  SECTION("Simple vector") {
    const size_t size = 5;
    std::vector<cf::future<int>> vec;

    SECTION("Async") {
      for (size_t i = 0; i < size; ++i) {
        vec.push_back(cf::async([i] {
          std::this_thread::sleep_for(std::chrono::milliseconds(i * 30));
          return (int)i;
        }));
      }
      
      std::this_thread::sleep_for(std::chrono::milliseconds(5));
      REQUIRE(vec[0].is_ready());
      
      for (size_t i = 1; i < size; ++i)
        REQUIRE(!vec[i].is_ready());
      
      auto when_all_result_future = cf::when_all(vec.begin(), vec.end());
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
      
      REQUIRE(!when_all_result_future.is_ready());
      
      auto when_all_result = when_all_result_future.get();
      REQUIRE(when_all_result.size() == size);
      
      for (size_t i = 0; i < size; ++i)
        REQUIRE(when_all_result[i].get() == i);
    }

    SECTION("Ready futures") {
      for (size_t i = 0; i < size; ++i) {
        vec.push_back(cf::make_ready_future((int)i));
      }
      auto when_all_result = cf::when_all(vec.begin(), vec.end()).get();
      REQUIRE(when_all_result.size() == size);
      for (size_t i = 0; i < size; ++i) {
        REQUIRE(when_all_result[i].get() == i);
      }
    }
  }

  SECTION("Simple tuple") {
    auto when_all_result = cf::when_all(
      cf::async([]{ return 1; }), 
      cf::async([]{ return cf::unit(); })).get();
    REQUIRE(std::get<0>(when_all_result).get() == 1);
    REQUIRE(std::get<1>(when_all_result).get() == cf::unit());
  }
}

TEST_CASE("When any") {
  SECTION("Simple vector") {
    const size_t size = 5;
    std::vector<cf::future<int>> vec;

    SECTION("Async") {
      for (size_t i = 0; i < size; ++i) {
        vec.push_back(cf::async([i, size] {
          std::this_thread::sleep_for(std::chrono::milliseconds((size - i) * 30));
          return (int)i;
        }));
      }
      
      auto when_any_result= cf::when_any(vec.begin(), vec.end()).get();
      REQUIRE(when_any_result.sequence.size() == size);
      REQUIRE(when_any_result.index == 4);
      REQUIRE(when_any_result.sequence[4].is_ready());
      REQUIRE(when_any_result.sequence[4].get() == 4);
    }

    SECTION("Ready futures") {
      for (size_t i = 0; i < size; ++i) {
        vec.push_back(cf::make_ready_future((int)i));
      }
      
      auto when_any_result= cf::when_any(vec.begin(), vec.end()).get();
      REQUIRE(when_any_result.sequence.size() == size);
      REQUIRE(when_any_result.index == 0);
      REQUIRE(when_any_result.sequence[0].is_ready());
      REQUIRE(when_any_result.sequence[0].get() == 0);
    }
  }
  
  SECTION("Vector") {
    const size_t size = 20;
    std::vector<cf::future<int>> vec;
    cf::async_thread_pool_executor executor(2);
    
    for (size_t i = 0; i < size; ++i) {
      vec.push_back(cf::async(executor, [i, size] {
        std::this_thread::sleep_for(std::chrono::milliseconds((i+1) * 50));
        return (int)i;
      }));
    }
    
    for (size_t i = 0; i < size; ++i) {
      auto when_any_result = cf::when_any(vec.begin(), vec.end()).get();
      REQUIRE(when_any_result.sequence[0].is_ready());
      //REQUIRE(when_any_result.sequence[0].get() == i);
      
      vec.clear();
      
      for (size_t j = 1; j < when_any_result.sequence.size(); ++j) {
        REQUIRE(!when_any_result.sequence[j].is_ready());
        vec.push_back(std::move(when_any_result.sequence[j]));
      }
    }
  }
  
  SECTION("tuple async") {
    auto when_any_result = cf::when_any(
      cf::async([] { 
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        return 1; 
      }), 
      cf::async([] { 
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        return cf::unit(); 
      })).get();
    
    REQUIRE(when_any_result.index == 1);
    REQUIRE(std::get<1>(when_any_result.sequence).get() == cf::unit());
  }
  
  SECTION("tuple ready") {
    auto when_any_result = cf::when_any(cf::make_ready_future(42),
      cf::async([] { 
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        return cf::unit(); 
      })).get();
    
    REQUIRE(when_any_result.index == 0);
    REQUIRE(std::get<0>(when_any_result.sequence).get() == 42);
  }

  SECTION("When w executors") {
    cf::async_queued_executor queue_executor;
    cf::async_thread_pool_executor tp_executor(1);
    
    auto when_any_result_future = cf::when_any(
      cf::make_ready_future<std::string>("Hello ").then(queue_executor,
        [] (cf::future<std::string> f) mutable {
          std::this_thread::sleep_for(std::chrono::milliseconds(25));
          return f.get() + "composable ";
        }).then(tp_executor, [] (cf::future<std::string> f) mutable {
          std::this_thread::sleep_for(std::chrono::milliseconds(25));
          return f.get() + "futures!";
        }),
      cf::make_ready_future<std::string>("Hello ").then(queue_executor,
        [] (cf::future<std::string> f) mutable {
          std::this_thread::sleep_for(std::chrono::milliseconds(25));
          return f.get() + "composable ";
        }).then(tp_executor, [] (cf::future<std::string> f) mutable {
          std::this_thread::sleep_for(std::chrono::milliseconds(35));
          return f.get() + "futures ";
        }).then(tp_executor, [] (cf::future<std::string> f) mutable {
          std::this_thread::sleep_for(std::chrono::milliseconds(75));
          return f.get() + "world!";
        }));
    
    REQUIRE(!when_any_result_future.is_ready());
    auto when_any_result = when_any_result_future.get();
    REQUIRE(std::get<1>(when_any_result.sequence).is_ready() == false);
    
    REQUIRE(when_any_result.index == 0);
    REQUIRE(std::get<0>(when_any_result.sequence).get() ==
            "Hello composable futures!");
    
    auto next_when_any_result = cf::when_any(
      std::move(std::get<1>(when_any_result.sequence))).get();
    
    REQUIRE(next_when_any_result.index == 0);
    REQUIRE(std::get<0>(next_when_any_result.sequence).get() ==
            "Hello composable futures world!");    
  }
}