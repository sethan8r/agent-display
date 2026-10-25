#include "app/protocol.h"

namespace protocol {

static void limit(JsonVariantConst j, Limit& l) {
  if (!j.isNull()) l = Limit::of(j["pct"] | 0, j["reset"] | 0);
}

void parse(JsonVariantConst d, Snapshot& s) {
  s.v = d["v"] | 0;
  s.now = d["now"] | 0;
  s.tz = d["tz"] | 0;
  if (s.v != PROTO) return;  // another format: the UI shows "update the firmware / the plugin"
  limit(d["limits"]["5h"], s.h5);
  limit(d["limits"]["7d"], s.d7);
  s.sessions_more = d["sessions_more"] | 0;
  s.fw_update = d["fw_update"] | "";
  for (JsonVariantConst j : d["sessions"].as<JsonArrayConst>()) {
    Session x;
    x.id = j["id"] | "";
    x.name = j["name"] | "";
    x.status = j["status"] | "idle";
    x.wait_for = j["wait_for"] | "";
    x.icon = j["icon"] | "";
    x.action = j["action"] | "";
    x.model = j["model"] | "";
    x.dir = j["dir"] | "";
    x.summary = j["summary"] | "";
    x.since = j["since"] | 0;
    x.started = j["started"] | 0;
    x.ctx = j["ctx"] | -1;
    x.tokens = j["tokens"] | 0;
    x.tokens_max = j["tokens_max"] | 0;
    x.quiet = j["quiet"] | -1;
    x.subagents_more = j["subagents_more"] | 0;
    for (JsonVariantConst a : j["subagents"].as<JsonArrayConst>()) x.subagents.push_back({a["type"] | "", a["task"] | ""});
    s.sessions.push_back(std::move(x));
  }
}

}  // namespace protocol
