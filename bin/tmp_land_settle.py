from pathlib import Path
logs = sorted(Path("bin/logs").glob("Cubatarium.exe.*.INFO.*"), key=lambda p: p.stat().st_mtime, reverse=True)
print("latest", logs[0].name)
text = logs[0].read_text(encoding="utf-8", errors="replace")
for line in text.splitlines():
    if "settle_reason" in line or "soft_exit" in line or "EnterWarmup" in line and ("elapsed_ms=3" in line or "elapsed_ms=1" in line or "heartbeat" in line):
        if "settle_reason" in line or "soft_force" in line or "heartbeat" in line:
            print(line[:240])
