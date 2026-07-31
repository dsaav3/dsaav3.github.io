# Task table & WCET evidence

Measured worst-case execution time (C) against each task's period (T) on the ESP32-WROOM-32, dual-core (App CPU / Pro CPU) deployment.

| Task | Period T (ms) | WCET C (ms) | U = C/T | Priority | Deadline |
|---|---:|---:|---:|---:|---:|
| `producer_task` (ECG sample) | 50 | 2.0 | 0.040 | 3 | 50 ms |
| `consumer_task` (arrhythmia check) | 50 | 10.0 | 0.200 | 2 | 50 ms |
| `coordinator_task` (sync) | 100 | 5.0 | 0.050 | 4 | 100 ms |
| `responder_task` (alerts) | 200 | 10.0 | 0.050 | 5 | 200 ms |
| `webmonitor_task` (Core 0) | 1000 | 50.0 | 0.050 | 1 | 1000 ms |

**Total utilization U = 0.390**

**Verdict:** U (0.390) is well below the Rate-Monotonic sufficiency bound for 5 tasks (≈ 0.743, from n·(2^(1/n) − 1)). The system is comfortably schedulable under Rate-Monotonic, leaving headroom for RTOS overhead, network jitter on Core 0, and interrupt latency on Core 1.
