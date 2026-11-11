============================
Kyber I/O scheduler tunables
============================

The only two tunables for the Kyber scheduler are the target latencies for
reads and synchronous writes. Kyber will throttle requests in order to meet
these target latencies.

Targets must be between 100000 (100 us) and 10000000000 (10 s); other values
are rejected with EINVAL. Changing a target discards the latency samples
collected for that domain so far.

read_lat_nsec
-------------
Target latency for reads (in nanoseconds).

write_lat_nsec
--------------
Target latency for synchronous writes (in nanoseconds).

domain_depth
------------
Read-only. One line per scheduling domain (READ, WRITE, DISCARD, OTHER) with
the current and the maximum number of requests of that domain that may be
dispatched to the device at once, as ``current/maximum``. The maximum is
derived from the hardware queue depth; the current value drops below it while
Kyber is throttling the domain.
