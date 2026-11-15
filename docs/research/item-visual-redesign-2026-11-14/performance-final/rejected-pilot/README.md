# Rejected first moving-load pilot

The scene validated 512 preserved bodies and 256 flying arrows in both resource
sets. The one-pair candidate result exceeded the unchanged +10% p95 gate and was
not accepted. This pilot also exported a full-resolution PNG every second, unlike
the established eight-second capture interval in the other benchmarks. Its data
includes export/readback and desktop swap stalls; the raw samples are retained.

Subsequent measurement uses the established eight-second capture interval and
three alternating baseline/candidate pairs. All measured frames remain included;
no slow samples or thresholds are removed. The moving-only statistic is reported
in addition to the complete eight-second window. A passing later run must satisfy
both gates and preserve all 512 bodies.
