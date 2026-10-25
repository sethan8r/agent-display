"""Session state from Claude Code hook and status line events, turned into board snapshots (docs/protocol.md).

Pure logic, no I/O except the transcript fallback, so it can be tested with recorded events.
"""
import json
import os
import time

PROTO = 1
MAX_SESSIONS = 20

# characters the board's fonts have (firmware/tools/gen_fonts.py TEXT_CODES); anything else is dropped
ALLOWED = (set(range(0x20, 0x7F)) | set(range(0xA0, 0x100)) | set(range(0x410, 0x450)) | set(range(0x2190, 0x2194))
           | {0x401, 0x404, 0x406, 0x407, 0x451, 0x454, 0x456, 0x457, 0x490, 0x491, 0x2013, 0x2014, 0x2026, 0x2116})

ICONS = {"Edit": "edit", "Write": "edit", "MultiEdit": "edit", "NotebookEdit": "edit", "Bash": "run", "PowerShell": "run",
         "Read": "read", "Grep": "find", "Glob": "find", "WebFetch": "web", "WebSearch": "web", "Agent": "agent",
         "Task": "agent"}


def clean(text, limit):
    """One line of text the board can draw, at most `limit` characters."""
    s = " ".join("".join(c if ord(c) in ALLOWED else " " for c in str(text or "")).split())
    return s if len(s) <= limit else s[:limit - 1].rstrip() + "…"


def first_line(text):
    for line in str(text or "").splitlines():
        line = line.strip().lstrip("#>*-• ").replace("**", "").replace("`", "").strip()
        if line:
            return line
    return ""


def action(tool, inp, cwd):
    """What a tool call does, the way the board shows it: "Bash: npm test", "Edit src/app.py"."""
    inp = inp if isinstance(inp, dict) else {}
    if tool in ("Bash", "PowerShell"):
        return f"{tool}: {first_line(inp.get('command'))}"
    path = inp.get("file_path") or inp.get("notebook_path")
    if path:
        return f"{tool} {relpath(path, cwd)}"
    if tool in ("Grep", "Glob"):
        return f"{tool}: {inp.get('pattern', '')}"
    if tool == "WebFetch":
        return "WebFetch " + str(inp.get("url", "")).split("://", 1)[-1]
    if tool == "WebSearch":
        return f"WebSearch: {inp.get('query', '')}"
    if tool in ("Agent", "Task"):
        return f"Agent: {inp.get('description', '')}"
    if tool.startswith("mcp__"):
        return "MCP " + tool.split("__", 2)[-1]
    return tool


def relpath(path, cwd):
    try:
        rel = os.path.relpath(path, cwd) if cwd else path
    except ValueError:  # another drive on Windows
        rel = path
    return (os.path.basename(path) if rel.startswith("..") else rel).replace("\\", "/")


def question_text(inp):
    qs = (inp or {}).get("questions") or []
    return qs[0].get("question", "") if qs and isinstance(qs[0], dict) else ""


def last_dirs(cwd, n=2):
    parts = [p for p in str(cwd or "").replace("\\", "/").split("/") if p and not p.endswith(":")]
    return "/".join(parts[-n:])


def tokens_from_transcript(path):
    """Context size from the usage of the last API answer in the transcript (fallback without the status line)."""
    try:
        with open(path, "rb") as f:
            f.seek(0, 2)
            f.seek(max(0, f.tell() - 65536))
            tail = f.read().decode("utf-8", "replace")
    except (OSError, TypeError):
        return None
    for line in reversed(tail.splitlines()):
        if '"usage"' not in line:
            continue
        try:
            u = json.loads(line)["message"]["usage"]
        except (ValueError, KeyError, TypeError):
            continue
        return sum(u.get(k) or 0 for k in ("input_tokens", "cache_creation_input_tokens", "cache_read_input_tokens",
                                           "output_tokens"))
    return None


def interrupted(path):
    """True when the last message in the transcript is the user stopping Claude: a denied permission prompt
    ("[Request interrupted by user for tool use]") or Esc ("[Request interrupted by user]"). Neither fires a hook."""
    try:
        with open(path, "rb") as f:
            f.seek(0, 2)
            f.seek(max(0, f.tell() - 8192))
            tail = f.read().decode("utf-8", "replace")
    except (OSError, TypeError):
        return False
    for line in reversed(tail.splitlines()):
        try:
            d = json.loads(line)
        except ValueError:
            continue
        if d.get("type") not in ("user", "assistant"):
            continue
        if d["type"] == "assistant":
            return False
        content = (d.get("message") or {}).get("content")
        texts = [content] if isinstance(content, str) else [c.get("text", "") for c in content or [] if isinstance(c, dict)]
        if any(str(t).startswith("[Request interrupted by user") for t in texts):
            return True
        if isinstance(content, list) and any(isinstance(c, dict) and c.get("type") == "tool_result" for c in content):
            continue  # the rejected tool's result comes right before the interrupt line
        return False
    return False


class Session:
    def __init__(self, sid, now):
        self.id, self.cwd, self.transcript = sid, "", None
        self.status, self.wait_for, self.since, self.started, self.last_event = "idle", None, now, now, now
        self.icon = self.action = self.summary = None
        self.subagents = {}  # agent_id -> (type, task)
        self.pending = []    # (subagent_type, description) of Agent calls whose subagent hasn't started yet
        self.name = self.model = self.ctx = self.tokens = self.tokens_max = None
        self.status_line_at = 0
        self.pid = self.pid_created = None

    def work(self, now):
        if self.status in ("idle", "done", "error"):
            self.since = now
        self.status, self.wait_for = "work", None

    def wait(self, kind, text):
        self.status, self.wait_for, self.icon, self.action = "wait", kind, "ask", text


class State:
    def __init__(self):
        self.sessions = {}
        self.limits = {}  # "5h" / "7d" -> {"pct", "reset"}
        self.ended = set()  # a status line update still on its way after SessionEnd must not bring the session back

    def session(self, sid, now):
        return self.sessions.get(sid) or self.sessions.setdefault(sid, Session(sid, now))

    def event(self, ev, now=None):
        """Apply one hook event. Returns the session it touched, or None."""
        now = now or time.time()
        name, sid = ev.get("hook_event_name"), ev.get("session_id")
        if not sid:
            return None
        if name == "SessionEnd":
            self.sessions.pop(sid, None)
            if len(self.ended) > 500:
                self.ended.clear()
            self.ended.add(sid)
            return None
        if name == "SessionStart":
            self.ended.discard(sid)  # resumed
        elif sid in self.ended:
            return None
        s = self.session(sid, now)
        s.last_event = now
        s.cwd = ev.get("cwd") or s.cwd
        s.transcript = ev.get("transcript_path") or s.transcript
        sub = ev.get("agent_id")  # set when the event comes from inside a subagent
        tool, inp = ev.get("tool_name") or "", ev.get("tool_input") or {}

        if name == "UserPromptSubmit":
            if str(ev.get("prompt", "")).startswith("<task-notification>"):  # a background subagent finished
                if s.status == "subs":
                    s.status = "work"
            else:
                s.status, s.since, s.wait_for, s.summary, s.icon, s.action = "work", now, None, None, None, None
                s.pending.clear()
        elif name == "PreToolUse" and not sub:
            if tool == "AskUserQuestion":
                s.wait("question", question_text(inp))
            elif tool == "ExitPlanMode":
                s.wait("plan", first_line(inp.get("plan")))
            else:
                s.work(now)
                s.icon, s.action = ICONS.get(tool, "other"), action(tool, inp, s.cwd)
                if tool in ("Agent", "Task"):
                    s.pending.append((inp.get("subagent_type") or "", inp.get("description") or ""))
        elif name == "PermissionRequest":  # from a subagent too: its prompt waits for you all the same
            if tool == "AskUserQuestion":
                s.wait("question", question_text(inp))
            elif tool == "ExitPlanMode":
                s.wait("plan", first_line(inp.get("plan")))
            else:
                s.wait("perm", action(tool, inp, s.cwd))
        elif name == "Notification" and ev.get("notification_type") == "permission_prompt" and s.status != "wait":
            s.wait("perm", ev.get("message") or "")
        elif name in ("PostToolUse", "PostToolUseFailure"):  # a failed command ends a prompt just the same
            if s.status == "wait":  # the prompt was answered
                s.work(now)
            if not sub:
                self._fallback_context(s, now)
        elif name == "SubagentStart":
            typ = ev.get("agent_type") or ""
            i = next((i for i, p in enumerate(s.pending) if p[0] == typ), 0 if s.pending else None)
            s.subagents[sub] = (typ, s.pending.pop(i)[1] if i is not None else "")
        elif name == "SubagentStop":
            s.subagents.pop(sub, None)
        elif name == "Stop":
            running = [t for t in ev.get("background_tasks") or []
                       if isinstance(t, dict) and t.get("type") == "subagent" and t.get("status") == "running"]
            s.subagents = {t.get("id"): (t.get("agent_type") or "", t.get("description") or "") for t in running}
            s.wait_for = s.icon = s.action = None
            s.pending.clear()
            if running:
                s.status = "subs"
            else:
                s.status, s.since, s.summary = "done", now, first_line(ev.get("last_assistant_message"))
            self._fallback_context(s, now)
        elif name == "StopFailure":
            s.status, s.since, s.wait_for, s.icon, s.action = "error", now, None, None, None
        return s

    def status_line(self, d, now=None):
        """Apply the JSON Claude Code passes to the status line: name, model, context, plan limits."""
        now = now or time.time()
        sid = d.get("session_id")
        if not sid or sid in self.ended:
            return None
        s = self.session(sid, now)
        s.status_line_at = now
        s.cwd = d.get("cwd") or s.cwd
        s.name = d.get("session_name") or s.name
        s.model = (d.get("model") or {}).get("display_name") or s.model
        cw = d.get("context_window") or {}
        pct, size = cw.get("used_percentage"), cw.get("context_window_size")
        if pct is not None:
            s.ctx = round(pct)
            s.tokens_max = size or s.tokens_max
            s.tokens = round(pct / 100 * s.tokens_max) if s.tokens_max else None
        for key, short in (("five_hour", "5h"), ("seven_day", "7d")):
            w = (d.get("rate_limits") or {}).get(key)
            if w and w.get("used_percentage") is not None and w.get("resets_at"):
                self.limits[short] = {"pct": round(w["used_percentage"]), "reset": int(w["resets_at"])}
        return s

    def check_interrupts(self, now):
        """Sessions stopped by the user (denied prompt, Esc) send no hook: catch it in the transcript.
        Returns True if a session changed."""
        changed = False
        for s in self.sessions.values():
            if s.status in ("work", "wait") and now - s.last_event >= 1 and interrupted(s.transcript):
                s.status, s.since, s.wait_for, s.icon, s.action = "idle", now, None, None, None
                changed = True
        return changed

    def _fallback_context(self, s, now):
        if now - s.status_line_at < 60 and s.ctx is not None:
            return
        used = tokens_from_transcript(s.transcript)
        if used:
            s.tokens_max = s.tokens_max or 200000  # shortcut: assumes a 200k window without the status line
            s.tokens, s.ctx = used, min(100, round(used * 100 / s.tokens_max))

    def snapshot(self, now=None, fw_update=None):
        now = now or time.time()
        out = []
        for s in sorted(self.sessions.values(), key=lambda s: s.started):
            o = {"id": s.id[:8], "name": clean(s.name or last_dirs(s.cwd, 1) or "Claude Code", 40), "status": s.status}
            if s.status == "wait" and s.wait_for:
                o["wait_for"] = s.wait_for
            o["since"] = int(s.since)
            if s.status in ("work", "wait") and s.icon:
                o["icon"] = s.icon
            if s.status in ("work", "wait") and s.action:
                o["action"] = clean(s.action, 80)
            if s.ctx is not None:
                o["ctx"] = max(0, min(100, s.ctx))
            if s.tokens and s.tokens_max:
                o["tokens"], o["tokens_max"] = s.tokens, s.tokens_max
            if s.model:
                o["model"] = clean(s.model, 24)
            o["dir"] = clean(last_dirs(s.cwd), 40)
            o["started"] = int(s.started)
            subs = list(s.subagents.values())
            if subs:
                o["subagents"] = [{"type": clean(t, 20), "task": clean(d, 40)} for t, d in subs[:4]]
            if len(subs) > 4:
                o["subagents_more"] = len(subs) - 4
            if s.status == "done" and s.summary:
                o["summary"] = clean(s.summary, 80)
            if s.status == "work":
                o["quiet"] = int(now - s.last_event)
            out.append(o)
        snap = {"t": "snap", "v": PROTO, "now": int(now), "tz": time.localtime(now).tm_gmtoff}
        if self.limits:
            snap["limits"] = dict(self.limits)
        snap["sessions"] = out[:MAX_SESSIONS]
        if len(out) > MAX_SESSIONS:
            snap["sessions_more"] = len(out) - MAX_SESSIONS
        if fw_update:
            snap["fw_update"] = fw_update
        return snap
