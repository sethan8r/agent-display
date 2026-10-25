"""Status line wrapper: shows the user's own status line unchanged, then passes Claude Code's JSON on to the hub.

statusline_setup.py copies this file into the plugin data dir (it survives plugin updates) and makes it the
statusLine command. Only the status line gets context %, model, session name and plan limits, hence the wrapper.
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
        os.environ.setdefault("CLAUDE_PLUGIN_DATA", HERE)  # this copy lives in the plugin data dir; a hub we start logs there
        sys.path.insert(0, os.path.join(cfg["plugin_root"], "scripts"))
        import hub
        import procs
        data = json.loads(raw)
        data["pid"], data["pid_created"] = procs.claude_ancestor()
        hub.ensure_running()
        hub.post("/status", data, timeout=0.3)
    except Exception:  # noqa: BLE001 — the plugin must never break the status line
        pass


if __name__ == "__main__":
    main()
