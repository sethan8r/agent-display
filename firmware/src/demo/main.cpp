// Demo firmware: the real interface fed by a built-in simulation instead of the hub (same rules as design/index.html).
// Starts with a showcase: every status and icon on one main screen.
// Serial commands switch scenes: "scene showcase|live|empty|lost|many|mismatch|update", "ask", "reset".
#include <Arduino.h>
#include <esp_random.h>

#include "app/ui.h"
#include "board/cyd.h"

static cyd::LGFX lcd;

static constexpr uint32_t FAKE_EPOCH = 1761400000;  // fake clock, the board has none
static uint32_t now_s() { return FAKE_EPOCH + millis() / 1000; }

struct Pair { const char *a, *b; };
static const Pair NAMES[] = {{"agent-display", "VSCodeProjects/agent-display"}, {"Рефакторинг хаба", "agent-display/plugin"},
                             {"shop-backend", "work/shop-backend"}, {"Миграция БД на Postgres", "work/shop-backend"},
                             {"landing", "site/landing"}, {"Фикс тача XPT2046", "VSCodeProjects/agent-display"},
                             {"api-gateway", "work/api-gateway"}, {"Telegram-бот для заказов", "pets/order-bot"},
                             {"infra", "work/infra"}, {"ml-pipeline", "lab/ml-pipeline"},
                             {"Обновить зависимости", "work/shop-frontend"}, {"docs", "work/docs"}};
static const Pair ACTIONS[] = {{"edit", "Edit hub/serial.py"}, {"run", "Bash: pytest -q"}, {"read", "Read docs/protocol.md"},
                               {"find", "Grep: session_id"}, {"web", "WebFetch code.claude.com/docs/en/hooks"},
                               {"edit", "Write firmware/src/ui.cpp"}, {"run", "Bash: pio run -e cyd2usb"},
                               {"read", "Read plugin/hub/state.py"}, {"find", "Glob: **/*.tsx"},
                               {"run", "PowerShell: Get-Process claude"}, {"edit", "Edit README.ru.md"}};
static const char* PERMS[] = {"Bash: git push origin main", "Bash: rm -rf node_modules", "Edit .env.production",
                              "Bash: npm publish", "WebFetch github.com/espressif"};
static const char* QUESTIONS[] = {"Какую раскладку сделать по умолчанию?", "Удалить старые миграции или оставить?",
                                  "Пушить в main или сделать PR?"};
static const char* PLANS[] = {"План: хаб + хуки, 5 шагов", "План миграции на LVGL 9"};
static const Pair SUBTASKS[] = {{"Explore", "Найти конфиг роутера"}, {"Explore", "Где парсится session_id"},
                                {"general-purpose", "Проверить доки по хукам"}, {"Plan", "Спланировать прошивку"},
                                {"Explore", "Найти все вызовы Serial"}};
static const char* MODELS[] = {"Opus 5.5", "Opus 5.5", "Sonnet 5.5", "Haiku 5.5", "Fable 5.1"};

template <class T, size_t N> static const T& pick(const T (&a)[N]) { return a[random(N)]; }
static float rnd() { return random(10000) / 10000.0f; }

struct Sim {
  Session s;
  float ctx = -1;
};
static std::vector<Sim> sim;
static Limit h5, d7;
static uint32_t t_sim;

enum Scene { SHOWCASE, LIVE, EMPTY, LOST, MANY, MISMATCH, UPDATE };
static Scene scene = SHOWCASE;
static std::vector<Sim> fixed;  // showcase / many: built once, so ids stay put and details open

static Sim mk(const char* name, const char* dir, const char* status, int32_t since_ago, float ctx, int32_t started_ago = 1800) {
  Sim x;
  static uint32_t next_id = 0xd1a0c3f2;
  x.s.id = String(next_id++, HEX);
  x.s.name = name;
  x.s.dir = dir;
  x.s.status = status;
  x.s.since = t_sim - since_ago;
  x.s.started = t_sim - started_ago;
  x.s.model = "Opus 5.5";
  x.s.tokens_max = 200000;
  x.ctx = ctx;
  return x;
}

static void start_task(Sim& x) {
  const Pair& a = pick(ACTIONS);
  x.s.status = "work";
  x.s.since = t_sim;
  x.s.wait_for = "";
  x.s.icon = a.a;
  x.s.action = a.b;
  if (x.ctx < 0) x.ctx = 3;
}

static void ask(Sim& x, int kind = -1) {
  if (kind < 0) kind = random(4) == 0 ? 2 : random(3) == 0 ? 1 : 0;
  if (x.s.status != "work") start_task(x);
  x.s.status = "wait";
  x.s.icon = "ask";
  x.s.wait_for = kind == 0 ? "perm" : kind == 1 ? "question" : "plan";
  x.s.action = kind == 0 ? pick(PERMS) : kind == 1 ? pick(QUESTIONS) : pick(PLANS);
}

static void reset() {
  t_sim = now_s();
  sim.clear();
  Sim a = mk("agent-display", "VSCodeProjects/agent-display", "work", 260, 42, 5400);
  a.s.icon = "edit"; a.s.action = "Edit firmware/src/ui.cpp";
  Sim b = mk("Рефакторинг хаба", "agent-display/plugin", "work", 95, 67, 3000);
  ask(b, 0); b.s.action = "Bash: git push origin main"; b.s.since = t_sim - 95;
  Sim c = mk("shop-backend", "work/shop-backend", "subs", 410, 23);
  c.s.model = "Sonnet 5.5"; c.s.tokens_max = 1000000;
  c.s.subagents = {{"Explore", "Найти все вызовы оплаты"}, {"Explore", "Где парсится webhook"}, {"Plan", "План рефакторинга платежей"}};
  Sim d = mk("Миграция БД на Postgres", "work/shop-backend", "done", 720, 91, 9000);
  Sim e = mk("Telegram-бот для заказов", "pets/order-bot", "work", 150, 34);
  ask(e, 1); e.s.action = "Пушить в main или сделать PR?"; e.s.model = "Sonnet 5.5"; e.s.since = t_sim - 150;
  Sim f = mk("landing", "site/landing", "work", 40, 12, 60);
  f.s.icon = "run"; f.s.action = "Bash: npm run build"; f.s.model = "Haiku 5.5";
  sim = {a, b, c, d, e, f};
  h5 = Limit::of(42, t_sim + 2 * 3600 + 17 * 60);
  d7 = Limit::of(87, t_sim + 3 * 86400 + 5 * 3600);
}

static void step() {
  t_sim = now_s();
  for (auto& x : sim) {
    Session& s = x.s;
    float r = rnd();
    if (s.status == "work") {
      x.ctx += rnd() * 0.5f;
      if (x.ctx >= 97) { x.ctx = 18; s.action = "/compact"; s.icon = "other"; continue; }
      if (r < .03f) ask(x);
      else if (r < .06f) { s.status = s.subagents.empty() ? "done" : "subs"; s.since = t_sim; }
      else if (r < .09f && s.subagents.size() < 6) {
        const Pair& st = pick(SUBTASKS);
        s.subagents.push_back({st.a, st.b});
        s.icon = "agent";
        s.action = String("Agent: ") + st.b;
      } else if (r < .35f) { const Pair& a = pick(ACTIONS); s.icon = a.a; s.action = a.b; }
      if (!s.subagents.empty() && rnd() < .04f) s.subagents.erase(s.subagents.begin());
    } else if (s.status == "subs") {
      if (r < .12f) { s.subagents.erase(s.subagents.begin()); if (s.subagents.empty()) { s.status = "done"; s.since = t_sim; } }
    } else if (s.status == "wait") {
      if (r < .06f) { const Pair& a = pick(ACTIONS); s.status = "work"; s.wait_for = ""; s.icon = a.a; s.action = a.b; }
    } else if (s.status == "idle") { if (r < .05f) start_task(x); }
    else if (s.status == "done") { if (r < .015f) start_task(x); }
    else if (s.status == "error") { if (r < .05f) start_task(x); }
  }
  std::vector<size_t> quiet;
  for (size_t i = 0; i < sim.size(); i++)
    if (sim[i].s.status == "done" || sim[i].s.status == "idle") quiet.push_back(i);
  if (sim.size() > 3 && !quiet.empty() && rnd() < .015f) sim.erase(sim.begin() + quiet[random(quiet.size())]);
  if (sim.size() < 9 && rnd() < .02f) {
    const Pair& n = pick(NAMES);
    Sim x = mk(n.a, n.b, "idle", 0, -1, 0);
    x.s.model = pick(MODELS);
    sim.push_back(x);
  }
  if (rnd() < .004f) {
    for (auto& x : sim)
      if (x.s.status == "work") { x.s.status = "error"; x.s.since = t_sim; break; }
  }
  if (rnd() < .1f) h5.pct = min(100, h5.pct + 1);
}

static Session out(const Sim& x) {
  Session s = x.s;
  if (s.status != "work" && s.status != "wait") { s.icon = ""; s.action = ""; }
  s.ctx = x.ctx < 0 ? -1 : (int)x.ctx;
  s.tokens = x.ctx < 0 ? 0 : (uint32_t)(x.ctx / 100 * s.tokens_max);
  if (s.subagents.size() > 4) { s.subagents_more = s.subagents.size() - 4; s.subagents.resize(4); }
  return s;
}

static Snapshot snapshot() {
  Snapshot p;
  p.now = now_s();
  p.tz = 3 * 3600;
  p.h5 = h5;
  p.d7 = d7;
  if (scene == MISMATCH) p.v = PROTO + 1;
  if (scene == UPDATE) p.fw_update = "0.2.0";
  if (scene == EMPTY) return p;
  const std::vector<Sim>& src = scene == SHOWCASE || scene == MANY ? fixed : sim;
  for (size_t i = 0; i < src.size() && i < 20; i++) p.sessions.push_back(out(src[i]));
  return p;
}

static void build_many() {
  fixed.clear();
  for (int i = 0; i < 12; i++) {
    const Pair& n = NAMES[i];
    static const char* st[] = {"work", "done", "work", "idle", "subs", "work", "done", "error", "work", "done"};
    Sim x = mk(n.a, n.b, i == 10 ? "wait" : st[i % 10], 30 + i * 97, (i * 37) % 100);
    if (i == 10) { x.s.wait_for = "perm"; x.s.icon = "ask"; x.s.action = PERMS[0]; }
    else { x.s.icon = ACTIONS[i % 11].a; x.s.action = ACTIONS[i % 11].b; }
    if (x.s.status == "subs") x.s.subagents = {{"Explore", SUBTASKS[i % 5].b}};
    fixed.push_back(x);
  }
}

// every status, every wait reason, every tool icon, a long name, unknown context
static void build_showcase() {
  t_sim = now_s();
  fixed.clear();
  auto work = [](const char* name, const char* dir, int ago, float ctx, const char* ic, const char* act) {
    Sim x = mk(name, dir, "work", ago, ctx);
    x.s.icon = ic;
    x.s.action = act;
    return x;
  };
  auto wait = [](const char* name, const char* dir, int ago, float ctx, int kind, const char* act) {
    Sim x = mk(name, dir, "work", ago, ctx);
    ask(x, kind);
    x.s.action = act;
    x.s.since = t_sim - ago;
    return x;
  };
  fixed.push_back(work("agent-display", "VSCodeProjects/agent-display", 260, 42, "edit", "Edit firmware/src/ui.cpp"));
  fixed.push_back(wait("Рефакторинг хаба", "agent-display/plugin", 95, 67, 0, "Bash: git push origin main"));
  fixed.push_back(wait("Telegram-бот для заказов", "pets/order-bot", 150, 34, 1, "Пушить в main или сделать PR?"));
  fixed.push_back(wait("Переписать авторизацию на OAuth 2.1 с PKCE", "work/auth", 600, 95, 2,
                       "План: 7 шагов — токены, refresh, миграция пользователей, тесты"));
  Sim subs = mk("shop-backend", "work/shop-backend", "subs", 410, 23);
  subs.s.model = "Sonnet 5.5";
  subs.s.tokens_max = 1000000;
  subs.s.subagents = {{"Explore", "Найти все вызовы оплаты"}, {"Explore", "Где парсится webhook"},
                      {"Plan", "План рефакторинга платежей"}, {"general-purpose", "Проверить доки по хукам"},
                      {"Explore", "Найти все вызовы Serial"}};
  fixed.push_back(subs);
  fixed.push_back(work("landing", "site/landing", 40, 12, "run", "Bash: npm run build"));
  Sim docs = work("docs", "work/docs", 400, 55, "read", "Read docs/protocol.md");
  docs.s.quiet = 200;  // silent for a while: shows "quiet 3 min" on big text
  fixed.push_back(docs);
  fixed.push_back(work("api-gateway", "work/api-gateway", 520, 74, "find", "Grep: session_id"));
  fixed.push_back(work("ml-pipeline", "lab/ml-pipeline", 1300, 30, "web", "WebFetch code.claude.com/docs/en/hooks"));
  Sim agent = work("infra", "work/infra", 3700, 48, "agent", "Agent: Найти конфиг роутера");
  agent.s.subagents = {{"Explore", "Найти конфиг роутера"}};
  fixed.push_back(agent);
  fixed.push_back(work("Обновить зависимости", "work/shop-frontend", 20, -1, "other", "/compact"));
  Sim done = mk("Миграция БД на Postgres", "work/shop-backend", "done", 720, 91, 9000);
  done.s.summary = "Перенёс 14 таблиц на Postgres, все тесты зелёные";
  fixed.push_back(done);
  fixed.push_back(mk("Фикс тача XPT2046", "VSCodeProjects/agent-display", "error", 45, 61));
  fixed.push_back(mk("Новая сессия", "VSCodeProjects/new", "idle", 0, -1, 30));
}

static void command(String c) {
  c.trim();
  if (c.startsWith("scene ")) {
    String n = c.substring(6);
    scene = n == "showcase" ? SHOWCASE : n == "empty" ? EMPTY : n == "lost" ? LOST : n == "many" ? MANY
          : n == "mismatch" ? MISMATCH : n == "update" ? UPDATE : LIVE;
    if (scene == SHOWCASE) build_showcase();
    if (scene == MANY) build_many();
  } else if (c == "ask") {
    if (!sim.empty()) ask(sim[random(sim.size())], 0);
  } else if (c == "reset") reset();
  else { Serial.printf("err %s\n", c.c_str()); return; }
  Serial.printf("ok %s\n", c.c_str());
}

void setup() {
  Serial.begin(921600);
  for (int pin : cyd::PIN_LED) { pinMode(pin, OUTPUT); digitalWrite(pin, HIGH); }  // RGB LED off
  randomSeed(esp_random());
  ui::begin(lcd);
  reset();
  build_showcase();
  Serial.println("hello demo");
}

void loop() {
  static uint32_t last = 0, last_heap = 0;
  static String line;
  while (Serial.available()) {
    char ch = Serial.read();
    if (ch == '\n') { command(line); line = ""; }
    else if (ch != '\r' && line.length() < 80) line += ch;
  }
  if (millis() - last >= 1000) {
    last = millis();
    if (scene == LIVE) step();
    if (scene != LOST) ui::show(snapshot());
  }
  if (millis() - last_heap >= 10000) {
    last_heap = millis();
    Serial.printf("heap %lu min %lu\n", (unsigned long)ESP.getFreeHeap(), (unsigned long)ESP.getMinFreeHeap());
  }
  ui::loop();
  delay(2);
}
