// ADS-B parsing: one adsb.fi "ac" entry -> Aircraft.

#include <unity.h>

#include <ArduinoJson.h>

#include "services/adsb_parse.h"

// The unit under test, built straight into this test binary.
#include "../../src/services/adsb_parse.cpp"

using services::adsb::Aircraft;
using services::adsb::Emergency;
namespace parse = services::adsb::parse;

namespace {

JsonDocument s_doc;

JsonObjectConst json(const char* text) {
  s_doc.clear();
  const DeserializationError err = deserializeJson(s_doc, text);
  TEST_ASSERT_FALSE_MESSAGE(err, err.c_str());
  return s_doc.as<JsonObjectConst>();
}

Aircraft parsed(const char* text) {
  Aircraft ac = {};
  TEST_ASSERT_TRUE(parse::aircraft(json(text), &ac));
  return ac;
}

}  // namespace

void setUp() {}
void tearDown() {}

void test_full_entry() {
  const Aircraft ac = parsed(
      R"({"hex":"c0233f","flight":"ACA123  ","t":"B38M","desc":"BOEING 737 MAX 8",)"
      R"("lat":45.5,"lon":-73.6,"true_heading":91.5,"track":95,"gs":250.5,)"
      R"("alt_baro":12000,"baro_rate":-1500,"squawk":"2345","seen_pos":2.5})");
  TEST_ASSERT_EQUAL_STRING("c0233f", ac.hex);
  TEST_ASSERT_EQUAL_STRING("ACA123", ac.callsign);  // trailing spaces trimmed
  TEST_ASSERT_EQUAL_STRING("B38M", ac.type);
  TEST_ASSERT_EQUAL_STRING("BOEING 737 MAX 8", ac.desc);
  TEST_ASSERT_EQUAL_FLOAT(45.5f, ac.lat);
  TEST_ASSERT_EQUAL_FLOAT(-73.6f, ac.lon);
  TEST_ASSERT_EQUAL_FLOAT(91.5f, ac.nose_deg);
  TEST_ASSERT_EQUAL_FLOAT(95.0f, ac.track_deg);
  TEST_ASSERT_EQUAL_FLOAT(250.5f, ac.gs_knots);
  TEST_ASSERT_EQUAL(2500, ac.pos_age_ms);
  TEST_ASSERT_EQUAL_STRING("12000 ft", ac.alt);
  TEST_ASSERT_EQUAL(12000, ac.alt_ft);
  TEST_ASSERT_TRUE(ac.has_alt);
  TEST_ASSERT_FALSE(ac.on_ground);
  TEST_ASSERT_TRUE(ac.has_vrate);
  TEST_ASSERT_EQUAL(-1500, ac.vrate_fpm);
  TEST_ASSERT_EQUAL_STRING("2345", ac.squawk);
  TEST_ASSERT_EQUAL(Emergency::None, ac.emergency);
}

void test_no_position_rejected() {
  Aircraft ac = {};
  TEST_ASSERT_FALSE(parse::aircraft(json(R"({"hex":"abc123","lon":-73.6})"), &ac));
  TEST_ASSERT_FALSE(parse::aircraft(json(R"({"hex":"abc123","lat":45.5})"), &ac));
}

void test_callsign_falls_back_to_hex() {
  const Aircraft ac = parsed(R"({"hex":"c0ffee","lat":1,"lon":2})");
  TEST_ASSERT_EQUAL_STRING("c0ffee", ac.callsign);
  TEST_ASSERT_FALSE(services::adsb::hasFlightNumber(ac));
}

void test_long_strings_cut_to_fit() {
  const Aircraft ac = parsed(R"({"hex":"a","flight":"ABCDEFGHIJKL","t":"ABCDEFG","lat":1,"lon":2})");
  TEST_ASSERT_EQUAL_STRING("ABCDEFGH", ac.callsign);  // 8 chars + NUL
  TEST_ASSERT_EQUAL_STRING("ABCD", ac.type);          // 4 chars + NUL
}

void test_ground() {
  const Aircraft ac = parsed(R"({"hex":"a","lat":1,"lon":2,"alt_baro":"ground"})");
  TEST_ASSERT_TRUE(ac.on_ground);
  TEST_ASSERT_FALSE(ac.has_alt);
  TEST_ASSERT_EQUAL_STRING("GND", ac.alt);
  TEST_ASSERT_TRUE(parse::isOnGround(json(R"({"alt_baro":"ground"})")));
  TEST_ASSERT_FALSE(parse::isOnGround(json(R"({"alt_baro":100})")));
}

void test_altitude_geom_fallback_and_unknown() {
  Aircraft ac = parsed(R"({"hex":"a","lat":1,"lon":2,"alt_geom":3500.6})");
  TEST_ASSERT_TRUE(ac.has_alt);
  TEST_ASSERT_EQUAL(3501, ac.alt_ft);
  ac = parsed(R"({"hex":"a","lat":1,"lon":2})");
  TEST_ASSERT_FALSE(ac.has_alt);
  TEST_ASSERT_FALSE(ac.on_ground);
  TEST_ASSERT_EQUAL_STRING("", ac.alt);
}

void test_vertical_rate_fallback_and_clamp() {
  Aircraft ac = parsed(R"({"hex":"a","lat":1,"lon":2,"geom_rate":640})");
  TEST_ASSERT_TRUE(ac.has_vrate);
  TEST_ASSERT_EQUAL(640, ac.vrate_fpm);
  ac = parsed(R"({"hex":"a","lat":1,"lon":2,"baro_rate":99999})");
  TEST_ASSERT_EQUAL(32000, ac.vrate_fpm);
  ac = parsed(R"({"hex":"a","lat":1,"lon":2,"baro_rate":-99999})");
  TEST_ASSERT_EQUAL(-32000, ac.vrate_fpm);
  ac = parsed(R"({"hex":"a","lat":1,"lon":2})");
  TEST_ASSERT_FALSE(ac.has_vrate);
  TEST_ASSERT_EQUAL(0, ac.vrate_fpm);
}

void test_heading_fallbacks() {
  TEST_ASSERT_EQUAL_FLOAT(10.0f, parse::noseHeading(json(R"({"mag_heading":10,"track":20})")));
  TEST_ASSERT_EQUAL_FLOAT(20.0f, parse::noseHeading(json(R"({"track":20,"dir":30})")));
  TEST_ASSERT_EQUAL_FLOAT(30.0f, parse::noseHeading(json(R"({"dir":30})")));
  TEST_ASSERT_EQUAL_FLOAT(0.0f, parse::noseHeading(json(R"({})")));
  TEST_ASSERT_EQUAL_FLOAT(20.0f, parse::trackHeading(json(R"({"true_heading":10,"track":20})")));
  TEST_ASSERT_EQUAL_FLOAT(10.0f, parse::trackHeading(json(R"({"true_heading":10})")));
}

void test_speed_fallbacks() {
  TEST_ASSERT_EQUAL_FLOAT(300.0f, parse::groundSpeed(json(R"({"tas":300,"ias":200})")));
  TEST_ASSERT_EQUAL_FLOAT(200.0f, parse::groundSpeed(json(R"({"ias":200})")));
  TEST_ASSERT_EQUAL_FLOAT(0.0f, parse::groundSpeed(json(R"({})")));
}

void test_position_age_clamped() {
  TEST_ASSERT_EQUAL(0, parse::positionAgeMs(json(R"({"seen_pos":-3})")));
  TEST_ASSERT_EQUAL(30000, parse::positionAgeMs(json(R"({"seen_pos":120})")));
  TEST_ASSERT_EQUAL(0, parse::positionAgeMs(json(R"({})")));
}

void test_emergency_squawks() {
  TEST_ASSERT_EQUAL(Emergency::General, parsed(R"({"hex":"a","lat":1,"lon":2,"squawk":"7700"})").emergency);
  TEST_ASSERT_EQUAL(Emergency::Radio, parsed(R"({"hex":"a","lat":1,"lon":2,"squawk":"7600"})").emergency);
  TEST_ASSERT_EQUAL(Emergency::Hijack, parsed(R"({"hex":"a","lat":1,"lon":2,"squawk":"7500"})").emergency);
  TEST_ASSERT_EQUAL(Emergency::None, parsed(R"({"hex":"a","lat":1,"lon":2,"squawk":"1200"})").emergency);
}

void test_emergency_field() {
  const struct {
    const char* value;
    Emergency expected;
  } kCases[] = {
      {"nordo", Emergency::Radio},       {"unlawful", Emergency::Hijack},
      {"general", Emergency::General},   {"lifeguard", Emergency::General},
      {"minfuel", Emergency::General},   {"downed", Emergency::General},
      {"none", Emergency::None},         {"reserved", Emergency::None},
  };
  for (const auto& c : kCases) {
    char text[96];
    snprintf(text, sizeof(text), R"({"hex":"a","lat":1,"lon":2,"emergency":"%s"})", c.value);
    TEST_ASSERT_EQUAL_MESSAGE(c.expected, parsed(text).emergency, c.value);
  }
}

void test_squawk_wins_over_field() {
  const Aircraft ac = parsed(R"({"hex":"a","lat":1,"lon":2,"squawk":"7500","emergency":"nordo"})");
  TEST_ASSERT_EQUAL(Emergency::Hijack, ac.emergency);
}

void test_emergency_labels() {
  TEST_ASSERT_EQUAL_STRING("EMER", services::adsb::emergencyShort(Emergency::General));
  TEST_ASSERT_EQUAL_STRING("HIJACK", services::adsb::emergencyLong(Emergency::Hijack));
  TEST_ASSERT_EQUAL_STRING("", services::adsb::emergencyShort(Emergency::None));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_full_entry);
  RUN_TEST(test_no_position_rejected);
  RUN_TEST(test_callsign_falls_back_to_hex);
  RUN_TEST(test_long_strings_cut_to_fit);
  RUN_TEST(test_ground);
  RUN_TEST(test_altitude_geom_fallback_and_unknown);
  RUN_TEST(test_vertical_rate_fallback_and_clamp);
  RUN_TEST(test_heading_fallbacks);
  RUN_TEST(test_speed_fallbacks);
  RUN_TEST(test_position_age_clamped);
  RUN_TEST(test_emergency_squawks);
  RUN_TEST(test_emergency_field);
  RUN_TEST(test_squawk_wins_over_field);
  RUN_TEST(test_emergency_labels);
  return UNITY_END();
}
