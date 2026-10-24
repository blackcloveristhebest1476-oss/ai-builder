#include "AnalyzerPopup.hpp"
#include "../core/Util.hpp"

#include <Geode/binding/FLAlertLayer.hpp>
#include <Geode/ui/TextArea.hpp>

#include <cmath>

using namespace geode::prelude;

namespace fwa::ui {

InputDetailPopup* InputDetailPopup::create(size_t recordIndex) {
    auto ret = new InputDetailPopup();
    if (ret->init(recordIndex)) {
        ret->autorelease();
        return ret;
    }
    delete ret;
    return nullptr;
}

bool InputDetailPopup::init(size_t recordIndex) {
    if (!Popup::init(380.f, 270.f)) return false;
    m_index = recordIndex;
    auto const& session = Session::get();
    auto const& a = session.analysis();
    if (recordIndex >= a.inputs.size()) return false;
    auto const& r = a.inputs[recordIndex];
    auto s = Settings::load();
    this->setTitle(fmt::format("Input #{}", r.index + 1).c_str());

    auto w = windowFrames(r, s.windowFps, a.tps, true);
    bool measured = isMeasured(r.window.status);

    // ---- facts ----
    float x = 18.f, colW = 160.f, y = 232.f;
    int row = 0;
    auto put = [&](int col, std::string const& n, std::string const& v, ccColor3B c = kText) {
        auto node = statRow(n, v, colW, c, .3f);
        node->setPosition({x + col * (colW + 24.f), y - 13.f * row});
        m_mainLayer->addChild(node);
    };
    put(0, "Time", num(r.time, 4) + " s");
    put(1, "Status", statusName(r.window.status), statusColor(r.window.status));
    row++;
    put(0, "Frame (tick)", r.frame ? std::to_string(*r.frame) : "-");
    put(1, "Window", w ? (r.window.status == WindowStatus::LowerBound ? ">= " : "") + num(*w, 3) + " f @ " + num(s.windowFps) + " Hz" : "unknown",
        w ? windowColor(*w, s.stats().edges) : kMuted);
    row++;
    put(0, "Player / button", std::string(r.player2 ? "P2 / " : "P1 / ") + buttonName(r.button));
    put(1, "Total ticks", measured ? std::to_string(r.window.totalTicks()) + " ticks" : "-");
    row++;
    put(0, "Event", r.down ? "press" : "release");
    put(1, "Early / late", measured ? std::to_string(r.window.earlyTicks) + (r.window.earlyLimited ? "+" : "") + " / " +
        std::to_string(r.window.lateTicks) + (r.window.lateLimited ? "+" : "") + " ticks" : "-");
    row++;
    put(0, "Game mode", r.gameMode.empty() ? "unknown" : r.gameMode);
    put(1, "Asymmetry", asymmetryName(asymmetryOf(r.window)));
    row++;
    std::string cls = "-";
    if (w) cls = *w <= s.framePerfectThreshold + 1e-9 ? "Frame perfect" : *w <= s.tightThreshold + 1e-9 ? "Extremely tight" : "Normal";
    put(0, "Classification", cls, cls == "Frame perfect" ? kBad : cls == "Extremely tight" ? kWarn : kText);
    auto const& pass = session.passProbabilities();
    double p = recordIndex < pass.size() ? pass[recordIndex] : std::nan("");
    put(1, "Pass probability", std::isfinite(p) ? sig(p * 100.0, 6) + "%" : "not calculated");
    row++;
    put(0, "Counted", isCounted(r, s.count()) ? "yes" : "no");
    put(1, "Trials run", r.window.trials ? std::to_string(r.window.trials) : "-");
    row++;

    // ---- timing strip: which shifts survived ----
    float stripY = y - 13.f * row - 14.f;
    if (measured) {
        auto draw = CCDrawNode::create();
        m_mainLayer->addChild(draw);
        int e = r.window.earlyTicks, l = r.window.lateTicks;
        int span = std::max({e + 2, l + 2, 4});
        float cx = 190.f, cell = std::min(10.f, 160.f / static_cast<float>(span));
        for (int d = -span; d <= span; d++) {
            float x0 = cx + d * cell - cell / 2 + .5f;
            ccColor4F c;
            if (d >= -e && d <= l) c = toColor4F(kGood, d == 0 ? 1.f : .75f);
            else if ((d == -e - 1 && !r.window.earlyLimited) || (d == l + 1 && !r.window.lateLimited)) c = toColor4F(kBad, .85f);
            else c = {1.f, 1.f, 1.f, .08f};
            draw->drawRect({x0, stripY - 5.f}, {x0 + cell - 1.f, stripY + 5.f}, c, 0.f, {0, 0, 0, 0});
        }
        draw->drawSegment({cx, stripY - 8.f}, {cx, stripY + 8.f}, .6f, {1.f, 1.f, 1.f, 1.f});
        auto el = label("earlier", "chat", .45f, kMuted, {1.f, .5f});
        el->setPosition({cx - span * cell - 6.f, stripY});
        m_mainLayer->addChild(el);
        auto ll = label("later", "chat", .45f, kMuted, {0.f, .5f});
        ll->setPosition({cx + span * cell + 6.f, stripY});
        m_mainLayer->addChild(ll);
        auto legend = label("green = survived, red = died, white line = original timing", "chat", .4f, kMuted, {.5f, .5f});
        legend->setPosition({190.f, stripY - 13.f});
        m_mainLayer->addChild(legend);
    }

    // ---- evidence / notes ----
    std::string evidence = r.window.note;
    if (r.window.earlyLimited && !r.window.earlyLimitReason.empty()) evidence += "\nEarly side: " + r.window.earlyLimitReason + ".";
    if (r.window.lateLimited && !r.window.lateLimitReason.empty()) evidence += "\nLate side: " + r.window.lateLimitReason + ".";
    if (r.duplicate) evidence += "\nDuplicate of the previous event (excluded).";
    if (r.redundant && !r.duplicate) evidence += "\nRedundant event: the button was already in this state (excluded).";
    if (evidence.empty()) evidence = "No measurement yet. Measure in-game, import frame-window data, or enter a window below.";
    auto area = SimpleTextArea::create(evidence, "chatFont.fnt", .5f, 340.f);
    area->setAnchorPoint({.5f, 1.f});
    area->setPosition({190.f, measured ? stripY - 22.f : stripY + 6.f});
    m_mainLayer->addChild(area);

    // ---- manual entry ----
    auto menu = CCMenu::create();
    menu->setPosition({0.f, 0.f});
    m_mainLayer->addChild(menu);
    auto caption = label("Set window manually (frames @ " + num(s.windowFps) + " Hz):", "big", .28f);
    caption->setPosition({18.f, 26.f});
    m_mainLayer->addChild(caption);
    m_manual = TextInput::create(70.f, "frames", "chatFont.fnt");
    m_manual->setCommonFilter(CommonFilter::Float);
    m_manual->setScale(.75f);
    m_manual->setPosition({245.f, 26.f});
    m_mainLayer->addChild(m_manual);
    auto set = button("Set", .42f, 0, [this](auto) {
        auto text = m_manual->getString();
        char* end = nullptr;
        double v = std::strtod(text.c_str(), &end);
        if (end == text.c_str() || !std::isfinite(v) || v < 0) {
            FLAlertLayer::create("Set window", "Enter a non-negative number of frames.", "OK")->show();
            return;
        }
        auto s2 = Settings::load();
        auto& rec = Session::get().analysisMut().inputs[m_index];
        rec.window = {};
        rec.window.status = WindowStatus::UserSupplied;
        rec.window.windowSeconds = v / s2.windowFps;
        rec.window.note = "Entered by hand (" + num(v, 3) + " frames at " + num(s2.windowFps) + " Hz).";
        this->onClose(nullptr);
    }, 70.f);
    set->setPosition({300.f, 26.f});
    menu->addChild(set);
    if (r.window.status == WindowStatus::UserSupplied || r.window.status == WindowStatus::Estimated) {
        auto clear = button("Clear", .42f, 3, [this](auto) {
            auto& rec = Session::get().analysisMut().inputs[m_index];
            rec.window = {};
            rec.window.note = "Manual value cleared.";
            this->onClose(nullptr);
        }, 80.f);
        clear->setPosition({348.f, 26.f});
        menu->addChild(clear);
    }
    return true;
}

} // namespace fwa::ui
