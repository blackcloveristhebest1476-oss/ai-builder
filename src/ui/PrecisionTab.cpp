#include "AnalyzerPopup.hpp"
#include "../core/Util.hpp"

#include <Geode/binding/CCMenuItemToggler.hpp>
#include <Geode/binding/FLAlertLayer.hpp>
#include <Geode/ui/TextArea.hpp>

#include <cmath>

using namespace geode::prelude;

namespace fwa::ui {

namespace {

std::string durationFromLog(double value, double log10Value) {
    if (std::isfinite(value)) return formatDuration(value);
    if (std::isfinite(log10Value)) {
        double years = log10Value - std::log10(86400.0 * 365.25);
        return "10^" + num(years, 1) + " years";
    }
    return "infinite (cannot complete)";
}

class PrecisionTab : public Tab {
public:
    static PrecisionTab* create(AnalyzerPopup* popup, CCSize size) {
        auto ret = new PrecisionTab();
        if (ret->init(popup, size)) {
            ret->autorelease();
            return ret;
        }
        delete ret;
        return nullptr;
    }

    void refresh() override {
        auto s = Settings::load();
        if (!m_editingTarget) m_target->setString(num(s.targetSeconds, 3));
        if (!m_editingFixed) m_fixedInput->setString(num(s.fixedPrecision, 4));
        if (!m_editingRespawn) m_respawn->setString(num(s.respawnSeconds, 3));
        m_editingTarget = m_editingFixed = m_editingRespawn = false;
        m_nerve->toggle(s.nerve);
        m_fatigue->toggle(s.fatigue);
        m_cps->toggle(s.cps);
        m_constants->setString(fmt::format("k_t = {}   k_u = {}   k_c = {}", sig(s.kt, 6), sig(s.ku, 6), sig(s.kc, 6)).c_str());
        m_constants->limitLabelWidth(205.f, .45f, .2f);
        m_rate->setString(fmt::format("Window rate f = {} Hz, game rate {} TPS", num(s.windowFps), num(Session::get().analysis().tps)).c_str());
        m_rate->limitLabelWidth(205.f, .45f, .2f);
        rebuildModeButtons();
        rebuildResults();
    }

    void tick(float) override {
        auto const& job = Session::get().job();
        if (job) {
            double p = job->progress.load();
            m_progress->setString(fmt::format("Calculating... {}%", static_cast<int>(p * 100)).c_str());
            m_progress->setColor(kAccent);
            m_cancelBtn->setVisible(true);
        } else if (m_cancelBtn->isVisible()) {
            m_cancelBtn->setVisible(false);
            m_progress->setString("");
        }
    }

private:
    bool m_fixed = false;
    bool m_editingTarget = false, m_editingFixed = false, m_editingRespawn = false;
    CCMenu* m_menu = nullptr;
    CCMenu* m_modeMenu = nullptr;
    TextInput* m_target = nullptr;
    TextInput* m_fixedInput = nullptr;
    TextInput* m_respawn = nullptr;
    CCMenuItemToggler* m_nerve = nullptr;
    CCMenuItemToggler* m_fatigue = nullptr;
    CCMenuItemToggler* m_cps = nullptr;
    CCLabelBMFont* m_constants = nullptr;
    CCLabelBMFont* m_rate = nullptr;
    CCLabelBMFont* m_progress = nullptr;
    CCMenuItemSpriteExtra* m_cancelBtn = nullptr;
    CCNode* m_results = nullptr;

    TextInput* numberInput(float x, float y, std::string const& caption, char const* key, bool* editingFlag) {
        auto l = label(caption, "big", .3f, kText);
        l->setPosition({10.f, y});
        this->addChild(l);
        auto input = TextInput::create(80.f, "0", "chatFont.fnt");
        input->setCommonFilter(CommonFilter::Float);
        input->setScale(.8f);
        input->setPosition({x, y});
        std::string k = key;
        input->setCallback([k, editingFlag](std::string const& text) {
            char* end = nullptr;
            double v = std::strtod(text.c_str(), &end);
            if (end != text.c_str() && std::isfinite(v) && v >= 0) {
                *editingFlag = true;
                Mod::get()->setSettingValue<double>(k, v);
            }
        });
        this->addChild(input);
        return input;
    }

    bool init(AnalyzerPopup* popup, CCSize size) {
        if (!initTab(popup, size)) return false;
        auto left = panel({214.f, size.height - 8.f}, 70);
        left->setPosition({4.f, 4.f});
        this->addChild(left);
        auto right = panel({224.f, size.height - 8.f}, 110);
        right->setPosition({222.f, 4.f});
        this->addChild(right);

        m_menu = CCMenu::create();
        m_menu->setPosition({0.f, 0.f});
        this->addChild(m_menu, 5);
        m_modeMenu = CCMenu::create();
        m_modeMenu->setPosition({0.f, 0.f});
        this->addChild(m_modeMenu, 5);

        float y = size.height - 34.f;
        m_target = numberInput(170.f, y, "Target time (s)", "target-time", &m_editingTarget);
        m_fixedInput = numberInput(170.f, y - 20.f, "Precision L (fixed)", "fixed-precision", &m_editingFixed);
        m_respawn = numberInput(170.f, y - 40.f, "Respawn time (s)", "respawn-time", &m_editingRespawn);

        float ty = y - 62.f;
        m_nerve = toggle(m_menu, this, {18.f, ty}, "Nerve", false, [](bool v) { Settings::set("nerve-enabled", v); }, .45f);
        m_fatigue = toggle(m_menu, this, {84.f, ty}, "Fatigue", false, [](bool v) { Settings::set("fatigue-enabled", v); }, .45f);
        m_cps = toggle(m_menu, this, {154.f, ty}, "CPS (exp.)", false, [](bool v) {
            Settings::set("cps-enabled", v);
            if (v) {
                FLAlertLayer::create("CPS Modifier",
                    "NaNDL describes the CPS modifier as a <cr>work in progress and unreliable</c>. Results with it enabled "
                    "are experimental. It also makes inputs without a window count as (largest window + 1) instead of always passing.",
                    "OK")->show();
            }
        }, .45f);
        m_constants = label("", "chat", .45f, kMuted);
        m_constants->setPosition({10.f, ty - 15.f});
        this->addChild(m_constants);
        m_rate = label("", "chat", .45f, kMuted);
        m_rate->setPosition({10.f, ty - 27.f});
        this->addChild(m_rate);

        auto calc = button("Calculate", .55f, 0, [this](auto) { onCalculate(); }, 160.f);
        calc->setPosition({60.f, 18.f});
        m_menu->addChild(calc);
        m_cancelBtn = button("Cancel", .45f, 3, [](auto) { Session::get().cancelPrecision(); }, 110.f);
        m_cancelBtn->setPosition({150.f, 18.f});
        m_cancelBtn->setVisible(false);
        m_menu->addChild(m_cancelBtn);
        m_progress = label("", "chat", .5f, kAccent, {.5f, .5f});
        m_progress->setPosition({150.f, 36.f});
        this->addChild(m_progress);

        m_results = CCNode::create();
        this->addChild(m_results, 4);
        return true;
    }

    void rebuildModeButtons() {
        m_modeMenu->removeAllChildren();
        auto solve = button("Solve L*", .42f, m_fixed ? 2 : 0, [this](auto) {
            defer(this, [this] { m_fixed = false; refresh(); });
        }, 120.f);
        solve->setPosition({48.f, m_size.height - 13.f});
        m_modeMenu->addChild(solve);
        auto fixed = button("Stats for fixed L", .42f, m_fixed ? 0 : 2, [this](auto) {
            defer(this, [this] { m_fixed = true; refresh(); });
        }, 190.f);
        fixed->setPosition({150.f, m_size.height - 13.f});
        m_modeMenu->addChild(fixed);
    }

    void onCalculate() {
        auto err = Session::get().startPrecision(m_fixed);
        if (err) {
            FLAlertLayer::create("Precision", *err, "OK")->show();
            return;
        }
        m_progress->setString("Calculating...");
    }

    void rebuildResults() {
        m_results->removeAllChildren();
        auto const& session = Session::get();
        auto const& prec = session.precision();
        float x = 230.f, w = 208.f;
        float y = m_size.height - 16.f;
        auto title = label(m_fixed ? "Stats for fixed precision" : "Required precision (NaNDL)", "gold", .42f, kText);
        title->setPosition({x, y});
        m_results->addChild(title);
        y -= 18.f;

        if (!prec) {
            std::string msg = session.precisionMessage().empty()
                ? "Press Calculate. The model uses each counted input's frame window and time position. Inputs without "
                  "a window are ignored (they always pass), and nothing is invented."
                : session.precisionMessage();
            auto area = SimpleTextArea::create(msg, "chatFont.fnt", .55f, w);
            area->setAnchorPoint({0.f, 1.f});
            area->setPosition({x, y + 4.f});
            m_results->addChild(area);
            return;
        }

        auto const& st = prec->stats;
        auto big = label((prec->solved ? "L* = " : "L = ") + num(prec->precision, 4) + " sigma/s", "big", .5f, kAccent);
        big->setPosition({x, y - 2.f});
        big->limitLabelWidth(w, .5f, .25f);
        m_results->addChild(big);
        y -= 20.f;
        auto row = [&](std::string const& n, std::string const& v, ccColor3B c = kText) {
            auto r = statRow(n, v, w, c, .3f);
            r->setPosition({x, y - 6.f});
            m_results->addChild(r);
            y -= 13.f;
        };
        row("Expected total time E[T_C]", durationFromLog(st.expectedTotalTime, st.log10ExpectedTotalTime));
        row("Completion chance P(C)", formatProbability(st.completionProbability, st.logCompletion));
        row("Expected attempts", formatLog10Count(st.log10ExpectedAttempts));
        row("Time per attempt E[T_A]", formatDuration(st.expectedAttemptTime));
        row("Status", prec->status, prec->status == "Converged" ? kGood : kWarn);
        if (prec->solved) row("Iterations / error", std::to_string(prec->iterations) + " / " + num(prec->absoluteError, 3) + " s");
        auto s = Settings::load();
        std::string mods;
        if (s.nerve) mods += "Nerve ";
        if (s.fatigue) mods += "Fatigue ";
        if (s.cps) mods += "CPS(exp) ";
        row("Modifiers", mods.empty() ? "Base model" : mods);
        std::string notes;
        if (session.precisionIgnoredInputs()) notes += std::to_string(session.precisionIgnoredInputs()) + " input(s) had no window and were ignored. ";
        if (session.precisionLowerBounds()) notes += std::to_string(session.precisionLowerBounds()) + " lower-bound window(s) used. ";
        if (session.precisionEstimated()) notes += std::to_string(session.precisionEstimated()) + " ESTIMATED window(s) used. ";
        notes += "A statistical estimate, not a guarantee.";
        auto area = SimpleTextArea::create(notes, "chatFont.fnt", .45f, w);
        area->setAnchorPoint({0.f, 1.f});
        area->setPosition({x, y});
        area->setColor({255, 210, 120, 255});
        m_results->addChild(area);
    }
};

} // namespace

Tab* createPrecisionTab(AnalyzerPopup* popup, CCSize size) {
    return PrecisionTab::create(popup, size);
}

} // namespace fwa::ui
