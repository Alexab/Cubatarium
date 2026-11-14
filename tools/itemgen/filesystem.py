"""Bounded Windows rename retry; never delete a destination to bypass a lock."""
import os
import time


def replace_path(source,destination):
    for attempt in range(10):
        try:
            os.replace(source,destination)
            return
        except OSError as error:
            # Directory-name reuse can briefly report access denied after an
            # antivirus/indexer observes a successful move on Windows.
            if getattr(error,'winerror',None) not in (5,32,145) or attempt==9:raise
            time.sleep(min(.01*2**attempt,.25))
