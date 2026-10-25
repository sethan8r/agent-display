"""Put the status line wrapper in front of the user's own status line, or take it away again.

Usage: statusline_setup.py status|install|uninstall <plugin data dir> [--settings PATH]
The user's statusLine is saved next to the wrapper and restored by uninstall; settings.json is backed up first.
"""
import json
import os
import shutil
import sys

HERE = os.path.dirname(os.path.abspath(__file__))


def slash(path):
    return path.replace("\\", "/")  # Git Bash eats backslashes in the statusLine command


def load(path, default):
    try:
        with open(path, encoding="utf-8") as f:
            return json.load(f)
    except (OSError, ValueError):
        return default


def save(path, data):
    with open(path, "w", encoding="utf-8") as f:
        json.dump(data, f, indent=2, ensure_ascii=False)
        f.write("\n")


def main(argv):
    action, data = argv[1], os.path.abspath(argv[2])
    settings_path = argv[4] if len(argv) > 4 and argv[3] == "--settings" else os.path.expanduser("~/.claude/settings.json")
    settings = load(settings_path, {})
    current = settings.get("statusLine")
    cfg_path, wrapper = os.path.join(data, "statusline.json"), os.path.join(data, "statusline.py")
    ours = isinstance(current, dict) and slash(wrapper) in str(current.get("command", ""))

    if action == "status":
        original = (load(cfg_path, {}).get("original") or {}) if ours else (current or {})
        print(("installed" if ours else "not installed") + "; your status line: " + str(original.get("command") or "(none)"))
    elif action == "install":
        os.makedirs(data, exist_ok=True)
        shutil.copyfile(os.path.join(HERE, "statusline.py"), wrapper)
        cfg = load(cfg_path, {}) if ours else {"original": current}
        cfg["plugin_root"] = os.path.dirname(HERE)
        save(cfg_path, cfg)
        if not ours:
            if os.path.exists(settings_path):
                shutil.copyfile(settings_path, settings_path + ".agent-display.bak")
            line = dict(current) if isinstance(current, dict) else {}  # keeps padding / refreshInterval
            line.update(type="command", command=f'"{slash(sys.executable)}" "{slash(wrapper)}"')
            settings["statusLine"] = line
            save(settings_path, settings)
        print("installed; your own status line stays: " + str((cfg["original"] or {}).get("command") or "(none)"))
    elif action == "uninstall":
        if not ours:
            print("not installed, nothing to do")
            return
        original = load(cfg_path, {}).get("original")
        if original:
            settings["statusLine"] = original
        else:
            settings.pop("statusLine", None)
        save(settings_path, settings)
        print("removed; restored: " + str((original or {}).get("command") or "(no status line)"))
    else:
        sys.exit(__doc__)


def refresh(data):
    """After a plugin update: point the installed wrapper at the new plugin root and refresh its copy."""
    cfg_path, wrapper = os.path.join(data, "statusline.json"), os.path.join(data, "statusline.py")
    cfg = load(cfg_path, None)
    if not cfg or cfg.get("plugin_root") == os.path.dirname(HERE):
        return
    cfg["plugin_root"] = os.path.dirname(HERE)
    save(cfg_path, cfg)
    shutil.copyfile(os.path.join(HERE, "statusline.py"), wrapper)


if __name__ == "__main__":
    main(sys.argv)
