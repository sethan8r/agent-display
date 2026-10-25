"""Put the status line wrapper in front of the user's own status line, or take it away again.

Usage: statusline_setup.py status|install|uninstall [--settings PATH]

The wrapper and the user's saved status line live in ~/.claude/agent-display/ (hub.data_dir), not in the plugin's
data dir: Claude Code deletes that on uninstall, and the user's status line must keep working without the plugin.
settings.json is backed up before the first change and replaced atomically; a file that doesn't parse is left alone.
"""
import json
import os
import re
import shutil
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import hub  # noqa: E402


def slash(path):
    return path.replace("\\", "/")  # Git Bash eats backslashes in the statusLine command


def load(path, default):
    """JSON from a file; `default` if it doesn't exist. A file that exists but doesn't parse raises ValueError."""
    try:
        with open(path, encoding="utf-8-sig") as f:  # -sig: Windows editors like to add a BOM
            return json.load(f)
    except FileNotFoundError:
        return default


def save(path, data):
    tmp = path + ".agent-display.tmp"
    with open(tmp, "w", encoding="utf-8") as f:
        json.dump(data, f, indent=2, ensure_ascii=False)
        f.write("\n")
    os.replace(tmp, path)


def installed_wrapper(status_line):
    """Path of our wrapper if the statusLine command runs it (this or an older install location), else None."""
    m = re.search(r'"([^"]*statusline\.py)"|(\S*statusline\.py)', str((status_line or {}).get("command", "")))
    path = m and (m.group(1) or m.group(2))
    return path if path and os.path.exists(os.path.join(os.path.dirname(path), "statusline.json")) else None


def command_for(wrapper):
    # `py -3 ...` parses in Git Bash and PowerShell alike and survives Python upgrades
    if shutil.which("py"):
        return f'py -3 "{slash(wrapper)}"'
    return f'"{slash(sys.executable)}" "{slash(wrapper)}"'


def install_wrapper(cfg):
    d = hub.data_dir()
    cfg["plugin_root"] = os.path.dirname(HERE)
    save(os.path.join(d, "statusline.json"), cfg)
    shutil.copyfile(os.path.join(HERE, "statusline.py"), os.path.join(d, "statusline.py"))
    return os.path.join(d, "statusline.py")


def main(argv):
    action = argv[1] if len(argv) > 1 else ""
    settings_path = argv[3] if len(argv) > 3 and argv[2] == "--settings" else os.path.expanduser("~/.claude/settings.json")
    try:
        settings = load(settings_path, {})
    except (OSError, ValueError) as e:
        sys.exit(f"can't read {settings_path} ({e}); nothing changed")
    current = settings.get("statusLine")
    ours = installed_wrapper(current)
    cfg = load(os.path.join(os.path.dirname(ours), "statusline.json"), {}) if ours else {"original": current}
    original = (cfg.get("original") or {}).get("command") or "(none)"

    if action == "status":
        print(("installed" if ours else "not installed") + "; your status line: " + original)
    elif action == "install":
        wrapper = install_wrapper(cfg)
        line = dict(current) if isinstance(current, dict) else {}  # keeps padding / refreshInterval
        line.update(type="command", command=command_for(wrapper))
        if line != current:
            backup = settings_path + ".agent-display.bak"
            if os.path.exists(settings_path) and not os.path.exists(backup):
                shutil.copyfile(settings_path, backup)
            settings["statusLine"] = line
            save(settings_path, settings)
        print("installed; your own status line stays: " + original)
    elif action == "uninstall":
        if not ours:
            print("not installed, nothing to do")
            return
        if cfg.get("original"):
            settings["statusLine"] = cfg["original"]
        else:
            settings.pop("statusLine", None)
        save(settings_path, settings)
        print("removed; restored: " + original)
    else:
        sys.exit(__doc__)


def refresh():
    """At SessionStart: keep an installed wrapper current with this plugin version and its folder."""
    d = hub.data_dir()
    cfg_path, wrapper = os.path.join(d, "statusline.json"), os.path.join(d, "statusline.py")
    if not os.path.exists(cfg_path):
        return
    with open(os.path.join(HERE, "statusline.py"), "rb") as f:
        new = f.read()
    try:
        with open(wrapper, "rb") as f:
            same = f.read() == new
    except OSError:
        same = False
    cfg = load(cfg_path, {})
    if same and cfg.get("plugin_root") == os.path.dirname(HERE):
        return
    install_wrapper(cfg)


if __name__ == "__main__":
    main(sys.argv)
