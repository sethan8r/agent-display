---
description: Flash the agent-display firmware onto the board — the first install of a new board, and updates
argument-hint: "[full]"
---

Flash the board with the firmware that comes with this plugin. Talk to the user in their language, simply: they may
know nothing about microcontrollers.

Python: use `py -3` on Windows; if that command is missing, use `python3`.
Script: `"${CLAUDE_PLUGIN_ROOT}/scripts/update_firmware.py"`.

1. Run the script with `status`. Tell the user in one or two lines what the board runs now and what the plugin brings.
   If no board is found: ask them to plug the board in with a micro-USB cable that carries data (some cables only
   charge), wait a few seconds, and check again. Windows normally installs the USB driver (CH340) by itself; if the
   board still doesn't show up, point them to the CH340 driver from WCH (wch-ic.com).
2. Ask for confirmation before flashing. Say: it takes about a minute, the screen may go dark meanwhile, the board
   restarts by itself, and settings and touch calibration are kept.
3. Run the script with `flash`; add `--full` if the argument is "full" (here: "$ARGUMENTS") or if `status` said the
   board firmware is unknown. Give the command a 5-minute timeout. Do not run it in the background.
4. On success: a new board first shows the touch calibration — tap the tip of the arrow in each corner, a stylus or a
   fingernail works best — then the agents screen. If `/agent-display:setup` hasn't been run yet, suggest it (it
   brings the context % and plan limits to the board).
5. On failure, read esptool's output and help:
   - "could not open port" / "Access is denied": another program holds the port (Arduino IDE, a serial monitor,
     PlatformIO): close it and run `flash` again.
   - "Failed to connect" / "Wrong boot mode": hold the BOOT button on the board, run `flash` again, and let go once
     "Connecting..." has passed.
   Running it again is always safe: the board can be reflashed any number of times.
