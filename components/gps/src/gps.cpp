#include "gps/gps.hpp"
#include "driver/i2c_master.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_macros.h"
#include "freertos/idf_additions.h"
#include "i2c/i2c.hpp"
#include "minmea.h"
#include "portmacro.h"
#include <string.h>

SemaphoreHandle_t gpsMutex;
volatile GPSData gpsData;

i2c_device gps_i2c_device;

void gps_init()
{
    ESP_ERROR_CHECK(i2c_add_device(GPS_I2C_DEVICE_ADDRESS, &gps_i2c_device));
    ESP_LOGI("gps", "Configured I2C");

    gpsMutex = xSemaphoreCreateMutex();
    if (gpsMutex == NULL)
    {
        ESP_ERROR_CHECK(ESP_FAIL);
    }
    ESP_LOGI("gps", "Created Mutex");

    gps_i2c_read_byte();
    vTaskDelay(1000 / portTICK_PERIOD_MS);
}

void gps_task(void *pvTaskParameters)
{
    static char buffer[0xFFFF];
    while (1)
    {
        // Check available bytes
        uint16_t available = gps_i2c_available_bytes();

        // Read bytes to buffer
        gps_i2c_read_datastream(buffer, available);

        // Scan for messages and update mutex.
        gps_update_mutex_from_buffer(buffer);

        // Wait until read to poll again.
        vTaskDelay(GPS_POLLING_PERIOD_MS / portTICK_PERIOD_MS);
    }
}

/*
 *  GPS Test Function
 */

void gps_test()
{
    while (true)
    {
        uint16_t available = gps_i2c_available_bytes();
        printf("%d\n", available);
        while (available != 0)
        {
            gps_i2c_read_byte();
            available--;
        }
        // printf("%c", gps_data);
    }
}

/*
 *  GPS Methods
 */

static void gps_update_mutex_from_buffer(char *data_buffer)
{
    char *line = strtok(data_buffer, "\n");
    while (line != NULL)
    {
        switch (minmea_sentence_id(line, false))
        {
        case MINMEA_SENTENCE_GGA:
            struct minmea_sentence_gga frame;
            if (minmea_parse_gga(&frame, line))
            {
                xSemaphoreTake(gpsMutex, portMAX_DELAY);
                gpsData.latitude = minmea_tocoord(&frame.latitude);
                gpsData.longitude = minmea_tocoord(&frame.longitude);
                gpsData.time.tm_hour = frame.time.hours;
                gpsData.time.tm_min = frame.time.minutes;
                gpsData.time.tm_sec = frame.time.seconds;
                gpsData.updated = xTaskGetTickCount();
                xSemaphoreGive(gpsMutex);
            }
            break;
        default:
            break;
        }
        line = strtok(NULL, "\n");
    }
}

/*
 *  GPS I2C Protocol Functions
 */

static uint8_t gps_i2c_read_byte()
{
    uint8_t data = 0;
    ESP_ERROR_CHECK(
        i2c_read_register(&gps_i2c_device, GPS_I2C_DATA_STREAM, &data, 1));

    return data;
}

static uint16_t gps_i2c_available_bytes()
{
    uint8_t address_buffer[1] = {GPS_I2C_BYTES_AVAILABLE_HIGH};
    uint8_t data_buffer[2];
    ESP_ERROR_CHECK(i2c_read_register(
        &gps_i2c_device, GPS_I2C_BYTES_AVAILABLE_HIGH, data_buffer, 2));

    uint16_t available =
        ((data_buffer[0] << 8) & 0xFF00) | (data_buffer[1] & 0xFF);

    return available;
}

static void gps_i2c_read_datastream(char *buffer, uint16_t bytes)
{
    ESP_ERROR_CHECK(i2c_read_register(&gps_i2c_device, GPS_I2C_DATA_STREAM,
                                      (uint8_t *)buffer, bytes));
}
