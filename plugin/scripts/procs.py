"""Which Claude Code process a session belongs to, and whether it is still running.

The Claude desktop app is also called claude.exe, so a session's process is never searched by name:
it is either an ancestor of a hook script, or the owner of the socket an HTTP hook connects from.
"""
import ctypes
import os
import socket
import subprocess
import sys

WIN = sys.platform == "win32"

if WIN:
    from ctypes import wintypes

    kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)
    kernel32.OpenProcess.restype = wintypes.HANDLE
    kernel32.CreateToolhelp32Snapshot.restype = wintypes.HANDLE

    class PROCESSENTRY32W(ctypes.Structure):
        _fields_ = [("dwSize", wintypes.DWORD), ("cntUsage", wintypes.DWORD), ("th32ProcessID", wintypes.DWORD),
                    ("th32DefaultHeapID", ctypes.c_void_p), ("th32ModuleID", wintypes.DWORD),
                    ("cntThreads", wintypes.DWORD), ("th32ParentProcessID", wintypes.DWORD),
                    ("pcPriClassBase", ctypes.c_long), ("dwFlags", wintypes.DWORD),
                    ("szExeFile", ctypes.c_wchar * 260)]


def _process_table():
    """pid -> (parent pid, executable name)"""
    table = {}
    if WIN:
        snap = kernel32.CreateToolhelp32Snapshot(0x2, 0)  # TH32CS_SNAPPROCESS
        if snap in (None, wintypes.HANDLE(-1).value):
            return table
        try:
            e = PROCESSENTRY32W()
            e.dwSize = ctypes.sizeof(e)
            ok = kernel32.Process32FirstW(snap, ctypes.byref(e))
            while ok:
                table[e.th32ProcessID] = (e.th32ParentProcessID, e.szExeFile)
                ok = kernel32.Process32NextW(snap, ctypes.byref(e))
        finally:
            kernel32.CloseHandle(snap)
        return table
    out = subprocess.run(["ps", "-A", "-o", "pid=,ppid=,comm="], capture_output=True, text=True).stdout
    for line in out.splitlines():
        parts = line.split(None, 2)
        if len(parts) == 3 and parts[0].isdigit() and parts[1].isdigit():
            table[int(parts[0])] = (int(parts[1]), os.path.basename(parts[2]))
    return table


def claude_ancestor():
    """(pid, creation time) of the nearest ancestor named claude*: the Claude Code session running this script."""
    table, pid, seen = _process_table(), os.getpid(), set()
    while pid in table and pid not in seen:
        seen.add(pid)
        parent, name = table[pid]
        if name.lower().startswith("claude"):
            return pid, created(pid)
        pid = parent
    return None, None


def created(pid):
    """Process creation time (Windows FILETIME), to tell a reused PID from the original process."""
    if not WIN:
        return None
    h = kernel32.OpenProcess(0x1000, False, pid)  # PROCESS_QUERY_LIMITED_INFORMATION
    if not h:
        return None
    try:
        times = [wintypes.FILETIME() for _ in range(4)]
        if not kernel32.GetProcessTimes(h, *[ctypes.byref(t) for t in times]):
            return None
        return times[0].dwHighDateTime << 32 | times[0].dwLowDateTime
    finally:
        kernel32.CloseHandle(h)


def alive(pid, when=None):
    if not WIN:
        try:
            os.kill(pid, 0)  # signal 0 only checks; never call this on Windows, where it sends CTRL+C
            return True
        except PermissionError:
            return True
        except OSError:
            return False
    h = kernel32.OpenProcess(0x1000 | 0x00100000, False, pid)  # QUERY_LIMITED_INFORMATION | SYNCHRONIZE
    if not h:
        return False
    try:
        if kernel32.WaitForSingleObject(h, 0) != 0x102:  # anything but WAIT_TIMEOUT: it has exited
            return False
    finally:
        kernel32.CloseHandle(h)
    return when is None or created(pid) == when


def tcp_client_pid(client_port, server_port):
    """PID owning the local TCP socket 127.0.0.1:client_port -> :server_port (the process behind an HTTP hook)."""
    if not WIN:
        return None  # shortcut: only Windows maps a socket to its process here; elsewhere sessions get the PID at SessionStart
    iphlpapi = ctypes.WinDLL("iphlpapi")
    size = wintypes.DWORD(0)
    iphlpapi.GetExtendedTcpTable(None, ctypes.byref(size), False, 2, 5, 0)  # AF_INET, TCP_TABLE_OWNER_PID_ALL
    buf = ctypes.create_string_buffer(size.value)
    if iphlpapi.GetExtendedTcpTable(buf, ctypes.byref(size), False, 2, 5, 0):
        return None
    count = wintypes.DWORD.from_buffer(buf).value
    rows = (wintypes.DWORD * 6 * count).from_buffer(buf, 4)  # state, local addr, local port, remote addr, remote port, pid
    for r in rows:
        if socket.ntohs(r[2] & 0xFFFF) == client_port and socket.ntohs(r[4] & 0xFFFF) == server_port:
            return r[5]
    return None
