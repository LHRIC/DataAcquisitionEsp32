#include "daq.h"
#include "unity.h"

// Optional: per-test setup/teardown hooks
void setUp(void) {}
void tearDown(void) {}

TEST_CASE("daq_add adds two positive ints", "[daq]")
{
    TEST_ASSERT_EQUAL_INT(5, daq_add(2, 3));
}

TEST_CASE("daq_add handles negatives", "[daq]")
{
    TEST_ASSERT_EQUAL_INT(-1, daq_add(2, -3));
}
