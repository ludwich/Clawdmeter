// burndown: the office poster's 7-day history, slotted by hour of the weekly
// window and cleared when the window resets; the pace, verdict, burn-up and
// projection drawn from it.
//   pio test -d firmware -e native -f test_burndown
#include <unity.h>
#include <string.h>
#include "burndown.h"

static Burndown b;
static const int32_t T0 = 29000000;   // some wall-clock minute (the app's `t` / 60)

void setUp(void) {
    memset(&b, 0, sizeof(b));
    burndown_sanitize(&b);              // a blank NVS blob: cleared, nothing saved yet
}
void tearDown(void) {}

static void test_slot_of(void) {
    TEST_ASSERT_EQUAL(-1, burndown_slot_of(-1));
    TEST_ASSERT_EQUAL(0, burndown_slot_of(BURNDOWN_WEEK_MINS));       // just started
    TEST_ASSERT_EQUAL(0, burndown_slot_of(BURNDOWN_WEEK_MINS + 30));  // skew past it
    TEST_ASSERT_EQUAL(0, burndown_slot_of(BURNDOWN_WEEK_MINS - 59));
    TEST_ASSERT_EQUAL(1, burndown_slot_of(BURNDOWN_WEEK_MINS - 60));
    TEST_ASSERT_EQUAL(167, burndown_slot_of(0));                       // reset now
    TEST_ASSERT_EQUAL(167, burndown_slot_of(59));
    TEST_ASSERT_EQUAL(167, burndown_slot_of(60));                      // 10020 min in: still the last hour
    TEST_ASSERT_EQUAL(166, burndown_slot_of(61));
}

static void test_blank_is_empty(void) {
    for (int s = 0; s < BURNDOWN_SLOTS; s++) TEST_ASSERT_EQUAL_HEX8(BURNDOWN_EMPTY, b.used[s]);
}

static void test_records_and_saves_once_per_hour(void) {
    const int wr = 3 * 24 * 60;                               // 3 days left: slot 96
    int f = burndown_sample(&b, T0, wr, 41.6f);
    TEST_ASSERT_TRUE(f & BURNDOWN_CHANGED);
    TEST_ASSERT_TRUE(f & BURNDOWN_SAVE);                      // a new window
    TEST_ASSERT_EQUAL(42, b.used[96]);
    // Ten minutes later, same hour: updated in RAM, not saved again.
    f = burndown_sample(&b, T0 + 10, wr - 10, 43.0f);
    TEST_ASSERT_TRUE(f & BURNDOWN_CHANGED);
    TEST_ASSERT_FALSE(f & BURNDOWN_SAVE);
    TEST_ASSERT_EQUAL(43, b.used[96]);
    // Same value: nothing at all.
    TEST_ASSERT_EQUAL(0, burndown_sample(&b, T0 + 11, wr - 11, 43.0f));
    // The next hour: saved once.
    f = burndown_sample(&b, T0 + 60, wr - 60, 44.0f);
    TEST_ASSERT_TRUE(f & BURNDOWN_SAVE);
    TEST_ASSERT_EQUAL(44, b.used[97]);
    TEST_ASSERT_EQUAL(43, b.used[96]);
    TEST_ASSERT_FALSE(burndown_sample(&b, T0 + 70, wr - 70, 45.0f) & BURNDOWN_SAVE);
}

static void test_no_clock_or_unknown_records_nothing(void) {
    TEST_ASSERT_EQUAL(0, burndown_sample(&b, 0, 600, 10.0f));
    TEST_ASSERT_EQUAL(0, burndown_sample(&b, T0, -1, 10.0f));
    for (int s = 0; s < BURNDOWN_SLOTS; s++) TEST_ASSERT_EQUAL_HEX8(BURNDOWN_EMPTY, b.used[s]);
}

static void test_reset_clears(void) {
    burndown_sample(&b, T0, 60, 90.0f);                       // the last hour of a week
    TEST_ASSERT_EQUAL(90, b.used[167]);
    // The window reset: wr jumps back up to a full week (reset moved 7 days on).
    const int f = burndown_sample(&b, T0 + 70, BURNDOWN_WEEK_MINS - 10, 1.0f);
    TEST_ASSERT_TRUE(f & BURNDOWN_SAVE);
    TEST_ASSERT_EQUAL_HEX8(BURNDOWN_EMPTY, b.used[167]);
    TEST_ASSERT_EQUAL(1, b.used[0]);
}

static void test_jitter_and_dst_keep_the_window(void) {
    burndown_sample(&b, T0, 5000, 30.0f);
    burndown_sample(&b, T0 + 61, 5000 - 60, 31.0f);           // a minute of jitter
    burndown_sample(&b, T0 + 180, 5000 - 120, 32.0f);         // the local clock jumped an hour
    TEST_ASSERT_EQUAL(30, b.used[burndown_slot_of(5000)]);
    TEST_ASSERT_EQUAL(31, b.used[burndown_slot_of(4940)]);
    TEST_ASSERT_EQUAL(32, b.used[burndown_slot_of(4880)]);
}

static void test_big_drop_is_a_new_window(void) {
    burndown_sample(&b, T0, 5000, 60.0f);
    burndown_sample(&b, T0 + 60, 4940, 55.0f);                // a small dip: kept
    TEST_ASSERT_EQUAL(60, b.used[burndown_slot_of(5000)]);
    burndown_sample(&b, T0 + 120, 4880, 5.0f);                // reset early: cleared
    TEST_ASSERT_EQUAL_HEX8(BURNDOWN_EMPTY, b.used[burndown_slot_of(5000)]);
    TEST_ASSERT_EQUAL(5, b.used[burndown_slot_of(4880)]);
}

static void test_reboot_into_a_later_window_clears(void) {
    burndown_sample(&b, T0, 5000, 70.0f);
    burndown_sanitize(&b);                                    // as if loaded from NVS
    TEST_ASSERT_EQUAL(-1, b.saved_slot);
    // Booted 9 days later: a different reset.
    burndown_sample(&b, T0 + 9 * 1440, 4000, 12.0f);
    TEST_ASSERT_EQUAL_HEX8(BURNDOWN_EMPTY, b.used[burndown_slot_of(5000)]);
    TEST_ASSERT_EQUAL(12, b.used[burndown_slot_of(4000)]);
}

static void test_sanitize_drops_junk(void) {
    b.reset_min = T0;
    b.used[3] = 140;
    b.used[4] = 50;
    burndown_sanitize(&b);
    TEST_ASSERT_EQUAL_HEX8(BURNDOWN_EMPTY, b.used[3]);
    TEST_ASSERT_EQUAL(50, b.used[4]);
}

// ─── Pace, verdict, projection ───────────────────────────────────────────────
static WeekPace p;
static char l1[8], l2[8];

static void verdict_is(const char* a, const char* b) {
    burndown_verdict(&p, l1, l2);
    TEST_ASSERT_EQUAL_STRING(a, l1);
    TEST_ASSERT_EQUAL_STRING(b, l2);
}

static void test_elapsed(void) {
    TEST_ASSERT_EQUAL(-1, burndown_elapsed(-1));
    TEST_ASSERT_EQUAL(0, burndown_elapsed(BURNDOWN_WEEK_MINS));
    TEST_ASSERT_EQUAL(0, burndown_elapsed(BURNDOWN_WEEK_MINS + 20));   // skew: the start
    TEST_ASSERT_EQUAL(BURNDOWN_WEEK_MINS, burndown_elapsed(0));
    TEST_ASSERT_EQUAL(5080, burndown_elapsed(5000));
}

static void test_pace_unknown(void) {
    burndown_pace(&p, -1, 5000);
    TEST_ASSERT_EQUAL(WEEK_UNKNOWN, p.level);
    TEST_ASSERT_EQUAL(-1, p.used);
    verdict_is("", "");
    burndown_pace(&p, 30, -1);                                // no window (or spend-capped)
    TEST_ASSERT_EQUAL(WEEK_UNKNOWN, p.level);
    TEST_ASSERT_EQUAL(-1, burndown_used_at(NULL, &p, 0));
    TEST_ASSERT_EQUAL(-1, burndown_projected_at(&p, 6000));
}

static void test_pace_behind(void) {
    burndown_pace(&p, 8, 8000);                               // 20.6 % of the week gone
    TEST_ASSERT_EQUAL(WEEK_ON, p.level);
    TEST_ASSERT_EQUAL(2080, p.elapsed);
    TEST_ASSERT_EQUAL(-13, p.delta);
    TEST_ASSERT_EQUAL(-1, p.out_in);
    verdict_is("-13", "BEHIND");
}

static void test_pace_on(void) {
    burndown_pace(&p, 50, 5040);                              // exactly half way
    TEST_ASSERT_EQUAL(WEEK_ON, p.level);
    TEST_ASSERT_EQUAL(0, p.delta);
    verdict_is("ON", "PACE");
    burndown_pace(&p, 51, 5040);                              // a point ahead: still on pace
    TEST_ASSERT_EQUAL(WEEK_ON, p.level);
    verdict_is("ON", "PACE");
}

static void test_pace_ahead_runs_out_late(void) {
    // 83 % with 80.2 % gone: the average rate hits 100 % ~6 h before the reset.
    burndown_pace(&p, 83, 2000);
    TEST_ASSERT_EQUAL(WEEK_AHEAD, p.level);
    TEST_ASSERT_EQUAL(3, p.delta);
    TEST_ASSERT_EQUAL(9734 - 8080, p.out_in);
    verdict_is("+3", "AHEAD");
}

static void test_pace_out_in_days(void) {
    // 62 % half way: at the average rate, out in 2.2 days, 31 h before the reset.
    burndown_pace(&p, 62, 5000);
    TEST_ASSERT_EQUAL(WEEK_OUT, p.level);
    TEST_ASSERT_EQUAL(12, p.delta);
    TEST_ASSERT_EQUAL(8193 - 5080, p.out_in);
    verdict_is("OUT", "2D");
}

static void test_pace_out_in_hours(void) {
    burndown_pace(&p, 91, 3000);
    TEST_ASSERT_EQUAL(WEEK_OUT, p.level);
    TEST_ASSERT_EQUAL(21, p.delta);
    TEST_ASSERT_EQUAL(700, p.out_in);
    verdict_is("OUT", "11H");
    burndown_pace(&p, 99, 5080);                              // 5000 min in: out in 50 min
    TEST_ASSERT_EQUAL(50, p.out_in);
    verdict_is("OUT", "<1H");
}

static void test_pace_limit(void) {
    burndown_pace(&p, 100, 3000);
    TEST_ASSERT_EQUAL(WEEK_OUT, p.level);
    TEST_ASSERT_EQUAL(0, p.out_in);
    verdict_is("OUT", "NOW");
    burndown_pace(&p, 130, 3000);                             // clamped
    TEST_ASSERT_EQUAL(100, p.used);
}

static void test_pace_red_from_90_even_behind(void) {
    burndown_pace(&p, 92, 300);                               // 97 % of the week gone
    TEST_ASSERT_EQUAL(WEEK_OUT, p.level);
    TEST_ASSERT_EQUAL(-5, p.delta);
    TEST_ASSERT_EQUAL(-1, p.out_in);
    verdict_is("-5", "BEHIND");
}

static void test_no_projection_in_the_first_hours(void) {
    burndown_pace(&p, 10, BURNDOWN_WEEK_MINS - 600);          // 10 h in
    TEST_ASSERT_EQUAL(WEEK_AHEAD, p.level);
    TEST_ASSERT_EQUAL(-1, p.out_in);
    TEST_ASSERT_EQUAL(-1, burndown_projected_at(&p, 1000));
    verdict_is("+4", "AHEAD");
}

static void test_used_at_without_history(void) {
    burndown_pace(&p, 30, 5000);                              // 5080 min in
    TEST_ASSERT_EQUAL(0, burndown_used_at(NULL, &p, 0));      // a window starts at 0
    TEST_ASSERT_EQUAL(15, burndown_used_at(NULL, &p, 2540));
    TEST_ASSERT_EQUAL(30, burndown_used_at(NULL, &p, 5080));
    TEST_ASSERT_EQUAL(-1, burndown_used_at(NULL, &p, 5081));  // the future
    TEST_ASSERT_EQUAL(-1, burndown_used_at(NULL, &p, -1));
    TEST_ASSERT_EQUAL(15, burndown_used_at(&b, &p, 2540));    // a blank history: the same
}

static void test_used_at_joins_real_points(void) {
    b.reset_min = T0;
    b.used[10] = 5;                                           // at 630 min
    b.used[20] = 12;                                          // at 1230 min
    b.used[84] = 60;                                          // the current hour: not a point
    burndown_pace(&p, 30, 5000);                              // now = 5080 min, slot 84
    TEST_ASSERT_EQUAL(5, burndown_used_at(&b, &p, 630));
    TEST_ASSERT_EQUAL(3, burndown_used_at(&b, &p, 315));      // 2.5 from the start, rounded
    TEST_ASSERT_EQUAL(9, burndown_used_at(&b, &p, 930));      // 8.5 between the samples
    TEST_ASSERT_EQUAL(21, burndown_used_at(&b, &p, 3155));    // across the gap to now
    TEST_ASSERT_EQUAL(30, burndown_used_at(&b, &p, 5075));
}

static void test_used_at_follows_a_dip(void) {
    b.reset_min = T0;
    b.used[30] = 40;                                          // at 1830 min
    burndown_pace(&p, 30, 5000);
    TEST_ASSERT_EQUAL(35, burndown_used_at(&b, &p, 3455));    // half way down to now
}

static void test_projection(void) {
    burndown_pace(&p, 62, 5000);                              // 5080 min in
    TEST_ASSERT_EQUAL(-1, burndown_projected_at(&p, 5079));   // the past isn't projected
    TEST_ASSERT_EQUAL(62, burndown_projected_at(&p, 5080));
    TEST_ASSERT_EQUAL(85, burndown_projected_at(&p, 7000));
    TEST_ASSERT_EQUAL(100, burndown_projected_at(&p, 8193));  // runs out here
    TEST_ASSERT_EQUAL(100, burndown_projected_at(&p, BURNDOWN_WEEK_MINS));   // clipped
    burndown_pace(&p, 8, 8000);
    TEST_ASSERT_EQUAL(39, burndown_projected_at(&p, BURNDOWN_WEEK_MINS));    // ends the week at 39 %
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_slot_of);
    RUN_TEST(test_blank_is_empty);
    RUN_TEST(test_records_and_saves_once_per_hour);
    RUN_TEST(test_no_clock_or_unknown_records_nothing);
    RUN_TEST(test_reset_clears);
    RUN_TEST(test_jitter_and_dst_keep_the_window);
    RUN_TEST(test_big_drop_is_a_new_window);
    RUN_TEST(test_reboot_into_a_later_window_clears);
    RUN_TEST(test_sanitize_drops_junk);
    RUN_TEST(test_elapsed);
    RUN_TEST(test_pace_unknown);
    RUN_TEST(test_pace_behind);
    RUN_TEST(test_pace_on);
    RUN_TEST(test_pace_ahead_runs_out_late);
    RUN_TEST(test_pace_out_in_days);
    RUN_TEST(test_pace_out_in_hours);
    RUN_TEST(test_pace_limit);
    RUN_TEST(test_pace_red_from_90_even_behind);
    RUN_TEST(test_no_projection_in_the_first_hours);
    RUN_TEST(test_used_at_without_history);
    RUN_TEST(test_used_at_joins_real_points);
    RUN_TEST(test_used_at_follows_a_dip);
    RUN_TEST(test_projection);
    return UNITY_END();
}
