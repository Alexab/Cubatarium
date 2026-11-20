"""A missing component or diagnostic proxy must never authorize acceptance."""
CORE_COMPONENTS = ("runtime", "startup", "route", "inspection", "visual",
                   "performance", "build_provenance")
STATUSES = {"PASS", "FAIL", "UNTESTED"}


def required_for_options(*, read_failure=False, air_cancellation=False,
                         terrain_save_failure=False, world_actions=False,
                         synchronous_meshing=False, capture_backpressure=False, walking=False):
    requirements = []
    for enabled, names in (
        (read_failure, ("disk_read_failure",)),
        (air_cancellation, ("air_cancellation",)),
        (terrain_save_failure, ("terrain_save_failure", "save_error_ui")),
        (world_actions, ("world_actions",)),
        (synchronous_meshing, ("synchronous_meshing",)),
        (capture_backpressure, ("capture_backpressure",)),
        (walking, ("surface_route",)),
    ):
        if enabled:
            requirements.extend(names)
    return requirements


def finalize(verdict, required=()):
    parts = verdict.setdefault("components", {})
    if not isinstance(parts, dict):
        parts = {"verdict_contract": {"status": "FAIL", "reason": "malformed_components"}}
        verdict["components"] = parts
    for name in (*CORE_COMPONENTS, *required):
        if name not in parts:
            parts[name] = {"status": "UNTESTED", "reason": "missing_required_component"}
    for name, part in list(parts.items()):
        if (not isinstance(part, dict) or not isinstance(part.get("status"), str)
                or part["status"] not in STATUSES):
            parts[name] = {"status": "FAIL", "reason": "malformed_component_verdict"}
    statuses = {part["status"] for part in parts.values()}
    verdict["status"] = ("FAIL" if "FAIL" in statuses else
                         "UNTESTED" if "UNTESTED" in statuses else "PASS")
    verdict["required_components"] = sorted(set(CORE_COMPONENTS) | set(required))
    return verdict
