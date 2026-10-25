#define TESLA_INIT_IMPL
#include <exception_wrap.hpp>
#include <tesla.hpp>

#include <cstdio>
#include <ctime>
#include <mutex>
#include <string>

#include "sleep.hpp"
#include "timer.hpp"

namespace {

    constexpr const char *ConfigDir     = "sdmc:/config/ovl-sleep-timer/";
    constexpr const char *ConfigPath    = "sdmc:/config/ovl-sleep-timer/config.ini";
    constexpr const char *ConfigSection = "settings";

    constexpr u32 Presets[] = { 30, 60, 90, 120, 180 };
    constexpr u32 StepMinutes = 5;
    constexpr u32 MinMinutes = 5;
    constexpr u32 MaxMinutes = 600;
    constexpr u32 DefaultMinutes = 90;
    constexpr u64 WarningSeconds = 60;
    constexpr u64 MinRemainingSeconds = 60;

    struct Config {
        u32 durationMinutes = DefaultMinutes;
        bool warning = true;
    };

    Config g_config;
    SleepTimer g_timer;
    constexpr const char *LogPath = "sdmc:/config/ovl-sleep-timer/sleep.log";

    // Written by the timer thread on expiry, read by the UI.
    std::mutex g_reportMutex;
    power::SleepReport g_lastReport;
    bool g_haveReport = false;

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

    void loadConfig() {
        const std::string duration = ult::parseValueFromIniSection(ConfigPath, ConfigSection, "duration_minutes");
        if (!duration.empty())
            g_config.durationMinutes = clampMinutes(std::strtol(duration.c_str(), nullptr, 10));

        const std::string warning = ult::parseValueFromIniSection(ConfigPath, ConfigSection, "warning");
        if (!warning.empty())
            g_config.warning = !(warning == "0" || warning == "false");
    }

    void saveConfig() {
        ult::createDirectory(ConfigDir);
        ult::setIniFileValue(ConfigPath, ConfigSection, "duration_minutes", std::to_string(g_config.durationMinutes));
        ult::setIniFileValue(ConfigPath, ConfigSection, "warning", g_config.warning ? "true" : "false");
    }

    std::string formatDuration(u64 seconds) {
        char buf[16];
        std::snprintf(buf, sizeof(buf), "%lu:%02lu:%02lu", seconds / 3600, (seconds / 60) % 60, seconds % 60);
        return buf;
    }

    std::string formatResult(Result rc) {
        char buf[48];
        std::snprintf(buf, sizeof(buf), "%04u-%04u (0x%X)", 2000 + R_MODULE(rc), R_DESCRIPTION(rc), rc);
        return buf;
    }

    // Short form for the overlay, e.g. "slept 12 min".
    std::string describeReport(const power::SleepReport &report) {
        if (R_FAILED(report.result))
            return "error " + formatResult(report.result);
        if (!report.slept)
            return "no sleep seen";

        char buf[32];
        if (report.sleptSeconds < 60)
            std::snprintf(buf, sizeof(buf), "slept %lu s", report.sleptSeconds);
        else if (report.sleptSeconds < 3600)
            std::snprintf(buf, sizeof(buf), "slept %lu min", report.sleptSeconds / 60);
        else
            std::snprintf(buf, sizeof(buf), "slept %lu h %lu min", report.sleptSeconds / 3600, (report.sleptSeconds / 60) % 60);
        return buf;
    }

    // One line per sleep attempt, so results survive the overlay being closed.
    void appendLog(const power::SleepReport &report) {
        ult::createDirectory(ConfigDir);
        FILE *file = std::fopen(LogPath, "a");
        if (!file)
            return;

        char stamp[32] = "?";
        const time_t now = time(nullptr);
        if (const tm *local = localtime(&now))
            std::strftime(stamp, sizeof(stamp), "%Y-%m-%d %H:%M:%S", local);

        std::fprintf(file, "%s rc=0x%X slept=%s seconds=%lu\n", stamp, report.result,
                     report.slept ? "yes" : "no", report.sleptSeconds);
        std::fclose(file);
    }

    std::string minutesLabel(u32 minutes) {
        return std::to_string(minutes) + " min";
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
                power::SleepReport report;
                const Result rc = power::putSystemToSleep(&report);
                appendLog(report);
                {
                    std::lock_guard lock(g_reportMutex);
                    g_lastReport = report;
                    g_haveReport = true;
                }
                return rc;
            },
        });
    }

}

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

        list->addItem(new tsl::elm::CategoryHeader("Status"));
        m_status = new tsl::elm::ListItem("Remaining", "");
        m_status->setClickListener([](u64) { return consumeWarning(); });
        list->addItem(m_status);

        // How the last sleep attempt went, e.g. "slept 12 min"; also appended
        // to sleep.log.
        m_last = new tsl::elm::ListItem("Last sleep", "");
        m_last->setClickListener([](u64) { return consumeWarning(); });
        list->addItem(m_last);

        m_start = new tsl::elm::ListItem("Start", minutesLabel(g_config.durationMinutes));
        m_start->setClickListener([](u64 keys) {
            if (consumeWarning()) return true;
            if (!(keys & KEY_A)) return false;
            saveConfig();
            g_timer.start(u64(g_config.durationMinutes) * 60, g_config.warning ? WarningSeconds : 0);
            endWarningEffects(); // from a previous run that was snoozed
            return true;
        });
        list->addItem(m_start);

        auto *cancel = new tsl::elm::ListItem("Cancel");
        cancel->setClickListener([](u64 keys) {
            if (consumeWarning()) return true;
            if (!(keys & KEY_A)) return false;
            g_timer.cancel();
            endWarningEffects();
            return true;
        });
        list->addItem(cancel);

        // While a timer runs these move its deadline (+5 during the warning acts
        // as a snooze); otherwise they change the duration used by Start.
        auto *plus = new tsl::elm::ListItem("+" + minutesLabel(StepMinutes));
        plus->setClickListener([this](u64 keys) {
            if (!(keys & KEY_A)) return consumeWarning();
            step(+1);
            return true;
        });
        list->addItem(plus);

        auto *minus = new tsl::elm::ListItem("−" + minutesLabel(StepMinutes));
        minus->setClickListener([this](u64 keys) {
            if (!(keys & KEY_A)) return consumeWarning();
            step(-1);
            return true;
        });
        list->addItem(minus);

        list->addItem(new tsl::elm::CategoryHeader("Duration"));
        for (const u32 minutes : Presets) {
            auto *preset = new tsl::elm::ListItem(minutesLabel(minutes));
            preset->setClickListener([this, minutes](u64 keys) {
                if (consumeWarning()) return true;
                if (!(keys & KEY_A)) return false;
                setDuration(minutes);
                return true;
            });
            list->addItem(preset);
        }

        frame->setContent(list);
        refreshStatus();
        return frame;
    }

    void update() override {
        refreshStatus();
    }

    bool handleInput(u64 keysDown, u64, const HidTouchState &, HidAnalogStickState, HidAnalogStickState) override {
        // A is left to the focused item's listener, which runs first; otherwise a
        // +5 snooze during the warning would be cancelled in the same frame.
        if ((keysDown & ALL_KEYS_MASK & ~KEY_A) && consumeWarning())
            return true;

        // Leaving the root menu returns to Ultrahand, which unloads this overlay
        // and the timer with it. While a timer runs, B only hides the overlay.
        if ((keysDown & KEY_B) && g_timer.active()) {
            tsl::Overlay::get()->hide();
            return true;
        }
        return false;
    }

private:
    // Any input during the warning window cancels the timer.
    static bool consumeWarning() {
        if (g_timer.state() != SleepTimer::State::Warning)
            return false;
        g_timer.cancel();
        endWarningEffects();
        if (tsl::notification) {
            tsl::notification->dismissFront(); // the countdown toast
            tsl::notification->show("Sleep timer cancelled.", 24, 20, "", "Sleep Timer", 2000);
        }
        return true;
    }

    void step(int direction) {
        const s64 delta = s64(direction) * StepMinutes * 60;
        if (g_timer.active()) {
            if (direction > 0 && g_timer.remaining() + StepMinutes * 60 > u64(MaxMinutes) * 60)
                return;
            g_timer.adjust(delta, MinRemainingSeconds);
            refreshStatus();
            return;
        }
        setDuration(long(g_config.durationMinutes) + direction * long(StepMinutes));
    }

    void setDuration(long minutes) {
        g_config.durationMinutes = clampMinutes(minutes);
        saveConfig();
        m_start->setValue(minutesLabel(g_config.durationMinutes));
    }

    void refreshStatus() {
        std::string text;
        switch (g_timer.state()) {
            case SleepTimer::State::Running:
            case SleepTimer::State::Warning:
                text = formatDuration(g_timer.remaining());
                break;
            case SleepTimer::State::Firing:
                text = "Sleeping…";
                break;
            case SleepTimer::State::Failed:
                text = "Failed, see below";
                break;
            case SleepTimer::State::Missed:
                text = "Expired during sleep";
                break;
            default:
                text = "Not running";
                break;
        }

        if (text != m_statusText) {
            m_statusText = text;
            m_status->setValue(text);
        }

        std::string last = "\u2013";
        {
            std::lock_guard lock(g_reportMutex);
            if (g_haveReport)
                last = describeReport(g_lastReport);
        }
        if (last != m_lastText) {
            m_lastText = last;
            m_last->setValue(last);
        }
    }

    tsl::elm::ListItem *m_status = nullptr;
    tsl::elm::ListItem *m_last = nullptr;
    std::string m_lastText;
    tsl::elm::ListItem *m_start = nullptr;
    std::string m_statusText;
};

class SleepTimerOverlay : public tsl::Overlay {
public:
    void initServices() override {
        // Steady clock for the countdown; libtesla only opens time briefly at boot.
        m_timeInitialized = R_SUCCEEDED(timeInitialize());
        loadConfig();
        configureTimer();
    }

    void exitServices() override {
        g_timer.shutdown();
        endWarningEffects();
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
