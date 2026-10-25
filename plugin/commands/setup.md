---
description: Connect the status line to agent-display (keeps your own status line), or remove it with "remove"
argument-hint: "[remove]"
---

Set up the agent-display status line wrapper. Talk to the user in their language.

What it is, for the user: only Claude Code's status line receives the context window % and the 5-hour / weekly plan
limits. The wrapper sits in front of the user's own status line: the user keeps seeing exactly the same status line
as now, and the board also gets this data. Without it the board still works, but shows no plan limits.

Python: use `py -3` on Windows; if that command is missing, use `python3`.

1. Run `py -3 "${CLAUDE_PLUGIN_ROOT}/scripts/statusline_setup.py" status` and read the result.
2. If the argument is `remove` (here: "$ARGUMENTS"): if it is installed, ask the user to confirm, then run the same
   script with `uninstall` instead of `status` and report what was restored. Stop here.
3. Otherwise, if it is not installed yet: explain the wrapper in 2–3 sentences, say which status line command they
   have now (from step 1) and that it stays as is, that `~/.claude/settings.json` gets a backup first, and that
   `/agent-display:setup remove` undoes it. Ask for confirmation. Only after a clear yes run it with `install`.
4. Report the result in one or two lines. No restart is needed: the status line picks it up on its next update.
