+--------------------------------+---------------+-------------+---------+----------+----------+
| Task                           | Period T (ms) | WCET C (ms) | U = C/T | Priority | Deadline |
+--------------------------------+---------------+-------------+---------+----------+----------+
| producer_task (ECG Sample)     | 50            | 2.0         | 0.040   | 3        | 50ms     |
| consumer_task (Arrhythmia)     | 50            | 10.0        | 0.200   | 2        | 50ms     |
| coordinator_task (Sync)        | 100           | 5.0         | 0.050   | 4        | 100ms    |
| responder_task (Alerts)        | 200           | 10.0        | 0.050   | 5        | 200ms    |
| webmonitor_task (Core 0)       | 1000          | 50.0        | 0.050   | 1        | 1000ms   |
+--------------------------------+---------------+-------------+---------+----------+----------+

Total utilization U = 0.390. 
Verdict: U (0.390) is well below the Rate-Monotonic (RM) sufficiency bound for 
5 tasks (~0.743). The system is highly schedulable, leaving ample CPU headroom 
for RTOS overhead and network interrupts.