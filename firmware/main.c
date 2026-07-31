/*
 * Application 5 — Dual-core IPC pipeline
 *
 * Scaffold level: ~65% (the pipeline logic is yours; the infrastructure is provided).
 *
 * Scaffold Code - AI useage:
 *   Addition of the USE_WEBSERVER compile-time switch and a working serial
 *     monitor task on Core 0, plus per-task heartbeat counters
 *   Logic to allow for switching between a serial monitor and the (student-built)
 *     web monitor, so the pipeline runs in Wokwi with no Wi-Fi by default
 *   Commenting of code including human readable summaries
 *
 * The three IPC primitives are CREATED and wired; the pipeline LOGIC is yours:
 *   Queue             — fixed-size FIFO between producer & consumer
 *   Task notification — fast 1-to-1 signal (ISR or task → specific task)
 *   Event group       — N-way rendezvous (wait for a set of bits)
 * Required by the assignment: at least one use of each, EACH defended.
 *
 * What this scaffold gives you:
 *   - Producer / consumer / coordinator / responder task skeletons on Core 1.
 *   - The event-group rendezvous + coordinator→responder notification, wired.
 *   - A button ISR → responder direct-notification path.
 *   - Per-task heartbeat counters and a Core-0 monitor that prints queue depth,
 *     event-group bits, and heartbeats once a second (USE_WEBSERVER=0).
 *
 * What you do:
 *   1. Producer body — themed data items, queue send with a back-pressure policy.
 *   2. Consumer body — receive with timeout, themed processing.
 *   3. Web monitor — port App 1's HTTP code into webmonitor_task (USE_WEBSERVER=1).
 *   4. Size the queue (depth + item size) and defend it.
 *   5. Theme-rename (YOURTHEME): task names, log strings, the meaning of a data item.
 *
 * What you DON'T need to change:
 *   - The event-group / notification plumbing, the button ISR, or the monitor.
 *   - The heartbeat counters (already incremented at the end of each task loop).
 *
 * ============================================================
 *  RUN MODE  (serial monitor vs. web monitor)
 * ============================================================
 *
 * USE_WEBSERVER selects the Core-0 observability plane. The Core-1 pipeline is
 * identical in both modes.
 *
 *   USE_WEBSERVER = 0  -> Serial monitor (provided, working). Prints queue depth,
 *                         event bits, and heartbeats once a second. No Wi-Fi, so
 *                         the pipeline runs in Wokwi out of the box.
 *   USE_WEBSERVER = 1  -> Web monitor. Runs webmonitor_task instead — the stub you
 *                         fill in with App 1's HTTP server (deliverable #3). Needs
 *                         the Wi-Fi REQUIRES already in this folder's CMakeLists.
 *
 * Start on USE_WEBSERVER=0 to get the pipeline moving in the simulator, then flip
 * to 1 once you have implemented the web monitor.
 *
 * ============================================================
 * Theme: YOURTHEME
 * ============================================================
 */

#ifndef USE_WEBSERVER
#define USE_WEBSERVER 1
#endif

#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/event_groups.h"
#include "freertos/semphr.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_attr.h"

#if USE_WEBSERVER
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "nvs_flash.h"
#include "esp_http_server.h"
#endif

#define BUTTON_GPIO GPIO_NUM_18

#define CONFIG_LOG_DEFAULT_LEVEL_INFO 1
#define CONFIG_LOG_MAXIMUM_LEVEL  5

static const char *TAG = "app5-med";

/* ---------- IPC objects (created in app_main, used everywhere) ---------- */
static QueueHandle_t      data_q;        /* TODO: choose depth + item size for YOUR pipeline */
static EventGroupHandle_t evt_group;
static TaskHandle_t       responder_handle;

/* Event-group bit definitions */
#define EV_BIT_DATA_PRODUCED  (1 << 0)
#define EV_BIT_DATA_PROCESSED (1 << 1)

/* Per-task heartbeats — proof of life for the monitor. Single 32-bit reads are
 * atomic on Xtensa, so the monitor can read these without a lock (App 6's topic). */
static volatile uint32_t hb_prod, hb_cons, hb_coord, hb_resp;

static volatile int64_t isr_entry_time_us;
static volatile uint64_t latency_max_notif_us;

typedef struct {
    uint32_t timestamp_ms;
    float ecg_voltage_mv;
} ecg_sample_t;

/* ---------- Producer task (Core 1) ----------
 * TODO(YOU): generate themed data. Push it into data_q. Set EV_BIT_DATA_PRODUCED.
 */
static void producer_task(void *arg)
{
    int tick = 0;
    for (;;) {
        /* TODO: build a themed data item.
         * Suggested struct: { uint32_t timestamp_ms; int value; } */

        /* TODO: push into queue with a timeout. Decide back-pressure policy. */

        float simulated_mv = (tick % 20 == 0) ? 2.5f : 0.5f; 
        
        ecg_sample_t sample = {
            .timestamp_ms = (uint32_t)(esp_timer_get_time() / 1000),
            .ecg_voltage_mv = simulated_mv
        };

        /* Back-pressure policy: block for up to 5ms. If full, drop the sample. */
        if (xQueueSend(data_q, &sample, pdMS_TO_TICKS(5)) == pdPASS) {
            xEventGroupSetBits(evt_group, EV_BIT_DATA_PRODUCED);
        } else {
            ESP_LOGW(TAG, "[prod] Queue full! Dropped ECG sample.");
        }

        tick++;
        hb_prod++;
        vTaskDelay(pdMS_TO_TICKS(50));   /* 20 Hz producer */
    }
}

/* ---------- Consumer task (Core 1) ----------
 * TODO(YOU): pop from queue, process, set EV_BIT_DATA_PROCESSED. */
static void consumer_task(void *arg)
{
    ecg_sample_t item;
    for (;;) {
        /* TODO(YOU): replace this placeholder wait with a real
         *   xQueueReceive(data_q, &item, timeout) plus themed processing.
         * The delay below only keeps the scaffold from busy-spinning (and
         * starving Core 1's idle task) until you wire the queue up — a real
         * xQueueReceive blocks on its own, so delete this line when you add it. */
        if (xQueueReceive(data_q, &item, portMAX_DELAY) == pdTRUE) {
            ESP_LOGI(TAG, "[cons] Arrhythmia Check -> Time: %lu ms | ECG: %.2f mV", 
                     (unsigned long)item.timestamp_ms, item.ecg_voltage_mv);
            
            xEventGroupSetBits(evt_group, EV_BIT_DATA_PROCESSED);
            hb_cons++;
        }
    }
}

/* ---------- Coordinator task (Core 1) ----------
 * Waits for BOTH event bits to be set, then signals the responder via direct
 * task notification.
 */
static void coordinator_task(void *arg)
{
    const EventBits_t wait_mask = EV_BIT_DATA_PRODUCED | EV_BIT_DATA_PROCESSED;
    for (;;) {
        EventBits_t got = xEventGroupWaitBits(evt_group, wait_mask,
                                              pdTRUE,   /* clear on exit */
                                              pdTRUE,   /* wait for ALL */
                                              portMAX_DELAY);
        if ((got & wait_mask) == wait_mask) {
            /* TODO: do whatever the "cycle complete" event means for your theme.
             * Then notify the responder. */
            // Just getting started and want to see your button presses?
            // Comment out the line below. -mb 
            ESP_LOGI(TAG, "[coord] ECG cycle complete. Triggering medical alert.");
            xTaskNotifyGive(responder_handle);
            hb_coord++;
        }
    }
}

/* ---------- Responder task (Core 1) ----------
 * Wakes via direct task notification from coordinator OR from button ISR.
 */
static void responder_task(void *arg)
{
    for (;;) {
        uint32_t n = ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        if (n == 0) continue;

        int64_t wake = esp_timer_get_time();
        int64_t lat = wake - isr_entry_time_us;
        if ((uint64_t)lat > latency_max_notif_us) latency_max_notif_us = (uint64_t)lat;

        ESP_LOGI(TAG, "[responder] Alert dispatched. Latency=%lld us (max=%llu us)", (long long)lat, (unsigned long long)latency_max_notif_us);
        /* TODO: themed action. */
        hb_resp++;
    }
}

/* ---------- Button ISR — notify responder directly ---------- */
static volatile int64_t last_edge_us;
static void IRAM_ATTR button_isr(void *arg)
{
    int64_t now = esp_timer_get_time();
    if (now - last_edge_us < 200) return;
    last_edge_us = now;

    isr_entry_time_us = now;

    BaseType_t woken = pdFALSE;
    vTaskNotifyGiveFromISR(responder_handle, &woken);
    portYIELD_FROM_ISR(woken);
}

#if USE_WEBSERVER
/* ---------- Web monitor task (Core 0)  [USE_WEBSERVER = 1] ----------
 * TODO(YOU) — deliverable #3. Port the HTTP server from App 1/2 here:
 *   - nvs_flash_init(); esp_netif_init(); esp_event_loop_create_default();
 *   - wifi_init_sta();  (STA connect — serial prints "Got IP: 10.13.37.x")
 *   - start_webserver(); register a root handler that renders:
 *       * uxQueueMessagesWaiting(data_q)     — live queue depth
 *       * xEventGroupGetBits(evt_group)      — event-group bit state
 *       * hb_prod / hb_cons / hb_coord / hb_resp  — per-task heartbeats
 * The Wi-Fi headers are already included above under USE_WEBSERVER, and the
 * Wi-Fi/HTTP components are in this folder's CMakeLists REQUIRES.
 * Auto-refresh ~1 Hz; faster and your handler shows up in latency measurements.
 */

/* ---------- HTTP handler: live JSON state ---------- */
static esp_err_t handle_state(httpd_req_t *req) {
    char buf[128];
    UBaseType_t depth = uxQueueMessagesWaiting(data_q);
    EventBits_t bits  = xEventGroupGetBits(evt_group);
    
    int n = snprintf(buf, sizeof(buf),
        "{\"depth\":%u,\"bits\":%u,\"hb_prod\":%lu,\"hb_cons\":%lu,\"hb_coord\":%lu,\"hb_resp\":%lu}",
        (unsigned)depth, (unsigned)bits,
        (unsigned long)hb_prod, (unsigned long)hb_cons,
        (unsigned long)hb_coord, (unsigned long)hb_resp);
        
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    httpd_resp_send(req, buf, n);
    return ESP_OK;
}

/* ---------- HTTP handler: root page (HTML shell) ---------- */
static esp_err_t handle_root(httpd_req_t *req) {
    static const char html[] =
        "<!DOCTYPE html><html lang=\"en\"><head><meta charset=\"utf-8\">"
        "<title>ECG Pipeline Monitor</title>"
        "<style>body { font-family: sans-serif; padding: 2rem; } .meta { color: #ED1B24; }</style></head>"
        "<body><h1>Medical Monitor: ECG Pipeline</h1>"
        "<p>Queue Depth: <span id=\"depth\">0</span></p>"
        "<p>Event Bits: <span id=\"bits\">0</span></p>"
        "<p class=\"meta\">Heartbeats - Prod: <span id=\"hp\">0</span> | Cons: <span id=\"hc\">0</span> | Coord: <span id=\"hco\">0</span> | Resp: <span id=\"hr\">0</span></p>"
        "<script>"
        "async function poll(){"
        "  try{"
        "    const r = await fetch('/state',{cache:'no-store'});"
        "    const s = await r.json();"
        "    document.getElementById('depth').textContent = s.depth;"
        "    document.getElementById('bits').textContent = s.bits;"
        "    document.getElementById('hp').textContent = s.hb_prod;"
        "    document.getElementById('hc').textContent = s.hb_cons;"
        "    document.getElementById('hco').textContent = s.hb_coord;"
        "    document.getElementById('hr').textContent = s.hb_resp;"
        "  }catch(e){}"
        "}"
        "setInterval(poll, 250); poll();"
        "</script></body></html>";
        
    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, html, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

/* ---------- HTTP & Wi-Fi Init ---------- */
static httpd_handle_t start_webserver(void) {
    httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
    cfg.server_port = 80;
    cfg.core_id = 0; /* Pin networking to Core 0 */

    cfg.task_priority = 5;
    cfg.stack_size = 8192;

    httpd_handle_t server = NULL;
    esp_err_t ret = httpd_start(&server, &cfg);

    if (ret == ESP_OK) {
        httpd_uri_t root = { .uri = "/", .method = HTTP_GET, .handler = handle_root };
        httpd_register_uri_handler(server, &root);
        httpd_uri_t state = { .uri = "/state", .method = HTTP_GET, .handler = handle_state };
        httpd_register_uri_handler(server, &state);
        ESP_LOGI(TAG, "HTTP server started successfully on port 80!");
    } else {
        /* If it fails, this will tell us exactly why */
        ESP_LOGE(TAG, "Failed to start HTTP server! Error: %s", esp_err_to_name(ret));
    }
    return server;
}

static void wifi_event_handler(void *arg, esp_event_base_t base, int32_t id, void *data) {
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) esp_wifi_connect();
    else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) esp_wifi_connect();
    else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)data;
        ESP_LOGI(TAG, "Got IP: " IPSTR, IP2STR(&event->ip_info.ip));
        start_webserver();
    }
}

static void wifi_init_sta(void) {
    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();
    wifi_init_config_t init_cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&init_cfg));
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL));
    wifi_config_t wifi_cfg = { .sta = { .ssid = "Wokwi-GUEST", .password = "", .threshold.authmode = WIFI_AUTH_OPEN }};
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_cfg));
    ESP_ERROR_CHECK(esp_wifi_start());
}

static void webmonitor_task(void *arg)
{
    /* TODO: implement (copy App 1's wifi_init_sta / handle_root / start_webserver). */
    ESP_LOGI(TAG, "[webmon] stub — implement the HTTP server here (USE_WEBSERVER=1)");
    wifi_init_sta();
    for (;;) vTaskDelay(pdMS_TO_TICKS(1000));
}
#else
/* ---------- Serial monitor task (Core 0)  [USE_WEBSERVER = 0] ----------
 * Provided and working. Prints the same state the web monitor will show, so the
 * pipeline is observable in Wokwi with no Wi-Fi. This is your baseline; the web
 * monitor (USE_WEBSERVER=1) renders the identical fields over HTTP.
 */
static void serial_monitor_task(void *arg)
{
    for (;;) {
        UBaseType_t depth = uxQueueMessagesWaiting(data_q);
        EventBits_t bits  = xEventGroupGetBits(evt_group);
        ESP_LOGI(TAG,
                 "[monitor] q_depth=%u  evt=0x%02x  hb: prod=%lu cons=%lu coord=%lu resp=%lu",
                 (unsigned)depth, (unsigned)bits,
                 (unsigned long)hb_prod, (unsigned long)hb_cons,
                 (unsigned long)hb_coord, (unsigned long)hb_resp);
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
#endif /* USE_WEBSERVER */

/* ---------- app_main ---------- */
void app_main(void)
{
    esp_log_level_set(TAG, ESP_LOG_INFO);
    ESP_LOGI(TAG, "==== App 5 [YOURTHEME] starting — IPC pipeline ====");

#if USE_WEBSERVER
    ESP_LOGI(TAG, "Monitor: WEB (USE_WEBSERVER=1) — implement webmonitor_task (Core 0)");
#else
    ESP_LOGI(TAG, "Monitor: SERIAL (USE_WEBSERVER=0) — Core-0 summary once/sec, no Wi-Fi");
#endif

    /* TODO: pick queue length + item size. Defend in README.
     * Hint: producer at 20 Hz, consumer at unknown rate — what burst size? */
    data_q = xQueueCreate(/*depth=*/ 20, /*item size=*/ sizeof(ecg_sample_t));

    evt_group = xEventGroupCreate();

    /* Tasks on Core 1 (real-time plane). 4096-byte stacks: any task that calls
     * ESP_LOGI needs headroom for the vprintf formatting (2048 overflows). */
    xTaskCreatePinnedToCore(producer_task,    "prod",   4096, NULL,  8, NULL, APP_CPU_NUM);
    xTaskCreatePinnedToCore(consumer_task,    "cons",   4096, NULL,  8, NULL, APP_CPU_NUM);
    xTaskCreatePinnedToCore(coordinator_task, "coord",  4096, NULL,  9, NULL, APP_CPU_NUM);
    xTaskCreatePinnedToCore(responder_task,   "resp",   4096, NULL, 12, &responder_handle, APP_CPU_NUM);

    /* Observability plane on Core 0 (networking plane) */
#if USE_WEBSERVER
    xTaskCreatePinnedToCore(webmonitor_task,    "webmon",  4096, NULL, 4, NULL, PRO_CPU_NUM);
#else
    xTaskCreatePinnedToCore(serial_monitor_task, "monitor", 4096, NULL, 4, NULL, PRO_CPU_NUM);
#endif

    /* Button ISR */
    gpio_config_t cfg = {
        .pin_bit_mask = 1ULL << BUTTON_GPIO, .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE, .intr_type = GPIO_INTR_NEGEDGE,
    };
    gpio_config(&cfg);
    gpio_install_isr_service(0);
    gpio_isr_handler_add(BUTTON_GPIO, button_isr, NULL);
}
