#include <string.h>
#include <sys/param.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "lwip/err.h"
#include "lwip/sockets.h"
#include "lwip/sys.h"
#include "lwip/netdb.h"

#define DNS_PORT 53
#define DNS_MAX_LEN 512

static const char *TAG = "dns_server";

typedef struct __attribute__((packed)) {
    uint16_t id;
    uint16_t flags;
    uint16_t qd_count;
    uint16_t an_count;
    uint16_t ns_count;
    uint16_t ar_count;
} dns_header_t;

static void dns_server_task(void *pvParameters) {
    char rx_buffer[DNS_MAX_LEN];
    char tx_buffer[DNS_MAX_LEN];
    struct sockaddr_storage source_addr;
    socklen_t socklen = sizeof(source_addr);

    int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
    if (sock < 0) {
        ESP_LOGE(TAG, "Unable to create socket: errno %d", errno);
        vTaskDelete(NULL);
        return;
    }

    struct sockaddr_in dest_addr;
    dest_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    dest_addr.sin_family = AF_INET;
    dest_addr.sin_port = htons(DNS_PORT);

    int err = bind(sock, (struct sockaddr *)&dest_addr, sizeof(dest_addr));
    if (err < 0) {
        ESP_LOGE(TAG, "Socket unable to bind: errno %d", errno);
        close(sock);
        vTaskDelete(NULL);
        return;
    }

    ESP_LOGI(TAG, "DNS server started on port %d", DNS_PORT);

    while (1) {
        int len = recvfrom(sock, rx_buffer, sizeof(rx_buffer), 0, (struct sockaddr *)&source_addr, &socklen);

        if (len < sizeof(dns_header_t)) {
            continue;
        }

        dns_header_t *header = (dns_header_t *)rx_buffer;
        ESP_LOGI(TAG, "Received DNS query, ID: 0x%04x", ntohs(header->id));

        // Prepare response
        dns_header_t *res_header = (dns_header_t *)tx_buffer;
        res_header->id = header->id;
        res_header->flags = htons(0x8400); // Response, Authoritative Answer
        res_header->qd_count = header->qd_count;
        res_header->an_count = header->qd_count;
        res_header->ns_count = 0;
        res_header->ar_count = 0;

        int tx_len = sizeof(dns_header_t);
        int rx_pos = sizeof(dns_header_t);

        // Copy questions and append answers
        for (int i = 0; i < ntohs(header->qd_count); i++) {
            // Copy question name
            while (rx_buffer[rx_pos] != 0 && rx_pos < len) {
                tx_buffer[tx_len++] = rx_buffer[rx_pos++];
            }
            tx_buffer[tx_len++] = rx_buffer[rx_pos++]; // null terminator
            tx_buffer[tx_len++] = rx_buffer[rx_pos++]; // type high
            tx_buffer[tx_len++] = rx_buffer[rx_pos++]; // type low
            tx_buffer[tx_len++] = rx_buffer[rx_pos++]; // class high
            tx_buffer[tx_len++] = rx_buffer[rx_pos++]; // class low

            // Add answer
            tx_buffer[tx_len++] = 0xc0; // Pointer to name
            tx_buffer[tx_len++] = sizeof(dns_header_t);
            tx_buffer[tx_len++] = 0x00; tx_buffer[tx_len++] = 0x01; // Type A
            tx_buffer[tx_len++] = 0x00; tx_buffer[tx_len++] = 0x01; // Class IN
            tx_buffer[tx_len++] = 0x00; tx_buffer[tx_len++] = 0x00; 
            tx_buffer[tx_len++] = 0x00; tx_buffer[tx_len++] = 0x3c; // TTL 60s
            tx_buffer[tx_len++] = 0x00; tx_buffer[tx_len++] = 0x04; // Data length 4
            // IP Address 192.168.4.1
            tx_buffer[tx_len++] = 192; tx_buffer[tx_len++] = 168;
            tx_buffer[tx_len++] = 4;   tx_buffer[tx_len++] = 1;
        }

        sendto(sock, tx_buffer, tx_len, 0, (struct sockaddr *)&source_addr, socklen);
    }
}

void start_dns_server(void) {
    xTaskCreate(dns_server_task, "dns_server", 4096, NULL, 10, NULL);
}
