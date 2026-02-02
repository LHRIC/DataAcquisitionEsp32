#include "gps/gps.hpp"
#include "driver/i2c_master.h"
#include "freertos/idf_additions.h"
#include "minmea.h"
#include "portmacro.h"
#include <string.h>

SemaphoreHandle_t gpsMutex;
volatile GPSData gpsData;

i2c_master_dev_handle_t gps_i2c_dev_handle;

void gps_init()
{
    i2c_device_config_t device_config;

    device_config.dev_addr_length = I2C_ADDR_BIT_LEN_7;
    device_config.device_address = GPS_I2C_DEVICE_ADDRESS;
    device_config.scl_speed_hz = 400000;

    ESP_ERROR_CHECK(i2c_master_bus_add_device(i2c_bus_handle, &device_config,
                                              &gps_i2c_dev_handle));

    // NOTE: Reading a byte via I2C begins data stream.
    // Waiting one second ensures that data is coming through
    gps_i2c_read_byte();
    vTaskDelay(1000 / portTICK_PERIOD_MS);
}

// NOTE: This task probably doesn't need 0xFFFF stack size
// Fix this at some point.
void gps_task(void *pvTaskParameters)
{
    static char buffer[0xFFFF];
    while (1)
    {
        uint16_t available = gps_i2c_available_bytes();
        if (available == 0)
        {
            vTaskDelay(10 / portTICK_PERIOD_MS);
            continue;
        }

        char *buffer_ptr = buffer;
        for (int i = 0; i < available && i < 0xFFFF; i++)
        {
            *buffer_ptr = gps_i2c_read_byte();
            buffer_ptr++;
        }
        *buffer_ptr = '\0';

        char *line = strtok(buffer, "\n");
        while (line != NULL)
        {
            int len = strlen(line);

            // HACK: strtok replaces characters that minmea needs to function
            // this is a dumb hack, but i don't wan't to write and nmea
            // parser...
            line[len] = '\n';
            line[len + 1] = '\0';
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

            // put characters back so strtok doesn't freak out
            line[len] = '\0';
            line[len + 1] = '$';
            line = strtok(NULL, "\n");
        }
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
 *  GPS I2C Protocol Functions
 */

static uint8_t gps_i2c_read_byte()
{
    uint8_t data;
    ESP_ERROR_CHECK(i2c_master_receive(gps_i2c_dev_handle, &data, 1, -1));

    return data;
}

static uint16_t gps_i2c_available_bytes()
{
    uint8_t address_buffer[1] = {GPS_I2C_BYTES_AVAILABLE_HIGH};
    uint8_t data_buffer[2];
    ESP_ERROR_CHECK(i2c_master_transmit_receive(
        gps_i2c_dev_handle, address_buffer, sizeof(address_buffer), data_buffer,
        sizeof(data_buffer), -1));

    uint16_t available =
        ((data_buffer[0] << 8) & 0xFF00) | (data_buffer[1] & 0xFF);

    return available;
}
