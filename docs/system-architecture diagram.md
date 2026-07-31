[ Core 0: PRO_CPU - Observability ]               [ Core 1: APP_CPU - Real-Time Plane ]

                                                    +-------------------------------+
                                                    |         producer_task         |
                                                    |     (Generates ECG sample)    |
                                                    +-------+---------------+-------+
                                                            |               | 
                                         (ecg_sample_t)     |               | (Set EV_BIT_DATA_PRODUCED)
                                               [ xQueueSend ]               |
                                                            v               v
+-------------------------------+             +-------------------+   +-------------------+
| webmonitor_task / serial      |             |   Queue: data_q   |   |    Event Group    |
| (Reads queue depth, event     |             | (Depth 20, FIFO)  |   |     evt_group     |
|  bits, and heartbeats)        |             +-------------------+   +-------------------+
+---------------+---------------+                               |               ^
                |                             [ xQueueReceive ] |               | (Set EV_BIT_DATA_PROCESSED)
                |                                               v               |
                |                               +-------------------------------+
                |                               |         consumer_task         |
                |                               |      (Arrhythmia Check)       |
                |                               +-------------------------------+
                |
                |                                               +-------------------+
                |                                               | (Wait for BOTH    |
                |                                               |  bits to be set)  |
                |                                               v
                |                               +-------------------------------+
                |                               |        coordinator_task       |
                |                               |   (ECG cycle complete sync)   |
                |                               +---------------+---------------+
                |                                               |
+---------------v---------------+                               | [ xTaskNotifyGive ]
|  [ SHARED MEMORY TELEMETRY ]  |                               v
|  hb_prod, hb_cons, hb_coord,  |               +-------------------------------+
|  hb_resp, isr_entry_time_us   |               |         responder_task        |
+-------------------------------+               |    (Medical Alert Dispatch)   |
                                                +---------------+---------------+
                                                                ^
                                      [ vTaskNotifyGiveFromISR ]|
                                                +-------------------------------+
                                                |     [ ISR ] GPIO 18 Button    |
                                                | (Hardware Priority|IRAM_ATTR) |
                                                +-------------------------------+