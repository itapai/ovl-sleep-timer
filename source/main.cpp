#define TESLA_INIT_IMPL
#include <exception_wrap.hpp>
#include <tesla.hpp>

#include <cstdio>
#include <functional>
#include <iterator>
#include <string>

#include "sleep.hpp"
#include "timer.hpp"

namespace {

    constexpr const char *ConfigDir     = "sdmc:/config/ovl-sleep-timer/";
    constexpr const char *ConfigPath    = "sdmc:/config/ovl-sleep-timer/config.ini";
    constexpr const char *ConfigSection = "settings";

    constexpr u32 Presets[] = { 30, 60, 90, 120, 180 };
    // Stops for -/+: 1-min steps up to 10 min, then 5, 15, 30 and 60 min
    // steps, so short timers are precise and long ones quick to reach.
    constexpr u32 Stops[] = {
        1, 2, 3, 4, 5, 6, 7, 8, 9, 10,
        15, 20, 25, 30,
        45, 60, 75, 90, 105, 120,
        150, 180, 210, 240,
        300, 360, 420, 480, 540, 600,
    };
    // + adds at least this much while running, so a press always counts.
    constexpr u64 MinGainSeconds = 30;
    constexpr u32 MinMinutes = 1;
    constexpr u32 MaxMinutes = 600;
    constexpr u32 DefaultMinutes = 90;
    constexpr u64 WarningSeconds = 60;
    constexpr u64 MinRemainingSeconds = 60;

    struct Config {
        u32 durationMinutes = DefaultMinutes;
        bool warning = true;
    };

    Config g_config;
    bool g_configDirty = false;
    SleepTimer g_timer;

    // Notifications from showNow always play the notification sound, so the
    // countdown silences them from the start of the warning until the timer
    // ends. Ultrahand reads this setting only at startup.
    std::atomic<bool> g_silencing{false};
    bool g_savedSilence = false;

    void beginQuietCountdown() {
        if (!g_silencing.exchange(true, std::memory_order_acq_rel)) {
            g_savedSilence = ult::silenceNotifications;
            ult::silenceNotifications = true;
        }
    }

    // Undoes what the warning changed (the notification sound setting).
    void endWarningEffects() {
        if (g_silencing.exchange(false, std::memory_order_acq_rel))
            ult::silenceNotifications = g_savedSilence;
    }

    u32 clampMinutes(long minutes) {
        if (minutes < static_cast<long>(MinMinutes)) return MinMinutes;
        if (minutes > static_cast<long>(MaxMinutes)) return MaxMinutes;
        return static_cast<u32>(minutes);
    }

    // Next stop above or below a duration; an odd value from config.ini
    // snaps onto the stops.
    u32 steppedMinutes(u32 minutes, bool up) {
        if (up) {
            for (const u32 stop : Stops)
                if (stop > minutes) return stop;
            return MaxMinutes;
        }
        for (auto it = std::rbegin(Stops); it != std::rend(Stops); ++it)
            if (*it < minutes) return *it;
        return MinMinutes;
    }

    // Time left after -/+ while running: the next stop at least
    // MinGainSeconds away, so 43:27 becomes 45:00 or 30:00.
    u64 steppedRemaining(u64 seconds, bool up) {
        if (up) {
            for (const u32 stop : Stops)
                if (u64(stop) * 60 >= seconds + MinGainSeconds) return u64(stop) * 60;
            return seconds;
        }
        for (auto it = std::rbegin(Stops); it != std::rend(Stops); ++it)
            if (u64(*it) * 60 + MinGainSeconds <= seconds) return u64(*it) * 60;
        return MinRemainingSeconds;
    }

    void loadConfig() {
        const std::string duration = ult::parseValueFromIniSection(ConfigPath, ConfigSection, "duration_minutes");
        if (!duration.empty())
            g_config.durationMinutes = clampMinutes(std::strtol(duration.c_str(), nullptr, 10));

        const std::string warning = ult::parseValueFromIniSection(ConfigPath, ConfigSection, "warning");
        if (!warning.empty())
            g_config.warning = !(warning == "0" || warning == "false");
    }

    // Written only on start and on exit, not on every press.
    void saveConfig() {
        if (!g_configDirty)
            return;
        ult::createDirectory(ConfigDir);
        ult::setIniFileValue(ConfigPath, ConfigSection, "duration_minutes", std::to_string(g_config.durationMinutes));
        ult::setIniFileValue(ConfigPath, ConfigSection, "warning", g_config.warning ? "true" : "false");
        g_configDirty = false;
    }

    void setDuration(u32 minutes) {
        minutes = clampMinutes(minutes);
        if (minutes != g_config.durationMinutes) {
            g_config.durationMinutes = minutes;
            g_configDirty = true;
        }
    }

    void startTimer() {
        g_timer.start(u64(g_config.durationMinutes) * 60, g_config.warning ? WarningSeconds : 0);
        endWarningEffects(); // from a previous run that was restarted in its last minute
        saveConfig();
    }

    void stopTimer() {
        const bool inWarning = g_timer.state() == SleepTimer::State::Warning;
        g_timer.cancel();
        endWarningEffects();
        if (inWarning && tsl::notification)
            tsl::notification->dismissFront(); // the countdown toast
    }

    // "1:23:45", or "23:45" under an hour.
    std::string formatRemaining(u64 seconds) {
        char buf[24];
        if (seconds >= 3600)
            std::snprintf(buf, sizeof(buf), "%lu:%02lu:%02lu", seconds / 3600, (seconds / 60) % 60, seconds % 60);
        else
            std::snprintf(buf, sizeof(buf), "%lu:%02lu", seconds / 60, seconds % 60);
        return buf;
    }

    // "45 min", "1 h", "1 h 30 min".
    std::string formatMinutes(u32 minutes) {
        char buf[24];
        if (minutes < 60)
            std::snprintf(buf, sizeof(buf), "%u min", minutes);
        else if (minutes % 60 == 0)
            std::snprintf(buf, sizeof(buf), "%u h", minutes / 60);
        else
            std::snprintf(buf, sizeof(buf), "%u h %u min", minutes / 60, minutes % 60);
        return buf;
    }

    std::string formatResult(Result rc) {
        char buf[16];
        std::snprintf(buf, sizeof(buf), "%04u-%04u", 2000 + R_MODULE(rc), R_DESCRIPTION(rc));
        return buf;
    }

    // Local wall-clock time secondsFromNow ahead, e.g. "23:47"; empty if the
    // clock can't be read.
    std::string clockTimeIn(u64 secondsFromNow) {
        u64 now = 0;
        TimeCalendarTime cal;
        TimeCalendarAdditionalInfo info;
        if (R_FAILED(timeGetCurrentTime(TimeType_Default, &now)) ||
            R_FAILED(timeToCalendarTimeWithMyRule(now + secondsFromNow, &cal, &info)))
            return {};

        char buf[8];
        std::snprintf(buf, sizeof(buf), "%02u:%02u", cal.hour, cal.minute);
        return buf;
    }

    void configureTimer() {
        g_timer.setCallbacks({
            .onWarning = [] {
                beginQuietCountdown();
            },
            .onWarningTick = [](u64 remaining) {
                // showNow replaces its own previous toast in place, so this reads
                // as one notification counting down rather than a stack of them.
                if (tsl::notification) {
                    char text[32];
                    std::snprintf(text, sizeof(text), "Sleeping in %lu:%02lu", remaining / 60, remaining % 60);
                    tsl::notification->showNow(text, 24, "Sleep Timer", 1500, false);
                }
            },
            .onExpire = [] {
                endWarningEffects();
                return power::putSystemToSleep();
            },
        });
    }

}

// List item where D-pad left/right act as -/+, repeating while held.
class AdjustableItem : public tsl::elm::ListItem {
public:
    using Adjust = std::function<void(bool up)>;

    AdjustableItem(const std::string &text, Adjust adjust)
        : ListItem(text), m_adjust(std::move(adjust)) {}

    bool handleInput(u64 keysDown, u64 keysHeld, const HidTouchState &, HidAnalogStickState, HidAnalogStickState) override {
        // KEY_LEFT/KEY_RIGHT include the stick when stick navigation is on.
        const u64 left = KEY_LEFT, right = KEY_RIGHT;
        constexpr u64 RepeatDelayNs = 400'000'000ULL;
        constexpr u64 RepeatIntervalNs = 120'000'000ULL;

        const u64 now = ult::nowNs();
        const u64 held = keysHeld & ALL_KEYS_MASK;
        if (keysDown & (left | right)) {
            m_adjust(keysDown & right);
            m_pressedAt = m_repeatedAt = now;
            return true;
        }
        // Only a lone left or right repeats; anything else held stops it.
        const bool onlyLeft = (held & left) && !(held & ~left);
        const bool onlyRight = (held & right) && !(held & ~right);
        if (onlyLeft || onlyRight) {
            if (now - m_pressedAt >= RepeatDelayNs && now - m_repeatedAt >= RepeatIntervalNs) {
                m_adjust(onlyRight);
                m_repeatedAt = now;
            }
            return true;
        }
        return false;
    }

private:
    Adjust m_adjust;
    u64 m_pressedAt = 0;
    u64 m_repeatedAt = 0;
};

class GuiUnsupported : public tsl::Gui {
public:
    tsl::elm::Element *createUI() override {
        auto *frame = new tsl::elm::OverlayFrame(APP_TITLE, APP_VERSION);
        auto *list = new tsl::elm::List();
        list->addItem(new tsl::elm::CategoryHeader("Unavailable"));
        list->addItem(new tsl::elm::ListItem("Requires firmware 15.0.0+"));
        frame->setContent(list);
        return frame;
    }
};

class GuiMain : public tsl::Gui {
public:
    tsl::elm::Element *createUI() override {
        auto *frame = new tsl::elm::OverlayFrame(APP_TITLE, APP_VERSION);
        auto *list = new tsl::elm::List();

        // The header's value says what the timer is doing ("Sleeps at 23:47").
        m_header = new tsl::elm::CategoryHeader("Timer");
        list->addItem(m_header);

        // One row for the timer: A starts or stops it, left/right (-/+) change the
        // duration, or the time left while it runs.
        // Note: libtesla calls click listeners every frame with that frame's
        // keysDown, which is usually 0. Act only on an actual A press.
        m_main = new AdjustableItem("Start", [this](bool up) { adjust(up); });
        m_main->setClickListener([this](u64 keys) {
            if (!(keys & KEY_A)) return false;
            if (g_timer.active())
                stopTimer();
            else
                startTimer();
            refresh();
            return true;
        });
        list->addItem(m_main);

        // Presets start right away; while a timer runs they restart it.
        list->addItem(new tsl::elm::CategoryHeader("Quick start"));
        for (const u32 minutes : Presets) {
            auto *preset = new tsl::elm::ListItem(formatMinutes(minutes));
            preset->setClickListener([this, minutes](u64 keys) {
                if (!(keys & KEY_A)) return false;
                setDuration(minutes);
                startTimer();
                refresh();
                return true;
            });
            list->addItem(preset);
        }

        frame->setContent(list);
        refresh();
        return frame;
    }

    void update() override {
        refresh();
    }

    bool handleInput(u64 keysDown, u64, const HidTouchState &, HidAnalogStickState, HidAnalogStickState) override {
        // Leaving the root menu returns to Ultrahand, which unloads this overlay
        // and the timer with it. While a timer runs, B only hides the overlay.
        if ((keysDown & KEY_B) && g_timer.active()) {
            tsl::Overlay::get()->hide();
            return true;
        }
        return false;
    }

private:
    void adjust(bool up) {
        if (g_timer.active()) {
            const u64 remaining = g_timer.remaining();
            const u64 target = steppedRemaining(remaining, up);
            // In the last minute, - would otherwise lengthen the timer to 1:00.
            if (up ? target > remaining : target < remaining)
                g_timer.adjust(s64(target) - s64(remaining), MinRemainingSeconds);
        } else {
            setDuration(steppedMinutes(g_config.durationMinutes, up));
        }
        refresh();
    }

    void refresh() {
        const SleepTimer::State state = g_timer.state();
        const bool running = state == SleepTimer::State::Running || state == SleepTimer::State::Warning;
        const u64 remaining = running ? g_timer.remaining() : 0;

        std::string header;
        std::string label = "Start";
        std::string value;
        switch (state) {
            case SleepTimer::State::Running:
            case SleepTimer::State::Warning: {
                label = "Stop";
                value = formatRemaining(remaining);
                const std::string at = clockTimeIn(remaining);
                header = at.empty() ? "Running" : "Sleeps at " + at;
                break;
            }
            case SleepTimer::State::Firing:
                label = "Stop";
                value = "0:00";
                header = "Sleeping…";
                break;
            case SleepTimer::State::Failed:
                header = "Sleep failed: " + formatResult(g_timer.lastResult());
                break;
            case SleepTimer::State::Missed:
                header = "Expired during sleep";
                break;
            default:
                header = "Off";
                break;
        }
        if (!running && state != SleepTimer::State::Firing)
            value = formatMinutes(g_config.durationMinutes);

        if (header != m_headerText) {
            m_headerText = header;
            m_header->setValue(header);
        }
        if (label != m_labelText) {
            m_labelText = label;
            m_main->setText(label);
        }
        value = "\u2212 " + value + " +";
        if (value != m_valueText) {
            m_valueText = value;
            m_main->setValue(value);
        }
    }

    tsl::elm::CategoryHeader *m_header = nullptr;
    AdjustableItem *m_main = nullptr;
    std::string m_headerText;
    std::string m_labelText = "Start";
    std::string m_valueText;
};

class SleepTimerOverlay : public tsl::Overlay {
public:
    void initServices() override {
        // Steady clock for the countdown and the time zone for "Sleeps at";
        // libtesla only opens time briefly at boot.
        m_timeInitialized = R_SUCCEEDED(timeInitialize());
        loadConfig();
        configureTimer();
    }

    void exitServices() override {
        g_timer.shutdown();
        endWarningEffects();
        saveConfig();
        if (m_timeInitialized)
            timeExit();
    }

    std::unique_ptr<tsl::Gui> loadInitialGui() override {
        if (!power::isSupported())
            return initially<GuiUnsupported>();
        return initially<GuiMain>();
    }

private:
    bool m_timeInitialized = false;
};

int main(int argc, char **argv) {
    return tsl::loop<SleepTimerOverlay>(argc, argv);
}
