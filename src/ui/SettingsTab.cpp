#include "AnalyzerPopup.hpp"
#include "../core/Util.hpp"

#include <Geode/binding/CCMenuItemToggler.hpp>
#include <Geode/ui/GeodeUI.hpp>
#include <Geode/ui/TextArea.hpp>

using namespace geode::prelude;

namespace fwa::ui {

namespace {

class SettingsTab : public Tab {
public:
    static SettingsTab* create(AnalyzerPopup* popup, CCSize size) {
        auto ret = new SettingsTab();
        if (ret->init(popup, size)) {
            ret->autorelease();
            return ret;
        }
        delete ret;
        return nullptr;
    }

    void refresh() override {
        auto s = Settings::load();
        for (auto& [key, t] : m_toggles) t->toggle(Mod::get()->getSettingValue<bool>(key));
        std::string text =
            "Game FPS (physics rate): " + num(s.gameFps) + " TPS - used only when a file has no rate of its own.\n"
            "Timing-window FPS (f): " + num(s.windowFps) + " Hz - windows are counted in frames of this rate (w = N / f).\n"
            "Target " + formatDuration(s.targetSeconds) + ", respawn " + num(s.respawnSeconds, 2) + " s, tolerance " +
            num(s.tolerance, 6) + " s, max " + std::to_string(s.maxIterations) + " iterations.\n"
            "Measurement: max shift " + std::to_string(s.maxShift) + " ticks, horizon " + num(s.horizonSeconds, 2) + " s, " +
            std::to_string(s.budgetMs) + " ms per frame.\n"
            "Display ranges: " + Mod::get()->getSettingValue<std::string>("histogram-edges") +
            (s.edgesError.empty() ? "" : "  (invalid: " + s.edgesError + ")") + "\nTheme: " + s.theme + ".";
        m_summary->setText(text);
    }

private:
    std::vector<std::pair<std::string, CCMenuItemToggler*>> m_toggles;
    SimpleTextArea* m_summary = nullptr;

    bool init(AnalyzerPopup* popup, CCSize size) {
        if (!initTab(popup, size)) return false;
        auto menu = CCMenu::create();
        menu->setPosition({0.f, 0.f});
        this->addChild(menu, 5);

        auto head = label("Current settings", "gold", .42f, kText);
        head->setPosition({10.f, size.height - 14.f});
        this->addChild(head);
        m_summary = SimpleTextArea::create("", "chatFont.fnt", .5f, 250.f);
        m_summary->setAnchorPoint({0.f, 1.f});
        m_summary->setPosition({10.f, size.height - 26.f});
        this->addChild(m_summary);

        auto explain = SimpleTextArea::create(
            "Game FPS and timing-window FPS are different things: GD simulates at 240 ticks per second, while f only "
            "changes the unit windows are expressed in. Every setting has an explanation (i) in All Settings.",
            "chatFont.fnt", .45f, 250.f);
        explain->setAnchorPoint({0.f, 0.f});
        explain->setPosition({10.f, 8.f});
        explain->setColor({150, 155, 175, 255});
        this->addChild(explain);

        struct T { char const* key; char const* text; };
        T list[] = {
            {"count-releases", "Count releases"},
            {"include-lower-bounds", "Use lower-bound windows"},
            {"measure-recheck", "Recheck window edges"},
            {"measure-determinism-check", "Determinism check"},
            {"auto-save", "Automatic saving"},
            {"background-calculation", "Background calculation"},
        };
        float y = size.height - 18.f;
        for (auto const& t : list) {
            std::string key = t.key;
            auto tg = toggle(menu, this, {290.f, y}, t.text, Mod::get()->getSettingValue<bool>(key),
                [key](bool v) { Mod::get()->setSettingValue<bool>(key, v); }, .45f);
            m_toggles.push_back({key, tg});
            y -= 20.f;
        }

        auto all = button("All Settings...", .45f, 1, [](auto) { openSettingsPopup(Mod::get()); }, 180.f);
        all->setPosition({320.f, 40.f});
        menu->addChild(all);
        auto reset = button("Reset to Defaults", .45f, 3, [this](auto) {
            createQuickPopup("Reset Settings", "Reset <cy>all</c> Frame Window Analyzer settings (including the keybind) to their defaults?",
                "Cancel", "Reset", [this](auto, bool yes) {
                    if (!yes) return;
                    Settings::resetAll();
                    reloadTheme();
                    defer(this, [this] { m_popup->refreshAll(); });
                });
        }, 200.f);
        reset->setPosition({320.f, 16.f});
        menu->addChild(reset);
        return true;
    }
};

} // namespace

Tab* createSettingsTab(AnalyzerPopup* popup, CCSize size) {
    return SettingsTab::create(popup, size);
}

} // namespace fwa::ui
