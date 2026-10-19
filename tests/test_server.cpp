#include "dispatchlab/http_server.hpp"
#include "doctest.h"

using namespace dispatchlab;

TEST_CASE("console server: url decoding and query parsing") {
    CHECK(url_decode("a%20b+c") == "a b c");
    CHECK(url_decode("%zz") == "%zz");  // malformed escapes pass through
    auto q = parse_query("s=12&t=34&traffic=heavy&flag&x=%2Fapi");
    CHECK(q["s"] == "12");
    CHECK(q["t"] == "34");
    CHECK(q["traffic"] == "heavy");
    CHECK(q.count("flag") == 1);
    CHECK(q["x"] == "/api");
}

TEST_CASE("console server: integer parameters are validated") {
    HttpRequest r;
    r.query = parse_query("n=5&bad=5x&big=99");
    CHECK(r.int_param("n", 0, 0, 10) == 5);
    CHECK(r.int_param("missing", 7, 0, 10) == 7);
    CHECK_THROWS_AS(r.int_param("bad", 0, 0, 10), std::invalid_argument);
    CHECK_THROWS_AS(r.int_param("big", 0, 0, 10), std::invalid_argument);
}

TEST_CASE("console server: mime types") {
    CHECK(std::string(mime_type_for("/index.html")).rfind("text/html", 0) == 0);
    CHECK(std::string(mime_type_for("/fonts/Geist-Variable.woff2")) == "font/woff2");
    CHECK(std::string(mime_type_for("/app.js")).rfind("text/javascript", 0) == 0);
}
