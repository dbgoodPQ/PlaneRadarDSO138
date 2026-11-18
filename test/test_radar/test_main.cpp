// Radar helpers: altitude colour bands, range presets and labels.

#include <unity.h>

#include "services/settings.h"
#include "ui/radar_range.h"
#include "ui/radar_theme.h"

// The unit under test, built straight into this test binary.
#include "../../src/ui/radar_range.cpp"

namespace radar = ui::radar;

// --- services::settings stand-in (radar_range.cpp reads and writes it) ---
namespace {
uint8_t s_range_index = 0;
bool s_use_miles = false;
}  // namespace

namespace services::settings {
uint8_t rangeIndex() { return s_range_index; }
void setRangeIndex(uint8_t index, bool) { s_range_index = index; }
bool useMiles() { return s_use_miles; }
bool showRunways() { return true; }
}  // namespace services::settings

void setUp() {
  s_range_index = 0;
  s_use_miles = false;
}
void tearDown() {}

void test_altitude_bands() {
  TEST_ASSERT_EQUAL_HEX32(radar::kColorAltGround, radar::altitudeColor(true, true, 35000));
  TEST_ASSERT_EQUAL_HEX32(radar::kColorAltUnknown, radar::altitudeColor(false, false, 0));
  TEST_ASSERT_EQUAL_HEX32(radar::kColorAltLow, radar::altitudeColor(false, true, -200));
  TEST_ASSERT_EQUAL_HEX32(radar::kColorAltLow, radar::altitudeColor(false, true, radar::kAltLowFt - 1));
  TEST_ASSERT_EQUAL_HEX32(radar::kColorAltMid, radar::altitudeColor(false, true, radar::kAltLowFt));
  TEST_ASSERT_EQUAL_HEX32(radar::kColorAltMid, radar::altitudeColor(false, true, radar::kAltMidFt - 1));
  TEST_ASSERT_EQUAL_HEX32(radar::kColorAltHigh, radar::altitudeColor(false, true, radar::kAltMidFt));
  TEST_ASSERT_EQUAL_HEX32(radar::kColorAltHigh, radar::altitudeColor(false, true, radar::kAltHighFt - 1));
  TEST_ASSERT_EQUAL_HEX32(radar::kColorAltCruise, radar::altitudeColor(false, true, radar::kAltHighFt));
}

void test_band_edges_match_legend() {
  // The legend text in the detail panel says 3k / 10k / 25k ft.
  TEST_ASSERT_EQUAL(3000, radar::kAltLowFt);
  TEST_ASSERT_EQUAL(10000, radar::kAltMidFt);
  TEST_ASSERT_EQUAL(25000, radar::kAltHighFt);
}

void test_presets_ascending_with_outer_ratio() {
  TEST_ASSERT_EQUAL(6, radar::kRangePresetCount);
  for (size_t i = 0; i < radar::kRangePresetCount; ++i) {
    const radar::RangePreset& p = radar::kRangePresets[i];
    TEST_ASSERT_EQUAL_FLOAT(5.0f * static_cast<float>(i + 1), p.ring3_km);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, p.ring3_km * 4.0f / 3.0f, p.outer_km);
  }
}

void test_range_next_wraps() {
  for (size_t i = 1; i < radar::kRangePresetCount; ++i) {
    radar::rangeNext();
    TEST_ASSERT_EQUAL(i, s_range_index);
  }
  radar::rangeNext();
  TEST_ASSERT_EQUAL(0, s_range_index);
}

void test_fetch_radius_reaches_screen_edge() {
  s_range_index = 1;  // 10 km
  const float expected = radar::kRangePresets[1].outer_km *
                         static_cast<float>(radar::kBeyondRingRimRadiusPx) /
                         static_cast<float>(radar::kGridOuterRadius);
  TEST_ASSERT_FLOAT_WITHIN(0.001f, expected, radar::fetchRadiusKm());
  TEST_ASSERT_TRUE(radar::fetchRadiusKm() > radar::kRangePresets[1].outer_km);
}

void test_ring3_labels_km_and_miles() {
  const char* const kKm[] = {"5km", "10km", "15km", "20km", "25km", "30km"};
  const char* const kMi[] = {"3mi", "6mi", "9mi", "12mi", "16mi", "19mi"};
  char buf[12];
  for (size_t i = 0; i < radar::kRangePresetCount; ++i) {
    radar::formatRing3Label(buf, sizeof(buf), radar::kRangePresets[i].ring3_km, false);
    TEST_ASSERT_EQUAL_STRING(kKm[i], buf);
    radar::formatRing3Label(buf, sizeof(buf), radar::kRangePresets[i].ring3_km, true);
    TEST_ASSERT_EQUAL_STRING(kMi[i], buf);
  }
}

void test_current_label_follows_settings() {
  char buf[12];
  s_range_index = 2;
  s_use_miles = true;
  radar::formatCurrentRing3Label(buf, sizeof(buf));
  TEST_ASSERT_EQUAL_STRING("9mi", buf);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_altitude_bands);
  RUN_TEST(test_band_edges_match_legend);
  RUN_TEST(test_presets_ascending_with_outer_ratio);
  RUN_TEST(test_range_next_wraps);
  RUN_TEST(test_fetch_radius_reaches_screen_edge);
  RUN_TEST(test_ring3_labels_km_and_miles);
  RUN_TEST(test_current_label_follows_settings);
  return UNITY_END();
}
