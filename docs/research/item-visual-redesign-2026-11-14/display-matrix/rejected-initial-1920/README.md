# Rejected initial display probe

The first real-window 1920x1080 / user 100% run was rejected even though all
1041 functional checks passed. Its screen still reported the default 1280x720
viewport immediately after creation. The effective scale was 150%, legitimately
including this monitor's DPI; the runner incorrectly compared that value with
the requested user percentage. This was a probe setup/reporting defect, not
successful 1920 visual acceptance or evidence of stale item models.

The stand now forwards the actual GLFW framebuffer size to the new Item Lab
screen before inspection, records user and effective scales separately, and
checks requested width/height/user scale. These rejected files and one original
frame remain for traceability. Subsequent accepted reports supersede this run.
