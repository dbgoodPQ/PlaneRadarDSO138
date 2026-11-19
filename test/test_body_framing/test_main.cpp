// BodyFramer: HTTP/1.1 body decoding (Content-Length, close-delimited, chunked).

#include <unity.h>

#include <cstring>
#include <string>

#include "services/http_body_framing.h"

using services::http::BodyFramer;
using services::http::BodyFraming;

namespace {

/** Hands out a fixed wire image one byte at a time, then -1. */
class StringSource {
 public:
  explicit StringSource(const std::string& wire) : wire_(wire) {}
  int read() {
    if (pos_ >= wire_.size()) {
      return -1;
    }
    return static_cast<unsigned char>(wire_[pos_++]);
  }
  size_t consumed() const { return pos_; }

 private:
  std::string wire_;
  size_t pos_ = 0;
};

std::string readAll(BodyFramer<StringSource>& body) {
  std::string out;
  for (int c = body.read(); c >= 0; c = body.read()) {
    out += static_cast<char>(c);
  }
  return out;
}

}  // namespace

void setUp() {}
void tearDown() {}

void test_identity_with_length() {
  StringSource src("hello world, extra");
  BodyFramer<StringSource> body(src, BodyFraming::kIdentity, 11);
  TEST_ASSERT_EQUAL_STRING("hello world", readAll(body).c_str());
  TEST_ASSERT_TRUE(body.complete());
  TEST_ASSERT_FALSE(body.truncated());
  TEST_ASSERT_EQUAL(11, body.bytesRead());
  TEST_ASSERT_EQUAL(11, src.consumed());  // stops at the body's end
}

void test_identity_cut_short() {
  StringSource src("hello");
  BodyFramer<StringSource> body(src, BodyFraming::kIdentity, 11);
  TEST_ASSERT_EQUAL_STRING("hello", readAll(body).c_str());
  TEST_ASSERT_TRUE(body.truncated());
  TEST_ASSERT_FALSE(body.complete());
}

void test_identity_until_close() {
  StringSource src("all of it");
  BodyFramer<StringSource> body(src, BodyFraming::kIdentity, -1);
  TEST_ASSERT_EQUAL_STRING("all of it", readAll(body).c_str());
  TEST_ASSERT_TRUE(body.complete());
  TEST_ASSERT_FALSE(body.truncated());
}

void test_chunked_basic() {
  StringSource src("5\r\nhello\r\n6\r\n world\r\n0\r\n\r\n");
  BodyFramer<StringSource> body(src, BodyFraming::kChunked, -1);
  TEST_ASSERT_EQUAL_STRING("hello world", readAll(body).c_str());
  TEST_ASSERT_TRUE(body.complete());
  TEST_ASSERT_FALSE(body.framingError());
  TEST_ASSERT_EQUAL(11, body.bytesRead());
}

void test_chunked_hex_sizes_upper_and_lower() {
  std::string payload(26, 'x');
  StringSource src("1A\r\n" + payload + "\r\na\r\n0123456789\r\n0\r\n\r\n");
  BodyFramer<StringSource> body(src, BodyFraming::kChunked, -1);
  TEST_ASSERT_EQUAL(36, readAll(body).size());
  TEST_ASSERT_TRUE(body.complete());
}

void test_chunked_extensions_and_trailer() {
  StringSource src("5;name=value\r\nhello\r\n0\r\nX-Trailer: 1\r\n\r\nNEXT");
  BodyFramer<StringSource> body(src, BodyFraming::kChunked, -1);
  TEST_ASSERT_EQUAL_STRING("hello", readAll(body).c_str());
  TEST_ASSERT_TRUE(body.complete());
  // The source is left exactly at the next message.
  TEST_ASSERT_EQUAL('N', src.read());
}

void test_chunked_bare_lf_tolerated() {
  StringSource src("3\nabc\n0\n\n");
  BodyFramer<StringSource> body(src, BodyFraming::kChunked, -1);
  TEST_ASSERT_EQUAL_STRING("abc", readAll(body).c_str());
  TEST_ASSERT_TRUE(body.complete());
}

void test_chunked_malformed_size() {
  StringSource src("zz\r\nhello\r\n0\r\n\r\n");
  BodyFramer<StringSource> body(src, BodyFraming::kChunked, -1);
  TEST_ASSERT_EQUAL(-1, body.read());
  TEST_ASSERT_TRUE(body.framingError());
}

void test_chunked_size_too_long() {
  StringSource src("123456789\r\n");
  BodyFramer<StringSource> body(src, BodyFraming::kChunked, -1);
  TEST_ASSERT_EQUAL(-1, body.read());
  TEST_ASSERT_TRUE(body.framingError());
}

void test_chunked_missing_crlf_after_data() {
  StringSource src("3\r\nabcX\r\n0\r\n\r\n");
  BodyFramer<StringSource> body(src, BodyFraming::kChunked, -1);
  TEST_ASSERT_EQUAL_STRING("abc", readAll(body).c_str());
  TEST_ASSERT_TRUE(body.framingError());
}

void test_chunked_truncated() {
  StringSource src("5\r\nhel");
  BodyFramer<StringSource> body(src, BodyFraming::kChunked, -1);
  TEST_ASSERT_EQUAL_STRING("hel", readAll(body).c_str());
  TEST_ASSERT_TRUE(body.truncated());
  TEST_ASSERT_FALSE(body.framingError());
}

void test_drain_consumes_terminator() {
  // A JSON parser stops at the closing brace; drain() reads the rest.
  StringSource src("2\r\n{}\r\n0\r\n\r\nNEXT");
  BodyFramer<StringSource> body(src, BodyFraming::kChunked, -1);
  TEST_ASSERT_EQUAL('{', body.read());
  TEST_ASSERT_EQUAL('}', body.read());
  body.drain();
  TEST_ASSERT_TRUE(body.complete());
  TEST_ASSERT_EQUAL('N', src.read());
}

void test_read_bytes() {
  StringSource src("5\r\nhello\r\n0\r\n\r\n");
  BodyFramer<StringSource> body(src, BodyFraming::kChunked, -1);
  char buf[16] = {};
  TEST_ASSERT_EQUAL(3, body.readBytes(buf, 3));
  TEST_ASSERT_EQUAL(0, strncmp(buf, "hel", 3));
  TEST_ASSERT_EQUAL(2, body.readBytes(buf, sizeof(buf)));
  TEST_ASSERT_EQUAL(0, strncmp(buf, "lo", 2));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_identity_with_length);
  RUN_TEST(test_identity_cut_short);
  RUN_TEST(test_identity_until_close);
  RUN_TEST(test_chunked_basic);
  RUN_TEST(test_chunked_hex_sizes_upper_and_lower);
  RUN_TEST(test_chunked_extensions_and_trailer);
  RUN_TEST(test_chunked_bare_lf_tolerated);
  RUN_TEST(test_chunked_malformed_size);
  RUN_TEST(test_chunked_size_too_long);
  RUN_TEST(test_chunked_missing_crlf_after_data);
  RUN_TEST(test_chunked_truncated);
  RUN_TEST(test_drain_consumes_terminator);
  RUN_TEST(test_read_bytes);
  return UNITY_END();
}
