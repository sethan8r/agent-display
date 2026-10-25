"""Status line wrapper: shows the user's own status line unchanged, then passes Claude Code's JSON on to the hub.

statusline_setup.py copies this file into ~/.claude/agent-display/ (it outlives plugin updates and uninstalls) and
makes it the statusLine command. Only the status line gets context %, model, session name and plan limits.
Without the plugin it still prints the user's own status line.
"""
import json
import os
import shutil
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))


def run_original(command, raw):
    if not command:
        return
    bash = shutil.which("bash")
    if os.name == "nt" and os.environ.get("MSYSTEM") and bash:  # Claude Code ran us from Git Bash: same shell for theirs
        r = subprocess.run([bash, "-c", command], input=raw, capture_output=True)
    else:
        r = subprocess.run(command, shell=True, input=raw, capture_output=True)
    sys.stdout.buffer.write(r.stdout)
    sys.stdout.flush()


def main():
    raw = sys.stdin.buffer.read()
    try:
        with open(os.path.join(HERE, "statusline.json"), encoding="utf-8") as f:
            cfg = json.load(f)
    except (OSError, ValueError):
        cfg = {}
    run_original((cfg.get("original") or {}).get("command"), raw)  # theirs first: it is what they look at
    try:
        sys.path.insert(0, os.path.join(cfg["plugin_root"], "scripts"))
        import hub
        import procs
        data = json.loads(raw)
        data["pid"], data["pid_created"] = procs.claude_ancestor()
        if not hub.post("/status", data, timeout=0.3):
            hub.spawn()  # the hub isn't running: start it, the next update gets through
    except Exception:  # noqa: BLE001 — the plugin must never break the status line (or it was uninstalled)
        pass


if __name__ == "__main__":
    main()
