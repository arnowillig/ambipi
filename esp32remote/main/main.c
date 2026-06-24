/*
 * esp32-hid — BLE HID Consumer-Control "remote" for waking a JMGO projector.
 *
 * What it does:
 *   - Advertises as a BLE HID device (appearance: HID, service 0x1812) so the
 *     JMGO "Zubehör koppeln" flow can pair/bond it (numeric comparison, auto-
 *     accepted on our side — you confirm on the beamer).
 *   - Stays connectable (re-advertises undirected when idle) so the TV can
 *     reconnect.
 *   - On BOOT button (GPIO0): if connected -> sends Consumer "Power"; if not
 *     connected -> does HIGH-DUTY DIRECTED advertising to the beamer (exactly
 *     what a real BLE remote does on a keypress) and sends Power once it
 *     reconnects + encrypts.
 *
 * Honest expectation: our Raspberry-Pi tests and prior art (Arduino forum)
 * suggest the JMGO only auto-reconnects to its *own* system remote in standby.
 * This firmware is the definitive test on YOUR hardware: watch the serial log.
 * If after a button press in standby you never see "CONNECTED", the beamer
 * ignores third-party BLE devices for wake — dead end. If it connects, Power
 * is sent and we find out whether it wakes.
 *
 * Set your beamer's address in TV_ADDR below (byte order is REVERSED here).
 */
#include <stdio.h>
#include <string.h>
#include <inttypes.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "driver/gpio.h"
#include "driver/uart.h"
#include <stdarg.h>
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_http_server.h"
#include "esp_ota_ops.h"
#include "esp_app_format.h"
#include "esp_system.h"

#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"
#include "host/util/util.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"

/* defined in the NimBLE config store component */
void ble_store_config_init(void);

#define TAG       "jmgo"
#ifndef FW_VERSION           /* injected from CMake PROJECT_VER (top CMakeLists.txt) */
#define FW_VERSION "dev"
#endif
#define BTN_GPIO  0          /* BOOT button on most ESP32 dev boards */
#define DEV_NAME  "ESP32 Remote"

/* JMGO address B8:D8:2D:28:61:2F (public). NimBLE wants it LITTLE-ENDIAN,
 * i.e. reversed. If your beamer differs, change both bytes and .type.       */
static const ble_addr_t TV_ADDR = {
    .type = BLE_ADDR_PUBLIC,
    .val  = { 0x2F, 0x61, 0x28, 0x2D, 0xD8, 0xB8 },
};

static uint8_t  own_addr_type;
static uint16_t conn_handle      = BLE_HS_CONN_HANDLE_NONE;
static uint16_t report_val_handle;
static volatile bool secured     = false;
static volatile bool want_wake   = false;
static uint32_t wake_ms          = 0;

/* Captured from the ORIGINAL JMGO remote's wake advertisement (sniffed):
 *   Company: MediaTek (0x0046)
 *   Data:    35 | 2F 61 28 2D D8 B8 (beamer addr, little-endian) | FF FF FF FF
 * The beamer's standby BLE scanner watches for exactly this and wakes.     */
static const uint8_t WAKE_MFG[] = {
    0x46, 0x00,                                 /* Company ID: MediaTek      */
    0x35,                                       /* prefix                    */
    0x2F, 0x61, 0x28, 0x2D, 0xD8, 0xB8,         /* B8:D8:2D:28:61:2F reversed*/
    0xFF, 0xFF, 0xFF, 0xFF
};

/* ============ WiFi credentials — FILL THESE IN ============ */
#define WIFI_SSID "Zamonien"
#define WIFI_PASS "n0teA20pGnolm"

/* ---- in-memory log ring shown on the web UI (/log) ---- */
#define LOG_CAP 3072
static char   s_log[LOG_CAP];
static size_t s_log_len = 0;
static SemaphoreHandle_t s_log_mtx;

static void applog(const char *fmt, ...)
{
    char msg[160];
    va_list ap; va_start(ap, fmt);
    vsnprintf(msg, sizeof msg, fmt, ap);
    va_end(ap);
    ESP_LOGI(TAG, "%s", msg);            /* also to serial */

    char line[180];
    int p = snprintf(line, sizeof line, "[%6u] %s\n", (unsigned)esp_log_timestamp(), msg);
    if (p <= 0) return;
    if (p > (int)sizeof line) p = sizeof line;
    if (s_log_mtx && xSemaphoreTake(s_log_mtx, pdMS_TO_TICKS(50)) == pdTRUE) {
        if (s_log_len + (size_t)p >= LOG_CAP) {            /* drop oldest chunk */
            size_t drop = s_log_len + (size_t)p - LOG_CAP + 512;
            if (drop > s_log_len) drop = s_log_len;
            memmove(s_log, s_log + drop, s_log_len - drop);
            s_log_len -= drop;
        }
        memcpy(s_log + s_log_len, line, (size_t)p);
        s_log_len += (size_t)p;
        xSemaphoreGive(s_log_mtx);
    }
}

static void advertise(bool directed);

/* ---- HID report map: Consumer Control, Report ID 1, 1 byte / 8 bits ----
 * bit0 Power, bit1 Menu, bit2 Vol+, bit3 Vol-, bit4 Mute, bit5 Play/Pause,
 * bit6 Next, bit7 Prev.                                                     */
static const uint8_t hid_report_map[] = {
    0x05, 0x0C,       /* Usage Page (Consumer)            */
    0x09, 0x01,       /* Usage (Consumer Control)         */
    0xA1, 0x01,       /* Collection (Application)         */
    0x85, 0x01,       /*   Report ID (1)                  */
    0x15, 0x00,       /*   Logical Min (0)                */
    0x25, 0x01,       /*   Logical Max (1)                */
    0x75, 0x01,       /*   Report Size (1)                */
    0x95, 0x08,       /*   Report Count (8)               */
    0x09, 0x30,       /*   Usage (Power)                  */
    0x09, 0x40,       /*   Usage (Menu)                   */
    0x09, 0xE9,       /*   Usage (Volume Increment)       */
    0x09, 0xEA,       /*   Usage (Volume Decrement)       */
    0x09, 0xE2,       /*   Usage (Mute)                   */
    0x09, 0xCD,       /*   Usage (Play/Pause)             */
    0x09, 0xB5,       /*   Usage (Scan Next)              */
    0x09, 0xB6,       /*   Usage (Scan Previous)          */
    0x81, 0x02,       /*   Input (Data,Var,Abs)           */
    0xC0              /* End Collection                   */
};

static const uint8_t hid_info[]   = { 0x11, 0x01, 0x00, 0x02 }; /* bcdHID 1.11, country 0, flags: RemoteWake */
static const uint8_t pnp_id[]     = { 0x02, 0x6B, 0x1D, 0x46, 0x02, 0x01, 0x00 }; /* USB, VID 1D6B, PID 0246, v1 */
static const uint8_t report_ref[] = { 0x01, 0x01 };            /* Report ID 1, Input */
static uint8_t protocol_mode      = 0x01;                      /* Report Protocol */

struct ro_val { const uint8_t *p; uint16_t len; };
static const struct ro_val rv_report_map = { hid_report_map, sizeof(hid_report_map) };
static const struct ro_val rv_hid_info   = { hid_info,       sizeof(hid_info)       };
static const struct ro_val rv_pnp        = { pnp_id,         sizeof(pnp_id)         };

/* ---- GATT access callbacks ---- */
static int chr_read_ro(uint16_t c, uint16_t a, struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    const struct ro_val *v = arg;
    return os_mbuf_append(ctxt->om, v->p, v->len) == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
}

static int chr_report(uint16_t c, uint16_t a, struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    if (ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR) {
        uint8_t z = 0;   /* current state = no key */
        return os_mbuf_append(ctxt->om, &z, 1) == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
    }
    return 0;
}

static int chr_proto_mode(uint16_t c, uint16_t a, struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    if (ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR)
        return os_mbuf_append(ctxt->om, &protocol_mode, 1) == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
    if (ctxt->op == BLE_GATT_ACCESS_OP_WRITE_CHR && OS_MBUF_PKTLEN(ctxt->om) >= 1)
        os_mbuf_copydata(ctxt->om, 0, 1, &protocol_mode);
    return 0;
}

static int chr_ctrl_point(uint16_t c, uint16_t a, struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    return 0; /* accept HID control point writes (suspend/exit-suspend) */
}

static int dsc_report_ref(uint16_t c, uint16_t a, struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    return os_mbuf_append(ctxt->om, report_ref, sizeof(report_ref)) == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
}

static const struct ble_gatt_svc_def gatt_svcs[] = {
    { /* HID Service */
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = BLE_UUID16_DECLARE(0x1812),
        .characteristics = (struct ble_gatt_chr_def[]) {
            { .uuid = BLE_UUID16_DECLARE(0x2A4A), .access_cb = chr_read_ro, .arg = (void *)&rv_hid_info,
              .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_READ_ENC },
            { .uuid = BLE_UUID16_DECLARE(0x2A4B), .access_cb = chr_read_ro, .arg = (void *)&rv_report_map,
              .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_READ_ENC },
            { .uuid = BLE_UUID16_DECLARE(0x2A4C), .access_cb = chr_ctrl_point,
              .flags = BLE_GATT_CHR_F_WRITE_NO_RSP },
            { .uuid = BLE_UUID16_DECLARE(0x2A4E), .access_cb = chr_proto_mode,
              .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_WRITE_NO_RSP },
            { .uuid = BLE_UUID16_DECLARE(0x2A4D), .access_cb = chr_report,
              .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_READ_ENC | BLE_GATT_CHR_F_NOTIFY,
              .val_handle = &report_val_handle,
              .descriptors = (struct ble_gatt_dsc_def[]) {
                  { .uuid = BLE_UUID16_DECLARE(0x2908), .att_flags = BLE_ATT_F_READ, .access_cb = dsc_report_ref },
                  { 0 }
              } },
            { 0 }
        },
    },
    { /* Device Information Service */
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = BLE_UUID16_DECLARE(0x180A),
        .characteristics = (struct ble_gatt_chr_def[]) {
            { .uuid = BLE_UUID16_DECLARE(0x2A50), .access_cb = chr_read_ro, .arg = (void *)&rv_pnp,
              .flags = BLE_GATT_CHR_F_READ },
            { 0 }
        },
    },
    { 0 }
};

/* ---- send Consumer "Power" (bit0): press + release ---- */
static void send_power(void)
{
    if (conn_handle == BLE_HS_CONN_HANDLE_NONE) { applog("send: not connected"); return; }
    applog("send: Consumer POWER");
    uint8_t down = 0x01, up = 0x00;
    struct os_mbuf *om = ble_hs_mbuf_from_flat(&down, 1);
    ble_gatts_notify_custom(conn_handle, report_val_handle, om);
    vTaskDelay(pdMS_TO_TICKS(40));
    om = ble_hs_mbuf_from_flat(&up, 1);
    ble_gatts_notify_custom(conn_handle, report_val_handle, om);
}

/* ---- GAP event handler ---- */
static int gap_event(struct ble_gap_event *event, void *arg)
{
    switch (event->type) {
    case BLE_GAP_EVENT_CONNECT:
        if (event->connect.status == 0) {
            conn_handle = event->connect.conn_handle;
            secured = false;
            want_wake = false;     /* the connection itself woke the beamer; no key needed */
            applog("CONNECTED (handle %d)", conn_handle);
        } else {
            ESP_LOGW(TAG, "connect failed (status %d), re-advertise", event->connect.status);
            advertise(false);
        }
        return 0;

    case BLE_GAP_EVENT_DISCONNECT:
        applog("disconnected (reason 0x%x)", event->disconnect.reason);
        conn_handle = BLE_HS_CONN_HANDLE_NONE;
        secured = false;
        advertise(false);
        return 0;

    case BLE_GAP_EVENT_ENC_CHANGE:
        ESP_LOGI(TAG, "encryption change, status %d", event->enc_change.status);
        if (event->enc_change.status == 0) secured = true;
        return 0;

    case BLE_GAP_EVENT_ADV_COMPLETE:
        /* directed high-duty adv timed out -> fall back to undirected */
        ESP_LOGI(TAG, "adv complete (reason %d) -> undirected", event->adv_complete.reason);
        advertise(false);
        return 0;

    case BLE_GAP_EVENT_PASSKEY_ACTION:
        if (event->passkey.params.action == BLE_SM_IOACT_NUMCMP) {
            applog("numeric comparison %06" PRIu32 " -> ACCEPT", event->passkey.params.numcmp);
            struct ble_sm_io io = { .action = BLE_SM_IOACT_NUMCMP, .numcmp_accept = 1 };
            ble_sm_inject_io(event->passkey.conn_handle, &io);
        }
        return 0;

    case BLE_GAP_EVENT_REPEAT_PAIRING: {
        /* peer re-pairs while a bond exists: delete old bond, allow retry */
        struct ble_gap_conn_desc desc;
        if (ble_gap_conn_find(event->repeat_pairing.conn_handle, &desc) == 0)
            ble_store_util_delete_peer(&desc.peer_id_addr);
        return BLE_GAP_REPEAT_PAIRING_RETRY;
    }

    case BLE_GAP_EVENT_SUBSCRIBE:
        ESP_LOGI(TAG, "subscribe: attr %d notify=%d", event->subscribe.attr_handle, event->subscribe.cur_notify);
        return 0;

    default:
        return 0;
    }
}

/* ---- advertising ---- */
static void advertise(bool directed)
{
    ble_gap_adv_stop();
    struct ble_gap_adv_params adv = { 0 };

    if (directed) {
        ESP_LOGI(TAG, "adv: DIRECTED high-duty -> beamer");
        adv.conn_mode = BLE_GAP_CONN_MODE_DIR;
        adv.disc_mode = BLE_GAP_DISC_MODE_NON;
        adv.high_duty_cycle = 1;
        int rc = ble_gap_adv_start(own_addr_type, &TV_ADDR, BLE_HS_FOREVER, &adv, gap_event, NULL);
        if (rc) ESP_LOGW(TAG, "directed adv rc=%d", rc);
        return;
    }

    /* undirected connectable: flags + appearance + HID UUID + name */
    struct ble_hs_adv_fields f = { 0 };
    f.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
    f.appearance = 0x0180;            /* Generic Remote Control (was 0x03C1 = Keyboard) */
    f.appearance_is_present = 1;
    f.uuids16 = (ble_uuid16_t[]){ BLE_UUID16_INIT(0x1812) };
    f.num_uuids16 = 1;
    f.uuids16_is_complete = 1;
    const char *name = ble_svc_gap_device_name();
    f.name = (uint8_t *)name;
    f.name_len = strlen(name);
    f.name_is_complete = 1;
    int rc = ble_gap_adv_set_fields(&f);
    if (rc) ESP_LOGW(TAG, "adv_set_fields rc=%d", rc);

    adv.conn_mode = BLE_GAP_CONN_MODE_UND;
    adv.disc_mode = BLE_GAP_DISC_MODE_GEN;
    rc = ble_gap_adv_start(own_addr_type, NULL, BLE_HS_FOREVER, &adv, gap_event, NULL);
    ESP_LOGI(TAG, "adv: undirected rc=%d", rc);
}

/* Replica of the original remote's WAKE advertisement: connectable ADV_IND,
 * HID service, Keyboard appearance, and the MediaTek manufacturer payload
 * carrying the beamer's address. Runs until the beamer connects (or we revert).*/
static void advertise_wake(void)
{
    ble_gap_adv_stop();
    applog("adv: WAKE replica (MediaTek mfg + beamer addr)");

    struct ble_hs_adv_fields f = { 0 };
    f.flags = BLE_HS_ADV_F_DISC_LTD | BLE_HS_ADV_F_BREDR_UNSUP;   /* 0x05 */
    f.uuids16 = (ble_uuid16_t[]){ BLE_UUID16_INIT(0x1812) };
    f.num_uuids16 = 1;
    f.uuids16_is_complete = 1;
    f.appearance = 0x03C1;                 /* Keyboard, exactly like the real wake adv */
    f.appearance_is_present = 1;
    f.mfg_data = (uint8_t *)WAKE_MFG;
    f.mfg_data_len = sizeof(WAKE_MFG);
    int rc = ble_gap_adv_set_fields(&f);
    if (rc) ESP_LOGW(TAG, "wake set_fields rc=%d", rc);

    struct ble_gap_adv_params adv = { 0 };
    adv.conn_mode = BLE_GAP_CONN_MODE_UND;
    adv.disc_mode = BLE_GAP_DISC_MODE_LTD;
    rc = ble_gap_adv_start(own_addr_type, NULL, BLE_HS_FOREVER, &adv, gap_event, NULL);
    ESP_LOGI(TAG, "adv: WAKE rc=%d", rc);
}

static void on_sync(void)
{
    ble_hs_id_infer_auto(0, &own_addr_type);
    uint8_t a[6] = { 0 };
    ble_hs_id_copy_addr(own_addr_type, a, NULL);
    ESP_LOGI(TAG, "synced, own addr %02X:%02X:%02X:%02X:%02X:%02X", a[5], a[4], a[3], a[2], a[1], a[0]);
    advertise(false);
}

static void on_reset(int reason) { ESP_LOGW(TAG, "BLE host reset, reason %d", reason); }

static void host_task(void *param)
{
    nimble_port_run();
    nimble_port_freertos_deinit();
}

/* ---- wake trigger (serial key OR GPIO0 button/jumper) ---- */
static void trigger_wake(void)
{
    ESP_LOGI(TAG, "WAKE requested");
    if (conn_handle != BLE_HS_CONN_HANDLE_NONE) {
        send_power();
    } else {
        want_wake = true;
        wake_ms = esp_log_timestamp();
        advertise_wake();                     /* broadcast the remote's wake signal */
    }
}

static void app_task(void *param)
{
    /* GPIO0 trigger (BOOT button, or briefly bridge IO0<->GND) */
    gpio_config_t io = {
        .pin_bit_mask = 1ULL << BTN_GPIO,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
    };
    gpio_config(&io);

    /* Serial trigger: press any key in the monitor. (Driver may already be
     * owned by the console — ignore the error, reading still works.)        */
    uart_driver_install(UART_NUM_0, 256, 0, 0, NULL, 0);

    int last = 1;
    while (1) {
        int v = gpio_get_level(BTN_GPIO);
        if (v == 0 && last == 1) trigger_wake();   /* button pressed */
        last = v;

        uint8_t ch;
        if (uart_read_bytes(UART_NUM_0, &ch, 1, 0) == 1) {
            ESP_LOGI(TAG, "serial key 0x%02x -> wake", ch);
            trigger_wake();
        }

        /* wake signal got no connection in time -> back to normal advertising */
        if (want_wake && conn_handle == BLE_HS_CONN_HANDLE_NONE &&
            (esp_log_timestamp() - wake_ms) > 12000) {
            want_wake = false;
            ESP_LOGI(TAG, "wake window over -> normal adv");
            advertise(false);
        }
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

/* ---- high-level beamer control (used by HTTP + serial) ---- */
static void beamer_on(void)
{
    if (conn_handle != BLE_HS_CONN_HANDLE_NONE) {
        /* Already connected = networked standby (BT up, display may be off) or
         * fully on. We can't read the display state, so send Power to wake it. */
        applog("ON: connected -> sending Power to wake display");
        send_power();
        return;
    }
    applog("ON: sending wake signal");
    want_wake = true;
    wake_ms = esp_log_timestamp();
    advertise_wake();
}

static void beamer_off(void)
{
    if (conn_handle != BLE_HS_CONN_HANDLE_NONE) { applog("OFF: sending Power"); send_power(); }
    else applog("OFF: not connected (already off?)");
}

/* ---- WiFi STA ---- */
static void wifi_evt(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        applog("wifi: disconnected, retrying");
        esp_wifi_connect();
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *e = (ip_event_got_ip_t *)data;
        applog("wifi: got IP " IPSTR, IP2STR(&e->ip_info.ip));
    }
}

static void wifi_init(void)
{
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_evt, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, wifi_evt, NULL, NULL));
    wifi_config_t wc = { 0 };
    strncpy((char *)wc.sta.ssid,     WIFI_SSID, sizeof(wc.sta.ssid));
    strncpy((char *)wc.sta.password, WIFI_PASS, sizeof(wc.sta.password));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wc));
    ESP_ERROR_CHECK(esp_wifi_start());
    applog("wifi: connecting to \"%s\"", WIFI_SSID);
}

/* ---- HTTP server ---- */
static const char INDEX_HTML[] =
"<!doctype html><html><head><meta charset=utf-8>"
"<meta name=viewport content='width=device-width,initial-scale=1'>"
"<title>JMGO Beamer</title><style>"
"body{font-family:system-ui,sans-serif;background:#111;color:#eee;max-width:560px;margin:1.5em auto;padding:0 1em}"
"h1{font-size:1.3em}button{font-size:1.1em;padding:.7em 1.5em;margin:.3em .3em 0 0;border:0;border-radius:10px;cursor:pointer}"
".on{background:#2e7d32;color:#fff}.off{background:#555;color:#fff}"
"#log{margin-top:1em;background:#000;color:#3f6;font:12px/1.4 monospace;padding:.6em;border-radius:10px;height:320px;overflow:auto;white-space:pre-wrap}"
"</style></head><body>"
"<h1>JMGO Beamer (BT) <small style='opacity:.55;font-size:.6em'>v" FW_VERSION "</small></h1>"
"<button class=on onclick=\"go('/api/beamer/on')\">Ein</button>"
"<button class=off onclick=\"go('/api/beamer/off')\">Aus</button>"
"<p style='opacity:.45;font-size:.8em'>Firmware-Update per <code>make push</code> (OTA)</p>"
"<div id=log>...</div>"
"<script>"
"function go(u){fetch(u).then(r=>r.text()).then(_=>setTimeout(load,400));}"
"function load(){fetch('/log').then(r=>r.text()).then(t=>{var l=document.getElementById('log');l.textContent=t;l.scrollTop=l.scrollHeight;});}"
"setInterval(load,2000);load();"
"</script></body></html>";

static esp_err_t h_root(httpd_req_t *r){ httpd_resp_set_type(r,"text/html"); return httpd_resp_send(r, INDEX_HTML, HTTPD_RESP_USE_STRLEN); }
static esp_err_t h_on (httpd_req_t *r){ beamer_on();  httpd_resp_set_type(r,"text/plain"); return httpd_resp_sendstr(r,"ok\n"); }
static esp_err_t h_off(httpd_req_t *r){ beamer_off(); httpd_resp_set_type(r,"text/plain"); return httpd_resp_sendstr(r,"ok\n"); }
static esp_err_t h_log(httpd_req_t *r)
{
    httpd_resp_set_type(r, "text/plain");
    esp_err_t e = ESP_OK;
    if (s_log_mtx && xSemaphoreTake(s_log_mtx, pdMS_TO_TICKS(100)) == pdTRUE) {
        e = httpd_resp_send(r, s_log, s_log_len);
        xSemaphoreGive(s_log_mtx);
    } else {
        e = httpd_resp_sendstr(r, "");
    }
    return e;
}

/* ---- OTA firmware upload: POST /api/ota/upload (raw .bin body) ---- */
static void reboot_task(void *arg)
{
    (void) arg;
    vTaskDelay(pdMS_TO_TICKS(1200));   /* let the HTTP response flush */
    esp_restart();
}

static esp_err_t h_ota(httpd_req_t *r)
{
    int total = r->content_len;
    applog("OTA: upload started (%d bytes)", total);

    const esp_partition_t *part = esp_ota_get_next_update_partition(NULL);
    if (!part) { applog("OTA: no update partition"); httpd_resp_send_err(r, HTTPD_500_INTERNAL_SERVER_ERROR, "no ota partition"); return ESP_FAIL; }

    esp_ota_handle_t oh = 0;
    esp_err_t err = esp_ota_begin(part, OTA_SIZE_UNKNOWN, &oh);
    if (err != ESP_OK) { applog("OTA: begin failed: %s", esp_err_to_name(err)); httpd_resp_send_err(r, HTTPD_500_INTERNAL_SERVER_ERROR, "ota begin failed"); return ESP_FAIL; }

    char buf[1024];
    int remaining = total, received = 0, last_decile = -1;
    while (remaining > 0) {
        int n = httpd_req_recv(r, buf, remaining < (int)sizeof buf ? remaining : (int)sizeof buf);
        if (n == HTTPD_SOCK_ERR_TIMEOUT) continue;
        if (n <= 0) { esp_ota_abort(oh); applog("OTA: recv failed (%d)", n); httpd_resp_send_err(r, HTTPD_500_INTERNAL_SERVER_ERROR, "recv failed"); return ESP_FAIL; }
        err = esp_ota_write(oh, buf, n);
        if (err != ESP_OK) { esp_ota_abort(oh); applog("OTA: write failed: %s", esp_err_to_name(err)); httpd_resp_send_err(r, HTTPD_500_INTERNAL_SERVER_ERROR, "ota write failed"); return ESP_FAIL; }
        received += n; remaining -= n;
        int dec = total ? (received * 10 / total) : 0;
        if (dec != last_decile) { last_decile = dec; applog("OTA: %d%% (%d/%d)", dec * 10, received, total); }
    }

    err = esp_ota_end(oh);
    if (err != ESP_OK) { applog("OTA: end/validate failed: %s", esp_err_to_name(err)); httpd_resp_send_err(r, HTTPD_500_INTERNAL_SERVER_ERROR, "image invalid"); return ESP_FAIL; }
    err = esp_ota_set_boot_partition(part);
    if (err != ESP_OK) { applog("OTA: set_boot failed: %s", esp_err_to_name(err)); httpd_resp_send_err(r, HTTPD_500_INTERNAL_SERVER_ERROR, "set boot failed"); return ESP_FAIL; }

    applog("OTA: done -> boot '%s', rebooting in ~1s", part->label);
    httpd_resp_set_type(r, "text/plain");
    httpd_resp_sendstr(r, "ok, rebooting\n");
    xTaskCreate(reboot_task, "reboot", 2048, NULL, 5, NULL);
    return ESP_OK;
}

static void http_start(void)
{
    httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
    cfg.server_port = 80;
    cfg.lru_purge_enable = true;
    cfg.recv_wait_timeout = 10;        /* generous for OTA upload */
    cfg.send_wait_timeout = 10;
    httpd_handle_t s = NULL;
    if (httpd_start(&s, &cfg) != ESP_OK) { applog("http: start FAILED"); return; }
    httpd_uri_t u;
    u = (httpd_uri_t){ .uri = "/",                .method = HTTP_GET,  .handler = h_root }; httpd_register_uri_handler(s, &u);
    u = (httpd_uri_t){ .uri = "/api/beamer/on",   .method = HTTP_GET,  .handler = h_on   }; httpd_register_uri_handler(s, &u);
    u = (httpd_uri_t){ .uri = "/api/beamer/off",  .method = HTTP_GET,  .handler = h_off  }; httpd_register_uri_handler(s, &u);
    u = (httpd_uri_t){ .uri = "/log",             .method = HTTP_GET,  .handler = h_log  }; httpd_register_uri_handler(s, &u);
    u = (httpd_uri_t){ .uri = "/api/ota/upload",  .method = HTTP_POST, .handler = h_ota  }; httpd_register_uri_handler(s, &u);
    applog("http: server on :80  (/, /api/beamer/on, /api/beamer/off, /log, /api/ota/upload)");
}

void app_main(void)
{
    s_log_mtx = xSemaphoreCreateMutex();
    applog("boot: JMGO beamer-remote v%s", FW_VERSION);
    esp_err_t rc = nvs_flash_init();
    if (rc == ESP_ERR_NVS_NO_FREE_PAGES || rc == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        nvs_flash_init();
    }

    nimble_port_init();

    ble_svc_gap_init();
    ble_svc_gatt_init();
    ble_svc_gap_device_name_set(DEV_NAME);
    ble_svc_gap_device_appearance_set(0x0180);   /* Remote Control, not Keyboard */

    ble_gatts_count_cfg(gatt_svcs);
    ble_gatts_add_svcs(gatt_svcs);

    /* Security: bond + MITM + Secure Connections, numeric comparison.
     * MITM+DISPLAY_YESNO is what the JMGO required during our tests.        */
    ble_hs_cfg.sync_cb = on_sync;
    ble_hs_cfg.reset_cb = on_reset;
    ble_hs_cfg.store_status_cb = ble_store_util_status_rr;
    ble_hs_cfg.sm_io_cap = BLE_HS_IO_DISPLAY_YESNO;
    ble_hs_cfg.sm_bonding = 1;
    ble_hs_cfg.sm_mitm = 1;
    ble_hs_cfg.sm_sc = 1;
    ble_hs_cfg.sm_our_key_dist  = BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID;
    ble_hs_cfg.sm_their_key_dist = BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID;

    ble_store_config_init();

    nimble_port_freertos_init(host_task);
    xTaskCreate(app_task, "app_task", 4096, NULL, 5, NULL);

    wifi_init();      /* STA, credentials at top of file */
    http_start();     /* :80  -> /, /api/beamer/on, /api/beamer/off, /log */
}
