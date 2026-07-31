# RTS-Portfolio-Saavedra

# App 5 scaffold — dual-core IPC pipeline

Scaffold level: **~65%** (the pipeline logic is yours; the observability infrastructure is provided).

## What's wired

- Queue (`data_q`), event group (`evt_group`), task notification target (`responder_handle`)
- Producer / consumer / coordinator / responder task skeletons on Core 1
- Button ISR &rarr; direct task notification path
- Per-task heartbeat counters (`hb_prod` / `hb_cons` / `hb_coord` / `hb_resp`)
- A Core-0 monitor behind one `#define`: a working serial monitor (`USE_WEBSERVER=0`) or the web-monitor stub you complete (`USE_WEBSERVER=1`)

## Run modes (`USE_WEBSERVER`)

`USE_WEBSERVER` picks the Core-0 observability plane; the Core-1 pipeline is identical either way. One line at the top of `main.c`:

```c
#define USE_WEBSERVER 0   /* 0 = serial monitor (provided), 1 = web monitor (you build) */
```

- `USE_WEBSERVER = 0` — **serial monitor, provided and working.** Prints queue depth (`uxQueueMessagesWaiting`), event-group bits, and the four heartbeats once a second. No Wi-Fi, so the pipeline runs in Wokwi out of the box. This is your baseline.
- `USE_WEBSERVER = 1` — **web monitor, your deliverable.** Runs `webmonitor_task` instead, where you port App 1's HTTP server and render the same fields over HTTP.

Start on `0` to get the pipeline moving in the simulator, then flip to `1` once the web monitor is in.

## What you build

1. **Producer body** — themed data items, queue send with a back-pressure policy
2. **Consumer body** — receive with timeout, themed processing (delete the placeholder `vTaskDelay` once your `xQueueReceive` blocks for you)
3. **Web monitor** — port App 1's HTTP code into `webmonitor_task` (the `USE_WEBSERVER=1` path), add live queue depth + event-bit readout
4. **Queue sizing** — pick depth + item size, and defend it
5. **Theme** — names, log messages, the meaning of "a data item"

The coordinator &rarr; responder rendezvous is already wired (just verify it), and the heartbeat counters are already provided and fed into the serial monitor.

## Explain in README

- **Queue depth**: how did you size it? Compute the worst-case burst your producer can deliver before the consumer catches up. Show the math.
- **Back-pressure policy**: queue full means... drop oldest? drop newest? block producer? log + drop? Justify.
- **Event-group vs N semaphores**: explain why the event group is the better fit for the producer-consumer rendezvous.
- **Direct notification vs binary semaphore**: measure wake latency on both paths (you have the App 3 helper). Numbers in your README.

## Where to copy from

- App 1's HTTP server: reuse `wifi_init_sta()`, `handle_root()`, `start_webserver()` patterns - drop them into `webmonitor_task` under `USE_WEBSERVER=1`
- App 3's latency-measurement helper: drop it in for the notification path

Heartbeat counters are already provided (`hb_prod` / `hb_cons` / `hb_coord` / `hb_resp`), so you can skip that copy from App 2 and go straight to reading them in your web page/terminal.

## Engineering analysis prompts (graded, see above for full question)

1. Why pin the web server to Core 0 vs Core 1? (The scaffold puts the monitor on Core 0 - defend or challenge that.)
2. Queue depth  & pressure - why did you select the size you did?
3. Explain the pros/cons to Event group vs N semaphores.
4. Direct notification vs binary semaphore — with measured numbers.

## Viewing the web monitor (`USE_WEBSERVER=1`, if you implement it on real hw)

The scaffold ships `webmonitor_task` as a stub. After you port App 1's HTTP code in and flip `USE_WEBSERVER` to `1`, the workflow is identical to App 1/2:

- Wi-Fi connects &rarr; serial monitor prints `Got IP: 10.13.37.x`
- A network indicator appears in Wokwi's simulator panel once port 80 is up
- Click "Open in new tab" to view the page

For App 5 specifically, the page should show live **queue depth** (`uxQueueMessagesWaiting(data_q)`), **event-group bit state** (`xEventGroupGetBits(evt_group)`), and the per-task heartbeats. The auto-refresh interval is your choice &mdash; 1 Hz is reasonable; faster than that and you'll see your own HTTP handler showing up in the latency measurements.

Until the web monitor is implemented, run on `USE_WEBSERVER=0`: the serial monitor prints `[monitor] q_depth=… evt=… hb: prod=… cons=… coord=… resp=…` once a second, and every `[responder] notified (count=N)` line confirms the IPC pipeline is moving.

## Honor code

You're now reusing infrastructure from Apps 1&ndash;3. That's NOT cheating &mdash; that's engineering. Cite where each block came from in the README.
-------------------------------------------------------------------------------------
----------------------- BELOW ARE MY APP 5 DELIVERABLES -----------------------------
-------------------------------------------------------------------------------------
Engineering Analysis Prompts

1. Pinning the web server to Core 0 isolates unpredictable networking latency 
from the hard real-time requirements on Core 1. If the web server was placed on Core 1, 
heavy HTTP requests could preempt our ECG producer or consumer tasks, leading to missed 
cardiac events or dropped sensor readings.

2. Sized at 20 items. The ECG producer runs at 20 Hz. If a higher-priority task preempts 
the consumer for up to 1s, the producer will generate 20 samples. Sizing the queue to 20 
ensures we can buffer a full second of ECG data without losing critical waveform history.

The back-pressure policy is to drop newest. In a real-time cardiac monitor, if the buffer 
is completely full, it is safer to drop the newest sample and log a warning than to permanently 
block the producer task.

3. Event Groups: Highly efficient for a rendezvous where a task needs to synchronize 
against multiple independent conditions simultaneously. In this pipeline, the coordinator
must wait for both EV_BIT_DATA_PRODUCED AND EV_BIT_DATA_PROCESSED at the exact same time.

N Semaphores: Binary semaphores would require the coordinator task to wait 
for them sequentially. This introduces a severe risk of priority inversion or deadlock 
if the tasks execute or fire events out of the expected order. Semaphores are better 
suited for counting identical resources, not synchronizing distinct state flags.

4. Based on the App 3 measurements:

  Direct Task Notification Latency: ~32 µs
  Binary Semaphore Latency: ~2563 µs

  Direct task notifications are significantly faster because they update the target task's 
  Task Control Block directly. Semaphores require the RTOS kernel to manage a separate 
  memory object and traverse blocked-task lists, which adds substantial overhead when 
  attempting to immediately unblock a time-critical task.
-------------------------------------------------------------------------------------
Concurrency Diagram

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



I used Gemini AI for help in this app 5 project, this link shows my conversation
with the LLM https://share.gemini.google/ITlJsckARFSk
