#define CATCH_CONFIG_MAIN
#include "catch.hh"
#include <cf/cfuture.h>

TEST_CASE("Future", "[future][promise]") {
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