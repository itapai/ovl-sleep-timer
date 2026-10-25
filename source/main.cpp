#define TESLA_INIT_IMPL
#include <exception_wrap.hpp>
#include <tesla.hpp>

#include <cstdio>
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
    std::atomic<power::SleepPath> g_lastPath{power::SleepPath::None};

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

    std::string minutesLabel(u32 minutes) {
        return std::to_string(minutes) + " min";
    }

    void configureTimer() {
        g_timer.setCallbacks({
            .onWarning = [] {
                if (tsl::notification)
                    tsl::notification->show("Sleeping in 1 minute. Open the overlay to cancel.", 24, 20, "", "Sleep Timer", 5000);
                power::blinkBacklight();
            },
            .onExpire = [] {
                power::SleepPath path = power::SleepPath::None;
                const Result rc = power::putSystemToSleep(&path);
                g_lastPath.store(path, std::memory_order_release);
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

        m_start = new tsl::elm::ListItem("Start", minutesLabel(g_config.durationMinutes));
        m_start->setClickListener([](u64 keys) {
            if (consumeWarning()) return true;
            if (!(keys & KEY_A)) return false;
            saveConfig();
            g_timer.start(u64(g_config.durationMinutes) * 60, g_config.warning ? WarningSeconds : 0);
            return true;
        });
        list->addItem(m_start);

        auto *cancel = new tsl::elm::ListItem("Cancel");
        cancel->setClickListener([](u64 keys) {
            if (consumeWarning()) return true;
            if (!(keys & KEY_A)) return false;
            g_timer.cancel();
            return true;
        });
        list->addItem(cancel);

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
        if (tsl::notification)
            tsl::notification->show("Sleep timer cancelled.", 24, 20, "", "Sleep Timer", 2000);
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
                text = std::string(power::pathName(g_lastPath.load(std::memory_order_acquire))) + ": " +
                       formatResult(g_timer.lastResult());
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
    }

    tsl::elm::ListItem *m_status = nullptr;
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
