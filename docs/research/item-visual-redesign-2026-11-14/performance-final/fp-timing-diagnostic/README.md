# FP timing diagnostic (not acceptance)

One 30-second pair with opt-in CPU and nonblocking timestamp-query observers.
All frames are retained. Query results are read only after GL reports them
available; final pending queries are discarded without waiting. GPU timestamps
cover the FP pass, not the world or window swap. CPU timing includes observer
polling overhead. This diagnostic must not be substituted for uninstrumented
whole-frame acceptance. Both modes restore runtime resources, world and config.

The prior uninstrumented three-pair 30-second series remains rejected in
`../fp-long-cached-bounds`: median p95 13.114485 -> 16.19278 ms (+23.47%).
The diagnostic shows a small GPU FP interval relative to the multi-millisecond
swap variability. This does not waive the full-frame threshold. Next run is
predefined as five 30-second alternating pairs without either GPU observer,
using the same Release executable for every run and retaining every sample.
