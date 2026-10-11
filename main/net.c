#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/timers.h"
#include "esp_wifi.h"
#include "esp_netif.h"
#include "esp_netif_sntp.h"
#include "esp_event.h"
#include "esp_log.h"
#include "mdns.h"
#include "clock.h"

static const char *TAG = "net";

volatile net_mode_t g_net_mode = NET_STA;
volatile bool g_wifi_connected = false;
volatile bool g_time_synced = false;

static esp_netif_t  *s_sta_netif;
static TimerHandle_t s_retry_timer;
static bool          s_sntp_started;
static char          s_ntp_server[sizeof(g_settings.ntp)];

static void on_time_sync(struct timeval *tv)
{
    g_time_synced = true;
    ESP_LOGI(TAG, "time synchronized");
}

static void start_sntp(void)
{
    if (s_sntp_started) return;
    strlcpy(s_ntp_server, g_settings.ntp, sizeof(s_ntp_server));
    esp_sntp_config_t cfg = ESP_NETIF_SNTP_DEFAULT_CONFIG(s_ntp_server);
    cfg.sync_cb = on_time_sync;
    esp_netif_sntp_init(&cfg);
    s_sntp_started = true;
    ESP_LOGI(TAG, "SNTP started (%s)", s_ntp_server);
}

static void retry_connect(TimerHandle_t t)
{
    esp_wifi_connect();
}

static void on_wifi_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    if (g_net_mode != NET_STA) return;

    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        const wifi_event_sta_disconnected_t *d = data;
        ESP_LOGW(TAG, "disconnected (reason %d), retrying in 5s", d->reason);
        g_wifi_connected = false;
        xTimerStart(s_retry_timer, 0);
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        const ip_event_got_ip_t *e = data;
        ESP_LOGI(TAG, "connected, IP " IPSTR, IP2STR(&e->ip_info.ip));
        g_wifi_connected = true;
        start_sntp();
    }
}

void net_apply_tx_power(void)
{
    int dbm = g_settings.txpower;
    if (dbm <= 0) return;
    if (dbm < 2) dbm = 2;
    if (dbm > 20) dbm = 20;
    esp_err_t err = esp_wifi_set_max_tx_power((int8_t)(dbm * 4));   /* units of 0.25 dBm */
    ESP_LOGI(TAG, "max TX power %d dBm: %s", dbm, esp_err_to_name(err));
}

void net_start(void)
{
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    s_sta_netif = esp_netif_create_default_wifi_sta();

    wifi_init_config_t init_cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&init_cfg));
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));   /* creds live in our own NVS blob */

    if (g_settings.ssid[0] == '\0') {
        ESP_LOGW(TAG, "no Wi-Fi configured, starting setup portal");
        portal_start();
        return;
    }

    s_retry_timer = xTimerCreate("wifi_retry", pdMS_TO_TICKS(5000), pdFALSE, NULL, retry_connect);
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, on_wifi_event, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, on_wifi_event, NULL));

    wifi_config_t wc = { 0 };
    memcpy(wc.sta.ssid, g_settings.ssid, strnlen(g_settings.ssid, sizeof(wc.sta.ssid)));
    memcpy(wc.sta.password, g_settings.pass, strnlen(g_settings.pass, sizeof(wc.sta.password)));
    wc.sta.threshold.authmode = WIFI_AUTH_OPEN;   /* accept whatever security the AP uses */
    wc.sta.sae_pwe_h2e = WPA3_SAE_PWE_BOTH;
    wc.sta.pmf_cfg.capable = true;

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wc));
    ESP_ERROR_CHECK(esp_wifi_start());
    net_apply_tx_power();

    /* Control page at http://led-clock.local/ (or the IP shown by `status`). */
    if (mdns_init() == ESP_OK) {
        mdns_hostname_set("led-clock");
        mdns_instance_name_set("LED Clock");
        mdns_service_add(NULL, "_http", "_tcp", 80, NULL, 0);
    }
    web_start(false);
    ESP_LOGI(TAG, "connecting to \"%s\"", g_settings.ssid);
}

void net_get_ip(char *buf, size_t len)
{
    esp_netif_ip_info_t ip;
    if (g_wifi_connected && s_sta_netif && esp_netif_get_ip_info(s_sta_netif, &ip) == ESP_OK) {
        snprintf(buf, len, IPSTR, IP2STR(&ip.ip));
    } else {
        strlcpy(buf, "-", len);
    }
}
