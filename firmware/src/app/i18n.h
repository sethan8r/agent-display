#pragma once
// All interface text lives on the board (the hub sends data only), so the language is just a board setting.
#include <string.h>

namespace ui {

struct Strings {
  const char *agents, *back, *settings, *theme, *lang, *size, *layout, *large, *compact, *grid, *recal, *calib_saved;
  const char *fw_avail, *fw, *proto, *ctx, *nodata, *subagents, *more, *opened, *lim_no, *h5, *wk, *reset, *quiet;
  const char *no_agents, *hub_lost, *hub_lost_sub, *lim_stale, *plug_newer, *fw_newer, *upd_fw, *upd_plug, *in_cc;
  const char *calib_t, *calib_s, *calib_go, *thinking, *wait_subs, *ago;  // ago: printf format, %s = duration
  const char* u[8];   // " s", " min", " h", " d", then the short forms
  const char* wd[7];  // weekdays, Sunday first
  const char* st[6][2];  // idle, work, subs, wait, done, error: label, short
  const char* wf[3][2];  // perm, question, plan: label, short
  const char* phrases[16];
};

inline const Strings& ru() {
  static const Strings s = [] {
    Strings s{};
    s.agents = "Агенты"; s.back = "Назад"; s.settings = "Настройки"; s.theme = "Тема"; s.lang = "Язык";
    s.size = "Размер текста"; s.layout = "Раскладка"; s.large = "Крупно"; s.compact = "Компактно"; s.grid = "Сетка";
    s.recal = "Перекалибровать экран"; s.calib_saved = "Калибровка сохранена"; s.fw_avail = "Есть прошивка";
    s.fw = "Прошивка"; s.proto = "протокол"; s.ctx = "Контекст"; s.nodata = "нет данных"; s.subagents = "Субагенты";
    s.more = "ещё"; s.opened = "открыта"; s.lim_no = "Лимиты: нет данных"; s.h5 = "5ч"; s.wk = "Нед"; s.reset = "сброшен"; s.quiet = "тишина";
    s.no_agents = "Агентов нет"; s.hub_lost = "Хаб не подключён";
    s.hub_lost_sub = "Комп выключен или Claude Code работает без плагина agent-display";
    s.lim_stale = "Лимиты внизу давно не обновлялись"; s.plug_newer = "Плагин новее прошивки";
    s.fw_newer = "Прошивка новее плагина"; s.upd_fw = "Обнови прошивку"; s.upd_plug = "Обнови плагин";
    s.in_cc = "— в Claude Code:"; s.calib_t = "Калибровка экрана";
    s.calib_s = "В углах по очереди появятся стрелки. Нажимай стилусом точно в кончик каждой.";
    s.calib_go = "Нажми, чтобы начать"; s.thinking = "Думает…"; s.wait_subs = "Ждёт субагентов"; s.ago = "%s назад";
    const char* u[8] = {" с", " мин", " ч", " д", "с", "м", "ч", "д"};
    const char* wd[7] = {"вс", "пн", "вт", "ср", "чт", "пт", "сб"};
    const char* st[6][2] = {{"Ждёт задачу", "без задачи"}, {"Работает", "работает"}, {"Ждёт субагентов", "субагенты"},
                            {"Ждёт тебя", "ждёт"}, {"Готов", "готов"}, {"Ошибка API", "ошибка"}};
    const char* wf[3][2] = {{"Просит разрешение", "разрешение"}, {"Задал вопрос", "вопрос"}, {"Ждёт одобрения плана", "план"}};
    const char* ph[16] = {"Не пора ли нам повайбкодить?", "Агенты отдыхают. Твой ход.", "Тишина. Подозрительно тихо.",
                          "Claude скучает без задач", "Ни одного агента. Кофе?", "Самое время для нового промта",
                          "Все ушли на перекур", "Код сам себя не напишет. Или напишет?", "Клодик хочет работать",
                          "Хватит смотреть рилсы, когда есть свободные токены", "Токены сами себя не потратят",
                          "Лимиты скучают без дела", "Агенты ждут. А ты чего ждёшь?", "Пора бы что-нибудь задеплоить",
                          "Клод выспался и готов к багам", "Пустой экран — отличный повод для промта"};
    memcpy(s.u, u, sizeof u); memcpy(s.wd, wd, sizeof wd); memcpy(s.st, st, sizeof st);
    memcpy(s.wf, wf, sizeof wf); memcpy(s.phrases, ph, sizeof ph);
    return s;
  }();
  return s;
}

inline const Strings& en() {
  static const Strings s = [] {
    Strings s{};
    s.agents = "Agents"; s.back = "Back"; s.settings = "Settings"; s.theme = "Theme"; s.lang = "Language";
    s.size = "Text size"; s.layout = "Layout"; s.large = "Large"; s.compact = "Compact"; s.grid = "Grid";
    s.recal = "Recalibrate touch"; s.calib_saved = "Calibration saved"; s.fw_avail = "Firmware available";
    s.fw = "Firmware"; s.proto = "protocol"; s.ctx = "Context"; s.nodata = "no data"; s.subagents = "Subagents";
    s.more = "more"; s.opened = "open"; s.lim_no = "Limits: no data"; s.h5 = "5h"; s.wk = "Wk"; s.reset = "reset"; s.quiet = "quiet";
    s.no_agents = "No agents"; s.hub_lost = "Hub not connected";
    s.hub_lost_sub = "PC is off or Claude Code runs without the agent-display plugin";
    s.lim_stale = "Limits below are out of date"; s.plug_newer = "Plugin is newer than firmware";
    s.fw_newer = "Firmware is newer than plugin"; s.upd_fw = "Update the firmware"; s.upd_plug = "Update the plugin";
    s.in_cc = "in Claude Code:"; s.calib_t = "Touch calibration";
    s.calib_s = "Arrows will appear in the corners one by one. Tap the exact tip of each with a stylus.";
    s.calib_go = "Tap to start"; s.thinking = "Thinking…"; s.wait_subs = "Waiting for subagents"; s.ago = "%s ago";
    const char* u[8] = {" s", " min", " h", " d", "s", "m", "h", "d"};
    const char* wd[7] = {"Su", "Mo", "Tu", "We", "Th", "Fr", "Sa"};
    const char* st[6][2] = {{"Waiting for a task", "idle"}, {"Working", "working"}, {"Waiting for subagents", "subagents"},
                            {"Waiting for you", "waiting"}, {"Done", "done"}, {"API error", "error"}};
    const char* wf[3][2] = {{"Asks permission", "permission"}, {"Asked a question", "question"},
                            {"Waiting for plan approval", "plan"}};
    const char* ph[16] = {"Time to vibe-code?", "Agents are resting. Your move.", "Quiet. Suspiciously quiet.",
                          "Claude is bored without tasks", "No agents. Coffee?", "Perfect time for a new prompt",
                          "Everyone is on a break", "Code won't write itself. Or will it?", "Claudie wants to work",
                          "Stop watching reels, you have free tokens", "Tokens won't spend themselves",
                          "Your limits are bored", "Agents are waiting. What are you waiting for?", "Time to ship something",
                          "Claude is rested and ready for bugs", "An empty screen is a great reason to prompt"};
    memcpy(s.u, u, sizeof u); memcpy(s.wd, wd, sizeof wd); memcpy(s.st, st, sizeof st);
    memcpy(s.wf, wf, sizeof wf); memcpy(s.phrases, ph, sizeof ph);
    return s;
  }();
  return s;
}

}  // namespace ui
