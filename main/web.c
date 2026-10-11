/* Web control page, served both on the home network and in the setup portal. */

#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "esp_http_server.h"
#include "esp_wifi.h"
#include "esp_timer.h"
#include "esp_system.h"
#include "esp_log.h"
#include "clock.h"

static const char *TAG = "web";

#define MAX_SCAN_APS 20

static wifi_ap_record_t s_aps[MAX_SCAN_APS];
static uint16_t s_ap_count;

static const char PAGE[] =
"<!DOCTYPE html><html><head><meta charset=utf-8>"
"<meta name=viewport content='width=device-width,initial-scale=1'><title>LED Clock</title><style>"
":root{color-scheme:light dark;--bg:#111;--card:#1b1b1b;--fg:#eee;--mut:#999;--acc:#e67e00;--bd:#333}"
"@media (prefers-color-scheme:light){:root{--bg:#f3f3f3;--card:#fff;--fg:#111;--mut:#666;--bd:#ddd}}"
"body{font-family:system-ui,sans-serif;background:var(--bg);color:var(--fg);max-width:480px;margin:0 auto;padding:16px}"
"h1{font-size:22px;margin:4px 0}h2{font-size:16px;margin:0 0 6px}.mut{color:var(--mut);font-size:14px}"
".card{background:var(--card);border:1px solid var(--bd);border-radius:10px;padding:14px;margin:14px 0}"
"label{display:block;font-size:14px;color:var(--mut);margin:12px 0 4px}"
"input,select,button{width:100%;box-sizing:border-box;font-size:16px;padding:9px;border-radius:6px;"
"border:1px solid var(--bd);background:var(--bg);color:var(--fg)}"
"input[type=color]{height:44px;padding:2px}input[type=range]{padding:0}"
"input[type=checkbox]{width:auto;margin:0 8px 0 0;vertical-align:middle}"
"button{background:var(--acc);color:#000;border:0;font-weight:600;margin-top:12px;cursor:pointer}"
"button.sec{background:transparent;color:var(--fg);border:1px solid var(--bd)}"
".sw{display:flex;gap:6px;flex-wrap:wrap;margin-top:8px}.sw b{width:30px;height:30px;border-radius:50%;"
"border:1px solid var(--bd);cursor:pointer}"
".nets a{display:block;padding:7px 9px;margin:4px 0;border:1px solid var(--bd);border-radius:6px;"
"color:var(--fg);text-decoration:none}"
"#msg{position:fixed;left:0;right:0;top:10px;text-align:center;color:var(--acc);font-weight:600}"
"#bar{position:sticky;bottom:10px;display:none;gap:8px;align-items:center;background:var(--card);"
"border:1px solid var(--acc);border-radius:10px;padding:10px;margin-top:14px}"
"#bar span{flex:2}#bar button{flex:1;margin:0}"
"</style></head><body>"
"<h1>LED Clock</h1><div class=mut id=st>Loading&hellip;</div>"
"<div class=card id=disp><h2>Display</h2>"
"<label>Color</label><input type=color id=color><div class=sw id=sw></div>"
"<label>Brightness <span id=bv></span></label><input type=range id=brightness min=1 max=255>"
"<label><input type=checkbox id=ampm>Show A/P indicator</label>"
"<label>Panel layout</label><select id=layout></select>"
"<button class=sec onclick=\"post('/api/test').then(s=>s&&msg('Test pattern on for 20 s'))\">"
"Show test pattern</button></div>"
"<div class=card><h2>Time</h2>"
"<label>Time zone</label><select id=tzsel></select>"
"<label>POSIX TZ string</label><input id=tz>"
"<label>NTP server (applies after reboot)</label><input id=ntp></div>"
"<div class=card id=wifi><h2>Wi-Fi</h2>"
"<div class=nets id=nets></div><button class=sec onclick=scan()>Scan for networks</button>"
"<label>Network name</label><input id=ssid maxlength=32>"
"<label>Password</label><input id=pass type=password maxlength=64>"
"<button onclick=wifi()>Save Wi-Fi &amp; reboot</button>"
"<label>Max TX power, dBm (0 = default; try 8 if Wi-Fi is flaky)</label>"
"<input id=txpower type=number min=0 max=20></div>"
"<div class=card><button class=sec onclick=\"confirm('Reboot the clock?')&&post('/api/reboot')\">"
"Reboot</button></div><div id=msg></div>"
"<div id=bar><span>Unsaved changes</span><button class=sec onclick=revert()>Revert</button>"
"<button onclick=save()>Save</button></div>"
"<script>"
"const $=i=>document.getElementById(i);let tmo;"
"function msg(t){$('msg').textContent=t;clearTimeout(tmo);tmo=setTimeout(()=>$('msg').textContent='',2500)}"
"async function post(u,d){try{const r=await fetch(u,{method:'POST',body:new URLSearchParams(d||{})});"
"if(!r.ok){msg('Error: '+await r.text());return null}return await r.json()}catch(e){msg('Clock not reachable');return null}}"
"function dirty(d){$('bar').style.display=d?'flex':'none'}"
/* Live preview: at most one request in flight, latest values win. */
"let busy=false,queued=null;"
"function set(k,v){queued=Object.assign(queued||{},{[k]:v});if(!busy)flush()}"
"async function flush(){if(!queued)return;busy=true;const d=queued;queued=null;"
"const s=await post('/api/settings',d);busy=false;if(s){status(s);dirty(s.dirty)}flush()}"
"function all(){return{color:$('color').value,brightness:$('brightness').value,ampm:$('ampm').checked?1:0,"
"layout:$('layout').value,tz:$('tz').value,ntp:$('ntp').value,txpower:$('txpower').value}}"
"async function save(){const s=await post('/api/settings',Object.assign(all(),{save:1}));"
"if(s){fill(s);msg('Saved')}}"
"async function revert(){const s=await post('/api/settings',{revert:1});if(s){fill(s);msg('Reverted')}}"
"const LAY=['columns, zig-zag','rows, zig-zag','columns','rows'];"
"for(let i=0;i<16;i++){const o=document.createElement('option');o.value=i;"
"o.textContent=i+': '+LAY[i&3]+(i&4?', flip L/R':'')+(i&8?', flip T/B':'');$('layout').append(o)}"
"[['#ff6000','amber'],['#ff0000','red'],['#00ff00','green'],['#0000ff','blue'],['#ffffff','white'],"
"['#00ffff','cyan'],['#a000ff','purple'],['#ff2878','pink'],['#ffc800','yellow']].forEach(([c,n])=>{"
"const b=document.createElement('b');b.style.background=c;b.title=n;"
"b.onclick=()=>{$('color').value=c;set('color',c)};$('sw').append(b)});"
"function status(s){$('st').textContent=(s.time||'--:--')+' \\u00b7 '+(s.mode=='portal'?'setup mode':"
"s.mode+' to '+s.ssid)+(s.ip!='-'?' \\u00b7 '+s.ip:'')}"
"function nets(s){const n=$('nets');n.textContent='';s.nets.forEach(x=>{const a=document.createElement('a');"
"a.href='#';a.textContent=x.s+'  ('+x.r+' dBm)';a.onclick=()=>{$('ssid').value=x.s;$('pass').focus();return false};"
"n.append(a)})}"
"function fill(s){status(s);nets(s);$('color').value=s.color;$('brightness').value=s.brightness;"
"$('bv').textContent=s.brightness;$('ampm').checked=s.ampm;$('layout').value=s.layout;"
"$('tz').value=s.tz;$('ntp').value=s.ntp;$('ssid').value=s.ssid;$('txpower').value=s.txpower;"
"const t=$('tzsel');t.textContent='';let hit=false;"
"s.presets.forEach(p=>{const o=document.createElement('option');o.value=p.v;o.textContent=p.n;"
"if(p.v==s.tz){o.selected=true;hit=true}t.append(o)});"
"const o=document.createElement('option');o.value='';o.textContent='custom';o.selected=!hit;t.append(o);"
"if(s.mode=='portal')$('disp').before($('wifi'))}"
"async function load(f){try{const s=await(await fetch('/api/status')).json();f?fill(s):status(s);dirty(s.dirty)}"
"catch(e){}}"
"async function scan(){msg('Scanning\\u2026');const s=await post('/api/scan');if(s){nets(s);msg('')}}"
"function wifi(){const d={ssid:$('ssid').value,pass:$('pass').value};"
"if(!d.ssid)return msg('Enter a network name');"
"if(confirm('Save Wi-Fi \"'+d.ssid+'\" and reboot?'))post('/api/wifi',d).then(s=>s&&msg('Rebooting\\u2026'))}"
"$('color').oninput=$('color').onchange=e=>set('color',e.target.value);"
"$('brightness').oninput=$('brightness').onchange=e=>{$('bv').textContent=e.target.value;"
"set('brightness',e.target.value)};"
"$('ampm').onchange=e=>set('ampm',e.target.checked?1:0);"
"$('layout').onchange=e=>set('layout',e.target.value);"
"$('tzsel').onchange=e=>{if(e.target.value){$('tz').value=e.target.value;set('tz',e.target.value)}};"
"$('tz').onchange=e=>set('tz',e.target.value);"
"$('ntp').onchange=e=>set('ntp',e.target.value);"
"$('txpower').onchange=e=>set('txpower',e.target.value);"
"load(true);setInterval(()=>load(false),5000);"
"</script></body></html>";

/* ---- helpers ------------------------------------------------------------ */

typedef struct { char *buf; size_t len, cap; } sbuf_t;

static void sb_printf(sbuf_t *b, const char *fmt, ...)
{
    if (b->len + 1 >= b->cap) return;
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(b->buf + b->len, b->cap - b->len, fmt, ap);
    va_end(ap);
    if (n > 0) b->len = (b->len + n < b->cap) ? b->len + n : b->cap - 1;
}

static void sb_json_str(sbuf_t *b, const char *s)
{
    sb_printf(b, "\"");
    for (; *s; s++) {
        unsigned char c = (unsigned char)*s;
        if (c == '"' || c == '\\') sb_printf(b, "\\%c", c);
        else if (c < 0x20) sb_printf(b, "\\u%04x", c);
        else sb_printf(b, "%c", c);
    }
    sb_printf(b, "\"");
}

static void url_decode(char *s)
{
    char *out = s;
    for (; *s; s++) {
        if (*s == '+') {
            *out++ = ' ';
        } else if (*s == '%' && isxdigit((unsigned char)s[1]) && isxdigit((unsigned char)s[2])) {
            char hex[3] = { s[1], s[2], '\0' };
            *out++ = (char)strtol(hex, NULL, 16);
            s += 2;
        } else {
            *out++ = *s;
        }
    }
    *out = '\0';
}

/* Reads a urlencoded body into buf. */
static esp_err_t read_body(httpd_req_t *req, char *buf, size_t size)
{
    if (req->content_len >= size) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Request too large");
        return ESP_FAIL;
    }
    int got = 0;
    while (got < (int)req->content_len) {
        int r = httpd_req_recv(req, buf + got, req->content_len - got);
        if (r == HTTPD_SOCK_ERR_TIMEOUT) continue;
        if (r <= 0) return ESP_FAIL;
        got += r;
    }
    buf[got] = '\0';
    return ESP_OK;
}

/* Fetches and decodes one form field; false if absent. */
static bool form_field(const char *body, const char *key, char *out, size_t size)
{
    if (httpd_query_key_value(body, key, out, size) != ESP_OK) return false;
    url_decode(out);
    return true;
}

static bool parse_long(const char *s, long lo, long hi, long *out)
{
    char *end;
    long v = strtol(s, &end, 10);
    if (*s == '\0' || *end != '\0' || v < lo || v > hi) return false;
    *out = v;
    return true;
}

void web_scan(void)
{
    wifi_scan_config_t scan = { 0 };
    if (esp_wifi_scan_start(&scan, true) == ESP_OK) {
        uint16_t n = MAX_SCAN_APS;
        esp_wifi_scan_get_ap_records(&n, s_aps);
        s_ap_count = n;
    }
}

/* ---- handlers ------------------------------------------------------------ */

static esp_err_t send_status(httpd_req_t *req)
{
    sbuf_t b = { .cap = 4096 };
    b.buf = malloc(b.cap);
    if (!b.buf) return httpd_resp_send_500(req);

    char ip[20], when[24] = "";
    net_get_ip(ip, sizeof(ip));
    if (g_time_synced) {
        time_t now = time(NULL);
        struct tm tm;
        localtime_r(&now, &tm);
        strftime(when, sizeof(when), "%I:%M:%S %p", &tm);
    }

    sb_printf(&b, "{\"mode\":\"%s\",\"ssid\":",
              g_net_mode == NET_PORTAL ? "portal" : g_wifi_connected ? "connected" : "connecting");
    sb_json_str(&b, g_settings.ssid);
    sb_printf(&b, ",\"ip\":\"%s\",\"time\":\"%s\",\"tz\":", ip, when[0] == '0' ? when + 1 : when);
    sb_json_str(&b, g_settings.tz);
    sb_printf(&b, ",\"ntp\":");
    sb_json_str(&b, g_settings.ntp);
    sb_printf(&b, ",\"color\":\"#%02x%02x%02x\",\"brightness\":%u,\"ampm\":%s,\"layout\":%u,\"txpower\":%d"
              ",\"dirty\":%s",
              g_settings.r, g_settings.g, g_settings.b, g_settings.brightness,
              g_settings.ampm ? "true" : "false", g_settings.layout, g_settings.txpower,
              g_settings_dirty ? "true" : "false");

    sb_printf(&b, ",\"presets\":[");
    for (size_t i = 0; i < TZ_PRESET_COUNT; i++) {
        sb_printf(&b, "%s{\"n\":\"%s\",\"v\":", i ? "," : "", TZ_PRESETS[i].name);
        sb_json_str(&b, TZ_PRESETS[i].posix);
        sb_printf(&b, "}");
    }

    sb_printf(&b, "],\"nets\":[");
    bool first = true;
    for (int i = 0; i < s_ap_count; i++) {
        const char *name = (const char *)s_aps[i].ssid;
        if (!name[0]) continue;
        bool dup = false;
        for (int j = 0; j < i && !dup; j++) dup = !strcmp(name, (const char *)s_aps[j].ssid);
        if (dup) continue;
        sb_printf(&b, "%s{\"s\":", first ? "" : ",");
        sb_json_str(&b, name);
        sb_printf(&b, ",\"r\":%d}", s_aps[i].rssi);
        first = false;
    }
    sb_printf(&b, "]}");

    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    esp_err_t err = httpd_resp_send(req, b.buf, b.len);
    free(b.buf);
    return err;
}

static esp_err_t page_get(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    return httpd_resp_send(req, PAGE, sizeof(PAGE) - 1);
}

static esp_err_t status_get(httpd_req_t *req)
{
    return send_status(req);
}

static esp_err_t bad_request(httpd_req_t *req, const char *why)
{
    httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, why);
    return ESP_FAIL;
}

/*
 * Applies any fields present to the live settings (the display picks them up immediately).
 * Changes stay in RAM until a request carries save=1; revert=1 reloads the saved settings.
 */
static esp_err_t settings_post(httpd_req_t *req)
{
    char body[512], v[128];
    long n;
    if (read_body(req, body, sizeof(body)) != ESP_OK) return ESP_FAIL;

    if (form_field(body, "revert", v, sizeof(v))) {
        settings_load();
        g_tz_dirty = true;
        net_apply_tx_power();
        return send_status(req);
    }

    if (form_field(body, "color", v, sizeof(v))) {
        char *end;
        unsigned long rgb = strtoul(v[0] == '#' ? v + 1 : v, &end, 16);
        if (strlen(v) != 7 || *end != '\0') return bad_request(req, "color must be #rrggbb");
        g_settings.r = (rgb >> 16) & 0xFF;
        g_settings.g = (rgb >> 8) & 0xFF;
        g_settings.b = rgb & 0xFF;
    }
    if (form_field(body, "brightness", v, sizeof(v))) {
        if (!parse_long(v, 1, 255, &n)) return bad_request(req, "brightness must be 1-255");
        g_settings.brightness = (uint8_t)n;
    }
    if (form_field(body, "ampm", v, sizeof(v))) {
        g_settings.ampm = (v[0] == '1');
    }
    if (form_field(body, "layout", v, sizeof(v))) {
        if (!parse_long(v, 0, 15, &n)) return bad_request(req, "layout must be 0-15");
        g_settings.layout = (uint8_t)n;
    }
    if (form_field(body, "tz", v, sizeof(v))) {
        if (!v[0] || strlen(v) >= sizeof(g_settings.tz)) return bad_request(req, "invalid time zone");
        strlcpy(g_settings.tz, v, sizeof(g_settings.tz));
        g_tz_dirty = true;
    }
    if (form_field(body, "ntp", v, sizeof(v))) {
        if (!v[0] || strlen(v) >= sizeof(g_settings.ntp)) return bad_request(req, "invalid NTP server");
        strlcpy(g_settings.ntp, v, sizeof(g_settings.ntp));
    }
    if (form_field(body, "txpower", v, sizeof(v))) {
        if (!parse_long(v, 0, 20, &n)) return bad_request(req, "TX power must be 0-20");
        g_settings.txpower = (int8_t)n;
        net_apply_tx_power();
    }
    if (form_field(body, "save", v, sizeof(v))) {
        if (settings_save() != ESP_OK) {
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Saving to flash failed");
            return ESP_FAIL;
        }
    } else {
        g_settings_dirty = true;
    }
    return send_status(req);
}

static void restart_cb(void *arg)
{
    esp_restart();
}

static void restart_soon(void)
{
    static esp_timer_handle_t t;
    const esp_timer_create_args_t args = { .callback = restart_cb, .name = "restart" };
    if (!t) esp_timer_create(&args, &t);
    esp_timer_start_once(t, 1500 * 1000);
}

static esp_err_t wifi_post(httpd_req_t *req)
{
    char body[512], ssid[100] = "", pass[200] = "";
    if (read_body(req, body, sizeof(body)) != ESP_OK) return ESP_FAIL;
    form_field(body, "ssid", ssid, sizeof(ssid));
    form_field(body, "pass", pass, sizeof(pass));
    if (!ssid[0] || strlen(ssid) > 32 || strlen(pass) > 64) {
        return bad_request(req, "Invalid network name or password");
    }
    strlcpy(g_settings.ssid, ssid, sizeof(g_settings.ssid));
    strlcpy(g_settings.pass, pass, sizeof(g_settings.pass));
    settings_save();
    ESP_LOGI(TAG, "saved network \"%s\", rebooting", g_settings.ssid);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, "{}");
    restart_soon();
    return ESP_OK;
}

static esp_err_t scan_post(httpd_req_t *req)
{
    /* Scanning while the station is mid-connect would disturb the reconnect logic. */
    if (g_net_mode != NET_PORTAL && !g_wifi_connected) return bad_request(req, "Wi-Fi is not connected");
    web_scan();
    return send_status(req);
}

static esp_err_t test_post(httpd_req_t *req)
{
    g_test_until_us = esp_timer_get_time() + 20LL * 1000 * 1000;
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_sendstr(req, "{}");
}

static esp_err_t reboot_post(httpd_req_t *req)
{
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, "{}");
    restart_soon();
    return ESP_OK;
}

/* In the setup portal, anything else (OS connectivity checks etc.) is bounced to the page. */
static esp_err_t redirect_404(httpd_req_t *req, httpd_err_code_t err)
{
    httpd_resp_set_status(req, "302 Found");
    httpd_resp_set_hdr(req, "Location", "http://192.168.4.1/");
    httpd_resp_send(req, "Redirecting", HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

void web_start(bool captive)
{
    httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
    cfg.lru_purge_enable = true;
    cfg.max_open_sockets = 7;
    cfg.max_uri_handlers = 10;
    cfg.stack_size = 6144;

    httpd_handle_t server;
    if (httpd_start(&server, &cfg) != ESP_OK) {
        ESP_LOGE(TAG, "HTTP server failed to start");
        return;
    }
    const httpd_uri_t uris[] = {
        { .uri = "/",             .method = HTTP_GET,  .handler = page_get },
        { .uri = "/api/status",   .method = HTTP_GET,  .handler = status_get },
        { .uri = "/api/settings", .method = HTTP_POST, .handler = settings_post },
        { .uri = "/api/wifi",     .method = HTTP_POST, .handler = wifi_post },
        { .uri = "/api/scan",     .method = HTTP_POST, .handler = scan_post },
        { .uri = "/api/test",     .method = HTTP_POST, .handler = test_post },
        { .uri = "/api/reboot",   .method = HTTP_POST, .handler = reboot_post },
    };
    for (size_t i = 0; i < sizeof(uris) / sizeof(uris[0]); i++) {
        httpd_register_uri_handler(server, &uris[i]);
    }
    if (captive) httpd_register_err_handler(server, HTTPD_404_NOT_FOUND, redirect_404);
}
