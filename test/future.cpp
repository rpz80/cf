#define CATCH_CONFIG_MAIN
#include "catch.hh"
#include <type_traits>
#include <cf/cfuture.h>

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

    REQUIRE((std::is_same<int, cf::detail::then_arg_ret_type<char, decltype(foo)>>::value) == true);
    REQUIRE((std::is_same<double, cf::detail::then_arg_ret_type<int, decltype(foo1)>>::value) == true);
    REQUIRE((std::is_same<int, cf::detail::then_arg_ret_type<bool, decltype(foo2)>>::value) == true);
    REQUIRE((std::is_same<cf::unit, cf::detail::then_arg_ret_type<baz, decltype(ts_bar1)>>::value) == true);
    REQUIRE((std::is_same<cf::future<double>, cf::detail::then_arg_ret_type<int, decltype(foo3)>>::value) == true);
  }

  SECTION("Is future check") {
    REQUIRE((cf::detail::is_future<cf::detail::then_arg_ret_type<char, decltype(foo)>>::value) == false);
    REQUIRE((cf::detail::is_future<cf::detail::then_arg_ret_type<int, decltype(foo3)>>::value) == true);
  }

  SECTION("Get return type for future::then") {
    using namespace cf::detail;
    using then_ret_type_for_foo = then_ret_type<char, decltype(foo)>;
    REQUIRE((std::is_same<then_ret_type_for_foo, cf::future<int>>::value) == true);

    using then_ret_type_for_foo3 = then_ret_type<int, decltype(foo3)>;
    REQUIRE((std::is_same<then_ret_type_for_foo3, cf::future<double>>::value) == true);
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
            REQUIRE(error.what() == cf::errc_string(cf::errc::future_already_retrieved));
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
}

TEST_CASE("Make future functions") {
  SECTION("Make ready") {
    cf::future<int> f = cf::make_ready_future(42);
    REQUIRE(f.is_ready());
    REQUIRE(f.valid());
    REQUIRE(f.get() == 42);
  }
  SECTION("Make excetion") {
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

TEST_CASE("Then simple test") {
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
    auto result = cf::make_ready_future<int>(42).then([](cf::future<int> f) -> double {
      return (double)f.get();
    }).then([](cf::future<double> f) -> char {
      return (char)f.get();
    });
    REQUIRE(result.get() == 42);
  }

  SECTION("Continuation returns non future executor") {
    cf::sync_executor sync_executor;
    auto result = cf::make_ready_future<int>(42)
      .then([](cf::future<int> f) {
        return (double)f.get();
      }, sync_executor).then([](cf::future<double> f) {
        return (char)f.get();
      }, sync_executor);
    REQUIRE(result.get() == 42);
  }
}

TEST_CASE("Async") {
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
}

TEST_CASE("Executors") {
  SECTION("Async queued executor") {
    cf::async_queued_executor executor;
    int counter = 0;
    auto result = cf::async([&counter] {
      ++counter;
      return 42;
    }).then([&counter](cf::future<int> f) {
      ++counter;
      f.get();
      return std::string("Hello");
    }, executor).then([&counter](cf::future<std::string> f) {
      ++counter;
      return f.get() + " world!";
    }, executor);
    REQUIRE(result.get() == "Hello world!");
    REQUIRE(counter == 3);
  }
}

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
    }

    SECTION("Ready futures") {
      for (size_t i = 0; i < size; ++i) {
        vec.push_back(cf::make_ready_future((int)i));
      }
    }

    auto when_all_result = cf::when_all(vec.begin(), vec.end()).get();
    REQUIRE(when_all_result.size() == size);
    for (size_t i = 0; i < size; ++i) {
      REQUIRE(when_all_result[i].get() == i);
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
          std::this_thread::sleep_for(
            std::chrono::milliseconds((size - i) * 30));
          return (int)i;
        }));
      }
      auto when_any_result= cf::when_any(vec.begin(), vec.end()).get();
      REQUIRE(when_any_result.sequence.size() == size);
      REQUIRE(when_any_result.index == 4);
    }

    SECTION("Ready futures") {
      for (size_t i = 0; i < size; ++i) {
        vec.push_back(cf::make_ready_future((int)i));
      }
      auto when_any_result= cf::when_any(vec.begin(), vec.end()).get();
      REQUIRE(when_any_result.sequence.size() == size);
      REQUIRE(when_any_result.index == 0);
    }
  }
}