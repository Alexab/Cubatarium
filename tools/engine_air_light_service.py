"""Check observed canonical AIR light service, independently of mesh readiness.

This joins producer requests and physical calculation receipts from one owned
native run. It does not establish field values, latency or framebuffer color.
"""
from __future__ import annotations

import re
from pathlib import Path


def evaluate(logs: list[Path], minimum_columns: int = 3) -> dict:
    if minimum_columns < 1:
        raise ValueError("positive column coverage required")
    requests = {}
    receipts = {}
    faults = []
    fault_count = 0
    observed_records = 0

    def fault(reason):
        nonlocal fault_count
        fault_count += 1
        if len(faults) < 8:
            faults.append(reason)

    for path in logs:
        with path.open(encoding="utf-8", errors="replace") as stream:
            for line in stream:
                submission = "job_submit " in line and "air_requested=" in line
                receipt = "calculation_certificate " in line and "air_outputs=" in line
                if not submission and not receipt:
                    continue
                observed_records += 1
                values = dict(re.findall(r"\b(\w+)=([^\s]+)", line))
                try:
                    job = int(values["job"])
                    if job <= 0:
                        raise ValueError("invalid job identity")
                    if submission:
                        column = re.fullmatch(r"\((-?\d+),(-?\d+)\)", values["column"])
                        requested = int(values["air_requested"])
                        if column is None or requested < 0:
                            raise ValueError("invalid AIR request")
                        row = (tuple(map(int, column.groups())), requested)
                        if job in requests and requests[job] != row:
                            fault(f"conflicting_request:{job}")
                        requests[job] = row
                    else:
                        outputs = int(values["air_outputs"])
                        complete = int(values["air_complete"])
                        inputs_complete = int(values["inputs_complete"])
                        if (outputs < 0 or not 0 <= complete <= outputs or
                                inputs_complete not in (0, 1) or
                                (complete > 0 and inputs_complete != 1)):
                            raise ValueError("invalid AIR physical receipt")
                        row = (outputs, complete)
                        if job in receipts and receipts[job] != row:
                            fault(f"conflicting_receipt:{job}")
                        receipts[job] = row
                except (KeyError, TypeError, ValueError):
                    fault("malformed_air_service_record")

    columns = set()
    joined = 0
    unjoined = 0
    for job, (outputs, complete) in receipts.items():
        request = requests.get(job)
        if request is None:
            unjoined += int(complete > 0)
            continue
        column, requested = request
        if outputs > requested:
            fault(f"unrequested_air_output:{job}")
        if requested > 0 and requested == outputs == complete:
            columns.add(column)
            joined += 1
    status = "FAIL" if fault_count else "PASS" if len(columns) >= minimum_columns else "UNTESTED"
    return dict(schema="engine-air-light-service.v1", status=status,
                reason="joined_air_light_producer_receipts", joined_completed_jobs=joined,
                distinct_columns=len(columns), minimum_columns=minimum_columns,
                observed_records=observed_records, unjoined_completions=unjoined,
                faults=fault_count, examples=faults,
                scope="observed requested AIR fields with complete canonical physical receipts; "
                      "no all-request coverage, field-value, latency, GPU or framebuffer acceptance")
