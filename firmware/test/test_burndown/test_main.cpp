// burndown: the office poster's 7-day history, slotted by hour of the weekly
// window and cleared when the window resets.
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
    TEST_ASSERT_EQUAL(166, burndown_slot_of(60));
}

static void test_blank_is_empty(void) {
    for (int s = 0; s < BURNDOWN_SLOTS; s++) TEST_ASSERT_EQUAL_HEX8(BURNDOWN_EMPTY, b.used[s]);
    TEST_ASSERT_EQUAL(-1, burndown_column(&b, 0, 21, 167));
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
    TEST_ASSERT_EQUAL(90, b.used[166]);
    // The window reset: wr jumps back up to a full week (reset moved 7 days on).
    const int f = burndown_sample(&b, T0 + 70, BURNDOWN_WEEK_MINS - 10, 1.0f);
    TEST_ASSERT_TRUE(f & BURNDOWN_SAVE);
    TEST_ASSERT_EQUAL_HEX8(BURNDOWN_EMPTY, b.used[166]);
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

static void test_columns(void) {
    // 21 columns of 8 slots. Slots 0..7 -> column 0, 8..15 -> column 1.
    b.reset_min = T0;
    b.used[2] = 5;
    b.used[6] = 9;
    b.used[17] = 20;
    TEST_ASSERT_EQUAL(9, burndown_column(&b, 0, 21, 100));    // the latest in the column
    TEST_ASSERT_EQUAL(-1, burndown_column(&b, 1, 21, 100));   // a gap stays a gap
    TEST_ASSERT_EQUAL(20, burndown_column(&b, 2, 21, 100));
    TEST_ASSERT_EQUAL(5, burndown_column(&b, 0, 21, 4));      // nothing after "now"
    TEST_ASSERT_EQUAL(-1, burndown_column(&b, 2, 21, 15));    // the future
    TEST_ASSERT_EQUAL(-1, burndown_column(&b, 0, 21, -1));    // now unknown
    TEST_ASSERT_EQUAL(-1, burndown_column(&b, 21, 21, 100));  // out of range
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
    RUN_TEST(test_columns);
    return UNITY_END();
}
