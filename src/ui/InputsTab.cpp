#include "AnalyzerPopup.hpp"
#include "../core/Util.hpp"

#include <Geode/binding/FLAlertLayer.hpp>

#include <algorithm>
#include <cctype>
#include <iterator>
#include <cmath>
#include <limits>

using namespace geode::prelude;

namespace fwa::ui {

namespace {

constexpr int kRowsPerPage = 10;
constexpr float kRowH = 12.5f;

char const* kSortNames[] = {"Input #", "Time", "Smallest window", "Largest window", "Lowest pass %"};
constexpr int kSortCount = 5;

struct Column {
    char const* name;
    float x;
};
constexpr Column kCols[] = {
    {"#", 4.f}, {"Time", 34.f}, {"Frame", 74.f}, {"P", 112.f}, {"Event", 124.f}, {"Mode", 152.f},
    {"Window", 206.f}, {"Early", 246.f}, {"Late", 272.f}, {"Pass", 298.f}, {"Diff", 342.f}, {"Status", 374.f},
};

class InputsTab : public Tab {
public:
    static InputsTab* create(AnalyzerPopup* popup, CCSize size) {
        auto ret = new InputsTab();
        if (ret->init(popup, size)) {
            ret->autorelease();
            return ret;
        }
        delete ret;
        return nullptr;
    }

    void refresh() override {
        rebuildList();
        drawPage();
    }

private:
    CCNode* m_rows = nullptr;
    CCMenu* m_rowMenu = nullptr;
    CCLabelBMFont* m_pageLabel = nullptr;
    CCLabelBMFont* m_countLabel = nullptr;
    CCMenuItemSpriteExtra* m_sortBtn = nullptr;
    CCMenu* m_controls = nullptr;
    TextInput* m_search = nullptr;
    std::vector<size_t> m_list; // record indices after filter + sort
    int m_page = 0;
    int m_sort = 0;
    bool m_countedOnly = true;
    std::string m_query;

    bool init(AnalyzerPopup* popup, CCSize size) {
        if (!initTab(popup, size)) return false;

        m_controls = CCMenu::create();
        m_controls->setPosition({0.f, 0.f});
        this->addChild(m_controls, 5);

        m_search = TextInput::create(120.f, "Search (#, <=2, ship...)", "chatFont.fnt");
        m_search->setScale(.8f);
        m_search->setPosition({54.f, size.height - 12.f});
        m_search->setCallback([this](std::string const& text) {
            m_query = text;
            m_page = 0;
            refresh();
        });
        this->addChild(m_search, 5);

        rebuildSortButton();

        toggle(m_controls, this, {222.f, size.height - 12.f}, "Counted only", true, [this](bool v) {
            m_countedOnly = v;
            m_page = 0;
            refresh();
        }, .45f);

        auto fill = button("Fill Unknown...", .38f, 2, [this](auto) { onFillUnknown(); }, 150.f);
        fill->setPosition({395.f, size.height - 12.f});
        m_controls->addChild(fill);

        // header
        for (auto const& c : kCols) {
            auto l = label(c.name, "big", .26f, kMuted);
            l->setPosition({c.x, size.height - 30.f});
            this->addChild(l);
        }

        m_rows = CCNode::create();
        this->addChild(m_rows);
        m_rowMenu = CCMenu::create();
        m_rowMenu->setPosition({0.f, 0.f});
        this->addChild(m_rowMenu, 3);

        auto prev = CCMenuItemExt::createSpriteExtraWithFrameName("GJ_arrow_03_001.png", .35f, [this](auto) { changePage(-1); });
        prev->setPosition({170.f, 9.f});
        m_controls->addChild(prev);
        auto next = CCMenuItemExt::createSpriteExtraWithFrameName("GJ_arrow_03_001.png", .35f, [this](auto) { changePage(1); });
        static_cast<CCSprite*>(next->getNormalImage())->setFlipX(true);
        next->setPosition({280.f, 9.f});
        m_controls->addChild(next);
        m_pageLabel = label("", "big", .3f, kText, {.5f, .5f});
        m_pageLabel->setPosition({225.f, 9.f});
        this->addChild(m_pageLabel);
        m_countLabel = label("", "chat", .5f, kMuted);
        m_countLabel->setPosition({6.f, 9.f});
        this->addChild(m_countLabel);
        auto hint = label("Click a row for details", "chat", .45f, kMuted, {1.f, .5f});
        hint->setPosition({444.f, 9.f});
        this->addChild(hint);
        return true;
    }

    void rebuildSortButton() {
        if (m_sortBtn) m_sortBtn->removeFromParent();
        m_sortBtn = button(std::string("Sort: ") + kSortNames[m_sort], .38f, 1, [this](auto) {
            defer(this, [this] {
                m_sort = (m_sort + 1) % kSortCount;
                m_page = 0;
                rebuildSortButton();
                refresh();
            });
        }, 190.f);
        m_sortBtn->setPosition({155.f, m_size.height - 12.f});
        m_controls->addChild(m_sortBtn);
    }

    void changePage(int delta) {
        int pages = std::max(1, static_cast<int>((m_list.size() + kRowsPerPage - 1) / kRowsPerPage));
        m_page = std::clamp(m_page + delta, 0, pages - 1);
        drawPage();
    }

    bool matches(InputRecord const& r, Settings const& s, double window, bool hasWindow) const {
        auto q = toLower(trim(m_query));
        if (q.empty()) return true;
        // numeric comparisons on the window
        for (char const* op : {"<=", ">=", "<", ">", "="}) {
            std::string_view o(op);
            if (q.rfind(o, 0) == 0) {
                char* end = nullptr;
                std::string rest = trim(q.substr(o.size()));
                double v = std::strtod(rest.c_str(), &end);
                if (end == rest.c_str()) return false;
                if (!hasWindow) return false;
                if (o == "<=") return window <= v + 1e-9;
                if (o == ">=") return window >= v - 1e-9;
                if (o == "<") return window < v - 1e-9;
                if (o == ">") return window > v + 1e-9;
                return std::fabs(window - v) < 1e-9;
            }
        }
        // input number or range
        if (std::isdigit(static_cast<unsigned char>(q[0]))) {
            auto dash = q.find('-');
            char* end = nullptr;
            unsigned long lo = std::strtoul(q.c_str(), &end, 10);
            unsigned long hi = lo;
            if (dash != std::string::npos) hi = std::strtoul(q.c_str() + dash + 1, &end, 10);
            size_t n = r.index + 1;
            return n >= lo && n <= hi;
        }
        std::string hay = toLower(std::string(statusName(r.window.status)) + " " + statusShortName(r.window.status) + " " +
            r.gameMode + " " + (r.player2 ? "p2" : "p1") + " " + (r.down ? "press" : "release") + " " + buttonName(r.button));
        (void)s;
        return hay.find(q) != std::string::npos;
    }

    void rebuildList() {
        auto const& a = Session::get().analysis();
        auto s = Settings::load();
        auto const& pass = Session::get().passProbabilities();
        struct Item { size_t idx; double window; bool has; double pass; };
        std::vector<Item> items;
        items.reserve(a.inputs.size());
        for (auto const& r : a.inputs) {
            if (m_countedOnly && !isCounted(r, s.count())) continue;
            auto w = windowFrames(r, s.windowFps, a.tps, true);
            double p = r.index < pass.size() ? pass[r.index] : std::numeric_limits<double>::quiet_NaN();
            if (!matches(r, s, w.value_or(0.0), w.has_value())) continue;
            items.push_back({r.index, w.value_or(0.0), w.has_value(), p});
        }
        auto inf = std::numeric_limits<double>::infinity();
        switch (m_sort) {
            case 1:
                std::stable_sort(items.begin(), items.end(), [&](auto const& x, auto const& y) { return a.inputs[x.idx].time < a.inputs[y.idx].time; });
                break;
            case 2:
                std::stable_sort(items.begin(), items.end(), [&](auto const& x, auto const& y) {
                    return (x.has ? x.window : inf) < (y.has ? y.window : inf);
                });
                break;
            case 3:
                std::stable_sort(items.begin(), items.end(), [&](auto const& x, auto const& y) {
                    return (x.has ? x.window : -inf) > (y.has ? y.window : -inf);
                });
                break;
            case 4:
                std::stable_sort(items.begin(), items.end(), [&](auto const& x, auto const& y) {
                    double px = std::isnan(x.pass) ? inf : x.pass;
                    double py = std::isnan(y.pass) ? inf : y.pass;
                    return px < py;
                });
                break;
            default: break;
        }
        m_list.clear();
        for (auto const& it : items) m_list.push_back(it.idx);
        int pages = std::max(1, static_cast<int>((m_list.size() + kRowsPerPage - 1) / kRowsPerPage));
        m_page = std::clamp(m_page, 0, pages - 1);
    }

    void drawPage() {
        m_rows->removeAllChildren();
        m_rowMenu->removeAllChildren();
        auto const& session = Session::get();
        auto const& a = session.analysis();
        auto s = Settings::load();
        auto stats = computeWindowStats(a, s.stats());
        auto const& pass = session.passProbabilities();
        auto edges = s.stats().edges;

        int pages = std::max(1, static_cast<int>((m_list.size() + kRowsPerPage - 1) / kRowsPerPage));
        m_pageLabel->setString(fmt::format("Page {}/{}", m_page + 1, pages).c_str());
        m_countLabel->setString(fmt::format("{} row(s)", m_list.size()).c_str());

        if (m_list.empty()) {
            auto e = label(session.hasData() ? "No inputs match." : "No data loaded. Import a macro or load sample data.",
                "chat", .6f, kMuted, {.5f, .5f});
            e->setPosition({m_size.width / 2, m_size.height / 2});
            m_rows->addChild(e);
            return;
        }

        float top = m_size.height - 44.f;
        for (int i = 0; i < kRowsPerPage; i++) {
            size_t li = static_cast<size_t>(m_page * kRowsPerPage + i);
            if (li >= m_list.size()) break;
            auto const& r = a.inputs[m_list[li]];
            float y = top - kRowH * static_cast<float>(i);

            auto bg = CCLayerColor::create(i % 2 ? ccColor4B{255, 255, 255, 10} : ccColor4B{0, 0, 0, 30}, m_size.width - 4.f, kRowH);
            bg->setPosition({2.f, y - kRowH / 2});
            m_rows->addChild(bg);

            auto w = windowFrames(r, s.windowFps, a.tps, true);
            bool measured = isMeasured(r.window.status);
            auto cell = [&](int col, std::string const& text, ccColor3B color = kText) {
                auto l = label(text, "chat", .5f, color);
                l->setPosition({kCols[col].x, y});
                float maxW = (col + 1 < static_cast<int>(std::size(kCols)) ? kCols[col + 1].x : m_size.width) - kCols[col].x - 3.f;
                if (l->getScaledContentSize().width > maxW) l->setScale(.5f * maxW / l->getScaledContentSize().width);
                m_rows->addChild(l);
            };
            cell(0, std::to_string(r.index + 1), kMuted);
            cell(1, num(r.time, 3));
            cell(2, r.frame ? std::to_string(*r.frame) : "-");
            cell(3, r.player2 ? "2" : "1", r.player2 ? kAccent : kText);
            cell(4, r.down ? "press" : "release", r.down ? kText : kMuted);
            cell(5, r.gameMode.empty() ? "-" : r.gameMode, kMuted);
            cell(6, windowText(r, s), w ? windowColor(*w, edges) : kMuted);
            cell(7, measured ? std::to_string(r.window.earlyTicks) + (r.window.earlyLimited ? "+" : "") : "-");
            cell(8, measured ? std::to_string(r.window.lateTicks) + (r.window.lateLimited ? "+" : "") : "-");
            double p = r.index < pass.size() ? pass[r.index] : std::numeric_limits<double>::quiet_NaN();
            cell(9, std::isfinite(p) ? (p >= .9999 ? num(p * 100, 3) : sig(p * 100, 4)) + "%" : "-",
                std::isfinite(p) ? (p < .9 ? kBad : p < .99 ? kWarn : kGood) : kMuted);
            if (w && *w > 0 && stats.withWindow > 0) {
                double diff = stats.median / *w;
                cell(10, num(diff, 2) + "x", diff >= 2 ? kBad : diff >= 1.25 ? kWarn : kText);
            } else {
                cell(10, "-", kMuted);
            }
            cell(11, statusName(r.window.status), statusColor(r.window.status));

            size_t idx = r.index;
            auto item = CCMenuItemExt::create([this, idx](auto) { m_popup->showInputDetail(idx); });
            item->setContentSize({m_size.width - 4.f, kRowH});
            item->setAnchorPoint({0.f, 0.f});
            item->setPosition({2.f, y - kRowH / 2});
            m_rowMenu->addChild(item);
        }
    }

    void onFillUnknown() {
        auto const& a = Session::get().analysis();
        auto s = Settings::load();
        size_t unknown = 0;
        for (auto const& r : a.inputs) {
            if (isCounted(r, s.count()) && r.window.status == WindowStatus::Unknown) unknown++;
        }
        if (unknown == 0) {
            FLAlertLayer::create("Fill Unknown", "There are no counted inputs with an unknown window.", "OK")->show();
            return;
        }
        auto p = ValuePromptPopup::create("Fill Unknown Windows",
            std::to_string(unknown) + " counted input(s) have no window. Enter a window (in " + num(s.windowFps) +
            " Hz frames) to use for them. They will be labelled ESTIMATED everywhere, never as measured.",
            "", [](std::string const& value) {
                char* end = nullptr;
                double v = std::strtod(value.c_str(), &end);
                if (end == value.c_str() || !std::isfinite(v) || v < 0) {
                    FLAlertLayer::create("Fill Unknown", "Please enter a non-negative number.", "OK")->show();
                    return;
                }
                auto s2 = Settings::load();
                auto& an = Session::get().analysisMut();
                size_t changed = 0;
                for (auto& r : an.inputs) {
                    if (!isCounted(r, s2.count()) || r.window.status != WindowStatus::Unknown) continue;
                    r.window.status = WindowStatus::Estimated;
                    r.window.windowSeconds = v / s2.windowFps;
                    r.window.note = "Default window entered by the user for unknown inputs (not measured).";
                    changed++;
                }
                Session::get().setMessage("Filled " + std::to_string(changed) + " unknown window(s) with an estimated " + num(v) + " frames.");
            });
        if (p) p->show();
    }
};

} // namespace

Tab* createInputsTab(AnalyzerPopup* popup, CCSize size) {
    return InputsTab::create(popup, size);
}

} // namespace fwa::ui
