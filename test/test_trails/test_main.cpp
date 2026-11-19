// Trail store: fixes per aircraft, dedupe, length cap, expiry, slot limit.

#include <unity.h>

#include <cstdio>

#include "ui/trails.h"

// The unit under test, built straight into this test binary.
#include "../../src/ui/trails.cpp"

namespace trails = ui::trails;

namespace {

unsigned long s_base = 0;
uint32_t s_now = 1000;

/** Start a new fetch, `dt_ms` after the previous one. */
bool fetch(uint32_t dt_ms = 5000) {
  s_now += dt_ms;
  return trails::beginRecord(++s_base, s_now);
}

}  // namespace

void setUp() {
  trails::clear();
  s_base = 0;
  s_now = 1000;
}
void tearDown() {}

void test_records_fix_in_10m_units() {
  TEST_ASSERT_TRUE(fetch());
  trails::add("abc123", 1.234f, -0.5f);
  const trails::Trail* t = trails::find("abc123");
  TEST_ASSERT_NOT_NULL(t);
  TEST_ASSERT_EQUAL(1, t->count);
  TEST_ASSERT_EQUAL(123, t->points[0].e);
  TEST_ASSERT_EQUAL(-50, t->points[0].n);
}

void test_unknown_and_empty_hex() {
  TEST_ASSERT_NULL(trails::find("nope"));
  TEST_ASSERT_NULL(trails::find(""));
  TEST_ASSERT_NULL(trails::find(nullptr));
  fetch();
  trails::add("", 1.0f, 1.0f);
  trails::add(nullptr, 1.0f, 1.0f);
  TEST_ASSERT_NULL(trails::find(""));
}

void test_same_fetch_records_once() {
  TEST_ASSERT_TRUE(trails::beginRecord(42, 1000));
  TEST_ASSERT_FALSE(trails::beginRecord(42, 2000));  // same fetch again
  TEST_ASSERT_FALSE(trails::beginRecord(0, 3000));   // no fetch yet
}

void test_small_moves_skipped() {
  fetch();
  trails::add("a", 1.00f, 1.00f);
  fetch();
  trails::add("a", 1.02f, 1.01f);  // 20 m: same position
  TEST_ASSERT_EQUAL(1, trails::find("a")->count);
  fetch();
  trails::add("a", 1.05f, 1.00f);  // 50 m: new fix
  TEST_ASSERT_EQUAL(2, trails::find("a")->count);
}

void test_version_bumps_on_new_fix_only() {
  fetch();
  trails::add("a", 0.0f, 0.0f);
  const uint8_t v1 = trails::find("a")->version;
  fetch();
  trails::add("a", 0.0f, 0.0f);
  TEST_ASSERT_EQUAL(v1, trails::find("a")->version);
  fetch();
  trails::add("a", 1.0f, 0.0f);
  TEST_ASSERT_EQUAL(static_cast<uint8_t>(v1 + 1), trails::find("a")->version);
}

void test_keeps_newest_points() {
  for (int i = 0; i < trails::kMaxPoints + 3; ++i) {
    fetch();
    trails::add("a", static_cast<float>(i), 0.0f);
  }
  const trails::Trail* t = trails::find("a");
  TEST_ASSERT_EQUAL(trails::kMaxPoints, t->count);
  TEST_ASSERT_EQUAL(300, t->points[0].e);  // oldest kept: i = 3 (3 km)
  TEST_ASSERT_EQUAL((trails::kMaxPoints + 2) * 100, t->points[trails::kMaxPoints - 1].e);
}

void test_expires_after_60s_unseen() {
  fetch();
  trails::add("a", 1.0f, 1.0f);
  trails::add("b", 2.0f, 2.0f);
  fetch(trails::kExpireMs / 2);
  trails::add("b", 3.0f, 3.0f);  // b seen again, a not
  fetch(trails::kExpireMs / 2 + 1);
  TEST_ASSERT_NULL(trails::find("a"));
  TEST_ASSERT_NOT_NULL(trails::find("b"));
}

void test_far_offsets_saturate() {
  fetch();
  trails::add("a", 1000.0f, -1000.0f);  // beyond ±327 km
  const trails::Trail* t = trails::find("a");
  TEST_ASSERT_EQUAL(32767, t->points[0].e);
  TEST_ASSERT_EQUAL(-32767, t->points[0].n);
}

void test_slot_limit_keeps_current_planes() {
  char hex[8];
  fetch();
  for (int i = 0; i < trails::kMaxTrails; ++i) {
    snprintf(hex, sizeof(hex), "p%02d", i);
    trails::add(hex, 1.0f, 1.0f);
  }
  // One more in the same fetch: no slot is taken from a plane seen now.
  trails::add("extra", 1.0f, 1.0f);
  TEST_ASSERT_NULL(trails::find("extra"));
  TEST_ASSERT_NOT_NULL(trails::find("p00"));

  // Next fetch: p00 is gone, the rest are still here. The newcomer takes the
  // slot of the plane seen longest ago (all equal: the first one, p00).
  fetch();
  for (int i = 1; i < trails::kMaxTrails; ++i) {
    snprintf(hex, sizeof(hex), "p%02d", i);
    trails::add(hex, 2.0f, 2.0f);
  }
  trails::add("extra", 1.0f, 1.0f);
  TEST_ASSERT_NOT_NULL(trails::find("extra"));
  TEST_ASSERT_NULL(trails::find("p00"));
  TEST_ASSERT_NOT_NULL(trails::find("p01"));
}

void test_clear_forgets_everything() {
  fetch();
  trails::add("a", 1.0f, 1.0f);
  trails::clear();
  TEST_ASSERT_NULL(trails::find("a"));
  // The same fetch can be recorded again after a clear (new centre).
  TEST_ASSERT_TRUE(trails::beginRecord(s_base, s_now));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_records_fix_in_10m_units);
  RUN_TEST(test_unknown_and_empty_hex);
  RUN_TEST(test_same_fetch_records_once);
  RUN_TEST(test_small_moves_skipped);
  RUN_TEST(test_version_bumps_on_new_fix_only);
  RUN_TEST(test_keeps_newest_points);
  RUN_TEST(test_expires_after_60s_unseen);
  RUN_TEST(test_far_offsets_saturate);
  RUN_TEST(test_slot_limit_keeps_current_planes);
  RUN_TEST(test_clear_forgets_everything);
  return UNITY_END();
}
