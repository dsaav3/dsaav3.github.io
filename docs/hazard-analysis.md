================================================================================================
HAZARD ANALYSIS & STANDARD MAPPING
================================================================================================

HAZARD:      ECG Queue Overflow
EFFECT:      Loss of historical cardiac data; producer task blocks indefinitely.
MITIGATION:  Drop Newest: If `data_q` is full, reject the incoming sample and log a non-blocking 
             RTOS warning. Ensures the consumer has time to catch up without freezing the 
             pipeline.
STANDARD:    IEC 62304 (Medical Device Software) - Data loss mitigation.
------------------------------------------------------------------------------------------------

HAZARD:      Core 0 HTTP Stack Overflow
EFFECT:      Silent system failure; network thread silently crashes due to `snprintf` payload.
MITIGATION:  Memory Provisioning: Increased `webmonitor_task` stack size to 8192 bytes and 
             pinned network execution entirely to Core 0 to protect Core 1 context switching.
STANDARD:    ISO 14971 (Risk Management) - Resource exhaustion.
------------------------------------------------------------------------------------------------

HAZARD:      Priority Inversion on Sync
EFFECT:      Dispatch of medical alerts is delayed by lower-priority tasks holding semaphores.
MITIGATION:  Event Groups: Replaced sequential binary semaphores with a FreeRTOS Event Group 
             for a logical 'AND' rendezvous, avoiding deadlocks.
STANDARD:    IEC 62304 - Concurrency and synchronization hazards.

================================================================================================