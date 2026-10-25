"""Session state machine on events shaped like the ones recorded from Claude Code 2.1.310 (see CLAUDE.md).

Run: py -3 -m unittest discover plugin/tests
"""
import json
import os
import sys
import unittest

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "scripts"))
import state  # noqa: E402

SID = "80d59de8-8f28-436f-b473-51bcdd266253"
CWD = "E:\\VSCodeProjects\\agent-display"


def ev(name, t, **kw):
    return dict(kw, hook_event_name=name, session_id=SID, cwd=CWD), t


class SessionFlow(unittest.TestCase):
    def setUp(self):
        self.st = state.State()

    def apply(self, *events):
        for e, t in events:
            self.st.event(e, now=t)
        return self.st.snapshot(now=events[-1][1])["sessions"]

    def test_full_task(self):
        s = self.apply(ev("SessionStart", 100, source="startup"))
        self.assertEqual(s[0]["status"], "idle")
        self.assertEqual(s[0]["name"], "agent-display")
        self.assertEqual(s[0]["dir"], "VSCodeProjects/agent-display")

        s = self.apply(ev("UserPromptSubmit", 110, prompt="fix the hub"),
                       ev("PreToolUse", 112, tool_name="Bash", tool_input={"command": "npm test\nnpm run lint"}))
        self.assertEqual((s[0]["status"], s[0]["since"], s[0]["icon"]), ("work", 110, "run"))
        self.assertEqual(s[0]["action"], "Bash: npm test")

        s = self.apply(ev("PermissionRequest", 113, tool_name="Edit",
                          tool_input={"file_path": CWD + "\\plugin\\hub.py"}))
        self.assertEqual((s[0]["status"], s[0]["wait_for"], s[0]["action"]), ("wait", "perm", "Edit plugin/hub.py"))
        self.assertNotIn("quiet", s[0])

        s = self.apply(ev("PostToolUse", 120, tool_name="Edit"))
        self.assertEqual((s[0]["status"], s[0]["since"]), ("work", 110))  # the task timer keeps running

        # a background subagent: description comes from the Agent call, the id from SubagentStart
        s = self.apply(ev("PreToolUse", 121, tool_name="Agent",
                          tool_input={"description": "List scratch files", "subagent_type": "Explore"}),
                       ev("SubagentStart", 122, agent_id="ac9a", agent_type="Explore"),
                       ev("PreToolUse", 123, agent_id="ac9a", agent_type="Explore", tool_name="Bash",
                          tool_input={"command": "ls"}))
        self.assertEqual(s[0]["subagents"], [{"type": "Explore", "task": "List scratch files"}])
        self.assertEqual(s[0]["action"], "Agent: List scratch files")  # the subagent's own tools don't overwrite it

        s = self.apply(ev("Stop", 124, background_tasks=[{"id": "ac9a", "type": "subagent", "status": "running",
                                                          "description": "List scratch files",
                                                          "agent_type": "Explore"}]))
        self.assertEqual(s[0]["status"], "subs")

        s = self.apply(ev("SubagentStop", 130, agent_id="ac9a", agent_type="Explore"),
                       ev("UserPromptSubmit", 131, prompt="<task-notification>\n<task-id>ac9a</task-id>"))
        self.assertEqual((s[0]["status"], s[0]["since"]), ("work", 110))  # not a new task
        self.assertNotIn("subagents", s[0])
        self.assertEqual(s[0]["quiet"], 0)

        s = self.apply(ev("Stop", 140, background_tasks=[],
                          last_assistant_message="## Done\n\nMoved **14** tables to `Postgres` 🎉"))
        self.assertEqual((s[0]["status"], s[0]["since"]), ("done", 140))
        self.assertEqual(s[0]["summary"], "Done")

        s = self.apply(ev("UserPromptSubmit", 200, prompt="again"), ev("StopFailure", 210))
        self.assertEqual((s[0]["status"], s[0]["since"]), ("error", 210))

        self.st.event(ev("SessionEnd", 220, reason="other")[0], now=220)
        self.assertEqual(self.st.snapshot(now=220)["sessions"], [])

    def test_questions_and_plans(self):
        s = self.apply(ev("UserPromptSubmit", 1, prompt="go"),
                       ev("PreToolUse", 2, tool_name="AskUserQuestion",
                          tool_input={"questions": [{"question": "Push to main or open a PR?"}]}))
        self.assertEqual((s[0]["wait_for"], s[0]["action"], s[0]["icon"]), ("question", "Push to main or open a PR?", "ask"))
        s = self.apply(ev("PreToolUse", 3, tool_name="ExitPlanMode", tool_input={"plan": "# Plan: hub + hooks\n1. ..."}))
        self.assertEqual((s[0]["wait_for"], s[0]["action"]), ("plan", "Plan: hub + hooks"))

    def test_quiet(self):
        s = self.apply(ev("UserPromptSubmit", 10, prompt="go"), ev("PreToolUse", 15, tool_name="Read",
                                                                  tool_input={"file_path": "/x/a.py"}))
        self.assertEqual(self.st.snapshot(now=200)["sessions"][0]["quiet"], 185)
        self.assertEqual(s[0]["action"], "Read a.py")

    def test_status_line(self):
        self.st.status_line({"session_id": SID, "session_name": "Hub refactoring", "cwd": CWD,
                             "model": {"display_name": "Opus 5.5"},
                             "context_window": {"used_percentage": 42.4, "context_window_size": 200000},
                             "rate_limits": {"five_hour": {"used_percentage": 23.5, "resets_at": 1738425600}}}, now=5)
        snap = self.st.snapshot(now=6)
        s = snap["sessions"][0]
        self.assertEqual((s["name"], s["model"], s["ctx"], s["tokens"], s["tokens_max"]),
                         ("Hub refactoring", "Opus 5.5", 42, 84800, 200000))
        self.assertEqual(snap["limits"], {"5h": {"pct": 24, "reset": 1738425600}})

    def test_denied_prompt_and_esc(self):
        """No hook fires when the user denies a prompt or presses Esc: the transcript tells (lines as recorded)."""
        import tempfile
        rejected = {"type": "user", "message": {"role": "user", "content": [
            {"type": "tool_result", "is_error": True, "content": "The user doesn't want to proceed with this tool use."}]}}
        stop_line = {"type": "user", "message": {"role": "user", "content": [
            {"type": "text", "text": "[Request interrupted by user for tool use]"}]}}
        tool_use = {"type": "assistant", "message": {"role": "assistant", "content": [{"type": "tool_use"}]}}
        with tempfile.TemporaryDirectory() as tmp:
            path = os.path.join(tmp, "t.jsonl")

            def write(*lines):
                with open(path, "w", encoding="utf-8") as f:
                    f.write("\n".join(json.dumps(x) for x in lines) + '\n{"type": "system"}\n')

            write(tool_use)
            self.apply(ev("UserPromptSubmit", 10, prompt="go", transcript_path=path),
                       ev("PermissionRequest", 11, tool_name="Edit", tool_input={"file_path": "/x/a.py"}))
            self.assertFalse(self.st.check_interrupts(20))  # still waiting: the prompt is open
            write(tool_use, rejected, stop_line)
            self.assertFalse(self.st.check_interrupts(11.5))  # too fresh, the hooks may still be on their way
            self.assertTrue(self.st.check_interrupts(20))
            s = self.st.snapshot(now=20)["sessions"][0]
            self.assertEqual((s["status"], s["since"]), ("idle", 20))
            self.assertNotIn("action", s)

    def test_text_fits_the_board(self):
        self.assertEqual(state.clean("Привет 👋\nмир\tЁё «x» — №1 ↑", 80), "Привет мир Ёё «x» — №1 ↑")
        self.assertEqual(state.clean("a" * 50, 40), "a" * 39 + "…")
        snap = self.st.snapshot(now=1)
        self.assertEqual((snap["t"], snap["v"]), ("snap", 1))
        for i in range(25):
            self.st.event({"hook_event_name": "SessionStart", "session_id": f"s{i:02d}", "cwd": "/p/x"}, now=i)
        snap = self.st.snapshot(now=30)
        self.assertEqual((len(snap["sessions"]), snap["sessions_more"]), (20, 5))
        self.assertLess(len(json.dumps(snap, ensure_ascii=False).encode()), 32 * 1024)


if __name__ == "__main__":
    unittest.main()
