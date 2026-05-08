#include "can/can.hpp"
#include "can/gps_time.hpp"
#include "daq_core/buffer_pool.hpp"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "unity.h"
#include <string.h>

extern QueueHandle_t twai_queue;

static TickType_t to_ticks(int ms) { return pdMS_TO_TICKS(ms); }

// Helper to simulate TWAI ISR sending data
static bool mock_twai_send(const uint8_t *data, size_t len)
{
    block_t *blk = NULL;
    if (xQueueReceive(free_queue, &blk, to_ticks(10)) != pdTRUE)
    {
        return false;
    }
    if (len > sizeof(blk->data))
        len = sizeof(blk->data);
    memcpy(blk->data, data, len);
    blk->size = len;
    blk->refcnt = 1;
    return xQueueSend(twai_queue, &blk, to_ticks(10)) == pdTRUE;
}

static void make_gps_mux1_payload(uint8_t *payload, uint32_t seconds_of_day,
                                  bool fix_valid)
{
    memset(payload, 0, 8);
    payload[0] = 0x01; // mux 1 in the low nibble
    payload[4] = seconds_of_day & 0xFF;
    payload[5] = (seconds_of_day >> 8) & 0xFF;
    payload[6] = (seconds_of_day >> 16) & 0xFF;
    if (fix_valid)
    {
        payload[7] |= 0x80;
    }
}

TEST_CASE("gps mux1 payload anchors time of day", "[gps][time]")
{
    gps_time::reset();

    uint8_t payload[8];
    make_gps_mux1_payload(payload, 45296, true); // 12:34:56

    TEST_ASSERT_TRUE(gps_time::update_from_can_payload(payload, sizeof(payload)));
    TEST_ASSERT_TRUE(gps_time::has_anchor());

    uint32_t sod = 0;
    int64_t anchor_us = 0;
    TEST_ASSERT_TRUE(gps_time::get_anchor(&sod, &anchor_us));
    TEST_ASSERT_EQUAL_UINT32(45296, sod);
    TEST_ASSERT_TRUE(anchor_us > 0);
}

TEST_CASE("gps anchor advances with monotonic time", "[gps][time]")
{
    gps_time::reset();

    uint8_t payload[8];
    make_gps_mux1_payload(payload, 86390, true);

    TEST_ASSERT_TRUE(gps_time::update_from_can_payload(payload, sizeof(payload)));

    timeval before = {};
    timeval after = {};
    TEST_ASSERT_TRUE(gps_time::get_timeval(&before));
    vTaskDelay(pdMS_TO_TICKS(20));
    TEST_ASSERT_TRUE(gps_time::get_timeval(&after));

    TEST_ASSERT_TRUE(after.tv_sec >= before.tv_sec);
    if (after.tv_sec == before.tv_sec)
    {
        TEST_ASSERT_TRUE(after.tv_usec > before.tv_usec);
    }
}

TEST_CASE("fanout with multiple messages preserves data integrity",
          "[can][fanout]")
{
    pool_init();
    if (twai_queue == nullptr)
    {
        twai_queue = xQueueCreate(POOL_SIZE, sizeof(block_t *));
    }

    // Send multiple different messages
    uint8_t msg1[8] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08};
    uint8_t msg2[5] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE};
    uint8_t msg3[3] = {0xFF, 0x00, 0x55};

    TEST_ASSERT_TRUE(mock_twai_send(msg1, sizeof(msg1)));
    TEST_ASSERT_TRUE(mock_twai_send(msg2, sizeof(msg2)));
    TEST_ASSERT_TRUE(mock_twai_send(msg3, sizeof(msg3)));

    // Process all messages
    fanout();
    fanout();
    fanout();

    // Verify all three messages are in xbee_queue
    TEST_ASSERT_EQUAL_UINT32(3, uxQueueMessagesWaiting(xbee_queue));

    // Retrieve and verify each message
    block_t *blk1 = NULL, *blk2 = NULL, *blk3 = NULL;
    TEST_ASSERT_EQUAL(pdTRUE, xQueueReceive(xbee_queue, &blk1, to_ticks(10)));
    TEST_ASSERT_EQUAL(pdTRUE, xQueueReceive(xbee_queue, &blk2, to_ticks(10)));
    TEST_ASSERT_EQUAL(pdTRUE, xQueueReceive(xbee_queue, &blk3, to_ticks(10)));

    TEST_ASSERT_EQUAL_UINT32(sizeof(msg1), blk1->size);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(msg1, blk1->data, sizeof(msg1));

    TEST_ASSERT_EQUAL_UINT32(sizeof(msg2), blk2->size);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(msg2, blk2->data, sizeof(msg2));

    TEST_ASSERT_EQUAL_UINT32(sizeof(msg3), blk3->size);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(msg3, blk3->data, sizeof(msg3));

    // Cleanup
    block_release(blk1);
    block_release(blk2);
    block_release(blk3);

    TEST_ASSERT_EQUAL_UINT32(POOL_SIZE, uxQueueMessagesWaiting(free_queue));
}

TEST_CASE("fanout refcnt is correct after successful enqueue", "[can][fanout]")
{
    pool_init();
    if (twai_queue == nullptr)
    {
        twai_queue = xQueueCreate(POOL_SIZE, sizeof(block_t *));
    }

    uint8_t data[4] = {1, 2, 3, 4};
    TEST_ASSERT_TRUE(mock_twai_send(data, sizeof(data)));

    fanout();

    block_t *blk = NULL;
    TEST_ASSERT_EQUAL(pdTRUE, xQueueReceive(xbee_queue, &blk, to_ticks(10)));

    // After fix: refcnt should be exactly 1 (only consumer holds it)
    TEST_ASSERT_EQUAL_UINT32(1, blk->refcnt);

    block_release(blk);
}

TEST_CASE("fanout refcnt prevents premature free", "[can][fanout]")
{
    pool_init();
    if (twai_queue == nullptr)
    {
        twai_queue = xQueueCreate(POOL_SIZE, sizeof(block_t *));
    }

    uint8_t data[6] = {0xDE, 0xAD, 0xBE, 0xEF, 0xCA, 0xFE};
    TEST_ASSERT_TRUE(mock_twai_send(data, sizeof(data)));

    UBaseType_t free_before = uxQueueMessagesWaiting(free_queue);

    fanout();

    // Block should NOT be back in free pool yet
    TEST_ASSERT_EQUAL_UINT32(free_before, uxQueueMessagesWaiting(free_queue));

    // Consumer receives and uses the block
    block_t *blk = NULL;
    TEST_ASSERT_EQUAL(pdTRUE, xQueueReceive(xbee_queue, &blk, to_ticks(10)));

    // Still not in free pool
    TEST_ASSERT_EQUAL_UINT32(free_before, uxQueueMessagesWaiting(free_queue));

    // Only after consumer releases should it return
    block_release(blk);
    TEST_ASSERT_EQUAL_UINT32(free_before + 1,
                             uxQueueMessagesWaiting(free_queue));
}

TEST_CASE("fanout handles max-size CAN frames", "[can][fanout]")
{
    pool_init();
    if (twai_queue == nullptr)
    {
        twai_queue = xQueueCreate(POOL_SIZE, sizeof(block_t *));
    }

    // Standard CAN frames have up to 8 bytes
    uint8_t max_data[8];
    for (int i = 0; i < 8; i++)
    {
        max_data[i] = i * 16 + i;
    }

    TEST_ASSERT_TRUE(mock_twai_send(max_data, sizeof(max_data)));
    fanout();

    block_t *blk = NULL;
    TEST_ASSERT_EQUAL(pdTRUE, xQueueReceive(xbee_queue, &blk, to_ticks(10)));
    TEST_ASSERT_EQUAL_UINT32(8, blk->size);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(max_data, blk->data, sizeof(max_data));

    block_release(blk);
}

TEST_CASE("fanout handles min-size CAN frames", "[can][fanout]")
{
    pool_init();
    if (twai_queue == nullptr)
    {
        twai_queue = xQueueCreate(POOL_SIZE, sizeof(block_t *));
    }

    uint8_t min_data[1] = {0x42};

    TEST_ASSERT_TRUE(mock_twai_send(min_data, sizeof(min_data)));
    fanout();

    block_t *blk = NULL;
    TEST_ASSERT_EQUAL(pdTRUE, xQueueReceive(xbee_queue, &blk, to_ticks(10)));
    TEST_ASSERT_EQUAL_UINT32(1, blk->size);
    TEST_ASSERT_EQUAL_UINT8(0x42, blk->data[0]);

    block_release(blk);
}

TEST_CASE("consecutive fanouts don't leak blocks", "[can][fanout]")
{
    pool_init();
    if (twai_queue == nullptr)
    {
        twai_queue = xQueueCreate(POOL_SIZE, sizeof(block_t *));
    }

    UBaseType_t initial_free = uxQueueMessagesWaiting(free_queue);

    // Send and process 10 messages, consuming each immediately
    for (int i = 0; i < 10; i++)
    {
        uint8_t data[4] = {(uint8_t)i, (uint8_t)(i + 1), (uint8_t)(i + 2),
                           (uint8_t)(i + 3)};
        TEST_ASSERT_TRUE(mock_twai_send(data, sizeof(data)));
        fanout();

        block_t *blk = NULL;
        TEST_ASSERT_EQUAL(pdTRUE, xQueueReceive(xbee_queue, &blk, to_ticks(10)));
        block_release(blk);
    }

    // All blocks should be back in the pool
    TEST_ASSERT_EQUAL_UINT32(initial_free, uxQueueMessagesWaiting(free_queue));
}

TEST_CASE("fanout with empty xbee queue still drops correctly", "[can][fanout]")
{
    pool_init();
    if (twai_queue == nullptr)
    {
        twai_queue = xQueueCreate(POOL_SIZE, sizeof(block_t *));
    }

    // Don't send any data - twai_queue is empty
    UBaseType_t free_before = uxQueueMessagesWaiting(free_queue);

    fanout();

    // Should have no effect
    TEST_ASSERT_EQUAL_UINT32(free_before, uxQueueMessagesWaiting(free_queue));
    TEST_ASSERT_EQUAL_UINT32(0, uxQueueMessagesWaiting(xbee_queue));
}

TEST_CASE("block data persists across fanout to consumer", "[can][fanout]")
{
    pool_init();
    if (twai_queue == nullptr)
    {
        twai_queue = xQueueCreate(POOL_SIZE, sizeof(block_t *));
    }

    uint8_t pattern[8] = {0xCA, 0xFE, 0xBA, 0xBE, 0xDE, 0xAD, 0xBE, 0xEF};

    TEST_ASSERT_TRUE(mock_twai_send(pattern, sizeof(pattern)));
    fanout();

    block_t *blk = NULL;
    TEST_ASSERT_EQUAL(pdTRUE, xQueueReceive(xbee_queue, &blk, to_ticks(10)));

    // Data should be intact
    TEST_ASSERT_EQUAL_UINT8_ARRAY(pattern, blk->data, sizeof(pattern));

    // Verify specific bytes
    TEST_ASSERT_EQUAL_UINT8(0xCA, blk->data[0]);
    TEST_ASSERT_EQUAL_UINT8(0xFE, blk->data[1]);
    TEST_ASSERT_EQUAL_UINT8(0xEF, blk->data[7]);

    block_release(blk);
}
