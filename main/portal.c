/* Captive setup portal: open soft-AP + catch-all DNS. The page itself is served by web.c. */

#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_wifi.h"
#include "esp_netif.h"
#include "esp_mac.h"
#include "esp_log.h"
#include "lwip/sockets.h"
#include "clock.h"

static const char *TAG = "portal";

/* ---- DNS: answer every A query with our own address ------------------- */

static void dns_task(void *arg)
{
    int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    struct sockaddr_in addr = {
        .sin_family = AF_INET,
        .sin_port = htons(53),
        .sin_addr.s_addr = htonl(INADDR_ANY),
    };
    if (sock < 0 || bind(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        ESP_LOGE(TAG, "DNS socket setup failed");
        if (sock >= 0) close(sock);
        vTaskDelete(NULL);
        return;
    }

    uint8_t buf[512];
    for (;;) {
        struct sockaddr_in from;
        socklen_t from_len = sizeof(from);
        int n = recvfrom(sock, buf, sizeof(buf), 0, (struct sockaddr *)&from, &from_len);
        if (n < 12 || (buf[2] & 0x80)) continue;            /* too short, or a response */

        /* Skip the (single) question: QNAME labels, then QTYPE + QCLASS. */
        int p = 12;
        while (p < n && buf[p] != 0 && buf[p] < 0xC0) p += buf[p] + 1;
        p += 1;
        if (p + 4 > n) continue;
        uint16_t qtype = (buf[p] << 8) | buf[p + 1];
        p += 4;

        bool answer = (qtype == 1 /* A */) && p + 16 <= (int)sizeof(buf);
        buf[2] = 0x84 | (buf[2] & 0x01);   /* QR, AA, keep RD */
        buf[3] = 0x80;                      /* RA, NOERROR */
        buf[4] = 0; buf[5] = 1;             /* QDCOUNT */
        buf[6] = 0; buf[7] = answer ? 1 : 0;
        memset(&buf[8], 0, 4);              /* NSCOUNT, ARCOUNT */
        if (answer) {
            static const uint8_t rr[] = {
                0xC0, 0x0C,             /* pointer to the question name */
                0x00, 0x01, 0x00, 0x01, /* A, IN */
                0x00, 0x00, 0x00, 0x3C, /* TTL 60 */
                0x00, 0x04,             /* RDLENGTH */
                192, 168, 4, 1,
            };
            memcpy(&buf[p], rr, sizeof(rr));
            p += sizeof(rr);
        }
        sendto(sock, buf, p, 0, (struct sockaddr *)&from, from_len);
    }
}

void portal_start(void)
{
    g_net_mode = NET_PORTAL;
    esp_netif_create_default_wifi_ap();

    uint8_t mac[6];
    char ssid[33];
    esp_read_mac(mac, ESP_MAC_WIFI_SOFTAP);
    snprintf(ssid, sizeof(ssid), "LED-Clock-%02X%02X", mac[4], mac[5]);

    wifi_config_t ap = {
        .ap = {
            .channel = 1,
            .max_connection = 4,
            .authmode = WIFI_AUTH_OPEN,
        },
    };
    memcpy(ap.ap.ssid, ssid, strlen(ssid));
    ap.ap.ssid_len = strlen(ssid);

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));   /* STA side is only used for scanning */
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap));
    ESP_ERROR_CHECK(esp_wifi_start());
    net_apply_tx_power();
    web_scan();

    xTaskCreate(dns_task, "dns", 3072, NULL, 4, NULL);
    web_start(true);
    ESP_LOGW(TAG, "setup portal up: join Wi-Fi \"%s\" and browse to http://192.168.4.1/", ssid);
}
