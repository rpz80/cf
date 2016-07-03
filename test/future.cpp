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

TEST_CASE("Types", "[future]") {
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

TEST_CASE("Future", "[future][promise][basic][single-thread]") {
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
          REQUIRE(future1.get() == 56);
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

TEST_CASE("Make future functions", "[future]") {
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
  SECTION("Single thread") {
    auto cont = [](cf::future<int> f) -> cf::future<double> {
      return cf::make_ready_future<double>(f.get());
    };
    auto result = cf::make_ready_future<int>(5)
      .then(&tfoo);
    REQUIRE(result.get() == 5.0);
  }
}