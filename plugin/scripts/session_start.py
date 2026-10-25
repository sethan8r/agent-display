"""SessionStart hook: start the hub if it isn't running and tell it which Claude Code process this session is.

HTTP hooks aren't allowed on SessionStart, so this is the one command hook. It runs async: never slows Claude down.
"""
import json
import os
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import hub  # noqa: E402
import procs  # noqa: E402
import statusline_setup  # noqa: E402


def main():
    try:
        event = json.load(sys.stdin)
    except ValueError:
        return
    event["pid"], event["pid_created"] = procs.claude_ancestor()
    try:  # the plugin may have been updated: keep an installed status line wrapper current
        statusline_setup.refresh()
    except (OSError, ValueError):
        pass
    hub.ensure_running()
    for _ in range(30):  # a freshly started hub needs a moment to open its port
        if hub.post("/hook", event):
            return
        time.sleep(0.1)


if __name__ == "__main__":
    main()
