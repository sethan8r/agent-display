#pragma once
// One snapshot from the hub, see docs/protocol.md
#include <Arduino.h>

#include <vector>

constexpr int PROTO = 1;
constexpr const char* FW_VERSION = "1.0.0";

struct Subagent {
  String type, task;
};

struct Session {
  String id, name, status, wait_for, icon, action, model, dir;
  uint32_t since = 0, started = 0;
  int ctx = -1;  // -1: unknown
  uint32_t tokens = 0, tokens_max = 0;
  std::vector<Subagent> subagents;
  int subagents_more = 0;
  String summary;  // done: first line of Claude's last answer
  int quiet = -1;  // working: seconds since the last hook event, -1 unknown
};

struct Limit {
  bool has = false;
  int pct = 0;
  uint32_t reset = 0;
  static Limit of(int pct, uint32_t reset) {
    Limit l;
    l.has = true;
    l.pct = pct;
    l.reset = reset;
    return l;
  }
};

struct Snapshot {
  int v = PROTO;
  uint32_t now = 0;
  int32_t tz = 0;
  Limit h5, d7;
  std::vector<Session> sessions;
  int sessions_more = 0;
  String fw_update;
};
