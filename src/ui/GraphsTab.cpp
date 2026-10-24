#include "AnalyzerPopup.hpp"
#include "../core/Util.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

using namespace geode::prelude;

namespace fwa::ui {

namespace {

// ---------------------------------------------------------------------------
// Per-input frame-window graph with hover, click, zoom and region selection
// ---------------------------------------------------------------------------

class WindowGraph : public CCLayer {
public:
    struct Point {
        size_t record;
        double x;      // input number or time
        double y;      // window frames, or cumulative value
        double window; // frames (for colors)
        bool lower;
        bool tight;
    };

    static WindowGraph* create(CCSize size) {
        auto ret = new WindowGraph();
        if (ret->init(size)) {
            ret->autorelease();
            return ret;
        }
        delete ret;
        return nullptr;
    }

    std::function<void(std::string const&)> onInfo;
    std::function<void(size_t)> onOpen;

    void setData(std::vector<Point> points, std::string yTitle, std::string xTitle, bool line, std::vector<double> edges) {
        m_points = std::move(points);
        m_yTitle = std::move(yTitle);
        m_xTitle = std::move(xTitle);
        m_line = line;
        m_edges = std::move(edges);
        m_dataMin = m_points.empty() ? 0.0 : m_points.front().x;
        m_dataMax = m_points.empty() ? 1.0 : m_points.back().x;
        if (m_dataMax <= m_dataMin) m_dataMax = m_dataMin + 1.0;
        if (!m_keepView || m_viewMax <= m_viewMin) {
            m_viewMin = m_dataMin;
            m_viewMax = m_dataMax;
        }
        m_keepView = false;
        m_viewMin = std::max(m_viewMin, m_dataMin);
        m_viewMax = std::min(m_viewMax, m_dataMax);
        if (m_viewMax <= m_viewMin) {
            m_viewMin = m_dataMin;
            m_viewMax = m_dataMax;
        }
        m_hover = -1;
        redraw();
    }

    void keepViewOnNextData() { m_keepView = true; }

    void zoom(double factor) {
        double center = (m_viewMin + m_viewMax) / 2;
        double half = (m_viewMax - m_viewMin) / 2 * factor;
        double minHalf = std::max(0.5, (m_dataMax - m_dataMin) / 2000.0);
        half = std::clamp(half, minHalf, (m_dataMax - m_dataMin) / 2);
        m_viewMin = center - half;
        m_viewMax = center + half;
        clampView();
        redraw();
    }

    void pan(double fraction) {
        double span = m_viewMax - m_viewMin;
        m_viewMin += span * fraction;
        m_viewMax += span * fraction;
        clampView();
        redraw();
    }

    void resetView() {
        m_viewMin = m_dataMin;
        m_viewMax = m_dataMax;
        redraw();
    }

    void updateHover() {
#if defined(GEODE_IS_WINDOWS) || defined(GEODE_IS_MACOS)
        if (!this->isVisible() || !this->getParent() || !this->getParent()->isVisible()) return;
        auto local = this->convertToNodeSpace(getMousePos());
        if (!inPlot(local)) {
            if (m_hover != -1) {
                m_hover = -1;
                redraw();
            }
            return;
        }
        int nearest = nearestPoint(local.x);
        if (nearest != m_hover) {
            m_hover = nearest;
            redraw();
        }
#endif
    }

protected:
    CCDrawNode* m_draw = nullptr;
    CCNode* m_labels = nullptr;
    CCSize m_size;
    std::vector<Point> m_points;
    std::vector<double> m_edges;
    std::string m_yTitle, m_xTitle;
    bool m_line = false;
    bool m_keepView = false;
    double m_dataMin = 0, m_dataMax = 1, m_viewMin = 0, m_viewMax = 1;
    int m_hover = -1;
    bool m_dragging = false;
    float m_dragStart = 0.f, m_dragNow = 0.f;

    static constexpr float kLeft = 30.f, kBottom = 16.f, kTop = 8.f, kRight = 6.f;

    bool init(CCSize size) {
        if (!CCLayer::init()) return false;
        m_size = size;
        this->setContentSize(size);
        this->ignoreAnchorPointForPosition(false);
        this->setAnchorPoint({0.f, 0.f});
        m_draw = CCDrawNode::create();
        this->addChild(m_draw);
        m_labels = CCNode::create();
        this->addChild(m_labels);
        this->setTouchEnabled(true);
        return true;
    }

    void registerWithTouchDispatcher() override {
        CCTouchDispatcher::get()->addTargetedDelegate(this, 0, true);
    }

    CCRect plotRect() const {
        return {kLeft, kBottom, m_size.width - kLeft - kRight, m_size.height - kBottom - kTop};
    }

    bool inPlot(CCPoint p) const {
        return plotRect().containsPoint(p);
    }

    void clampView() {
        double span = m_viewMax - m_viewMin;
        if (m_viewMin < m_dataMin) {
            m_viewMin = m_dataMin;
            m_viewMax = m_dataMin + span;
        }
        if (m_viewMax > m_dataMax) {
            m_viewMax = m_dataMax;
            m_viewMin = std::max(m_dataMin, m_dataMax - span);
        }
    }

    float toPx(double x) const {
        auto r = plotRect();
        return r.origin.x + static_cast<float>((x - m_viewMin) / (m_viewMax - m_viewMin)) * r.size.width;
    }

    double fromPx(float px) const {
        auto r = plotRect();
        return m_viewMin + (px - r.origin.x) / r.size.width * (m_viewMax - m_viewMin);
    }

    int nearestPoint(float px) const {
        if (m_points.empty()) return -1;
        double x = fromPx(px);
        auto it = std::lower_bound(m_points.begin(), m_points.end(), x, [](Point const& p, double v) { return p.x < v; });
        int best = -1;
        double bestD = std::numeric_limits<double>::infinity();
        for (auto cand : {it, it == m_points.begin() ? it : std::prev(it)}) {
            if (cand == m_points.end()) continue;
            double d = std::fabs(cand->x - x);
            if (d < bestD) {
                bestD = d;
                best = static_cast<int>(cand - m_points.begin());
            }
        }
        if (best >= 0 && std::fabs(toPx(m_points[best].x) - px) > 12.f) return -1;
        return best;
    }

    bool ccTouchBegan(CCTouch* touch, CCEvent*) override {
        if (!this->isVisible() || !this->getParent() || !this->getParent()->isVisible()) return false;
        auto p = this->convertTouchToNodeSpace(touch);
        if (!inPlot(p)) return false;
        m_dragging = true;
        m_dragStart = m_dragNow = p.x;
        return true;
    }

    void ccTouchMoved(CCTouch* touch, CCEvent*) override {
        if (!m_dragging) return;
        auto r = plotRect();
        m_dragNow = std::clamp(this->convertTouchToNodeSpace(touch).x, r.getMinX(), r.getMaxX());
        redraw();
    }

    void ccTouchEnded(CCTouch* touch, CCEvent*) override {
        if (!m_dragging) return;
        m_dragging = false;
        float a = std::min(m_dragStart, m_dragNow), b = std::max(m_dragStart, m_dragNow);
        if (b - a > 6.f) {
            // zoom to the selected region
            double lo = fromPx(a), hi = fromPx(b);
            if (hi - lo > 0.5) {
                m_viewMin = lo;
                m_viewMax = hi;
                clampView();
            }
            redraw();
            return;
        }
        int n = nearestPoint(this->convertTouchToNodeSpace(touch).x);
        if (n >= 0) {
            if (n == m_hover && onOpen) {
                onOpen(m_points[n].record);
            }
            m_hover = n;
        }
        redraw();
    }

    void ccTouchCancelled(CCTouch*, CCEvent*) override {
        m_dragging = false;
        redraw();
    }

    void redraw() {
        m_draw->clear();
        m_labels->removeAllChildren();
        auto r = plotRect();
        auto grid = ccColor4F{1.f, 1.f, 1.f, .08f};
        m_draw->drawRect(r.origin, r.origin + CCPoint{r.size.width, r.size.height}, {0.f, 0.f, 0.f, .25f}, .5f, {1.f, 1.f, 1.f, .15f});

        if (m_points.empty()) {
            auto l = label("No frame windows to plot yet.", "chat", .6f, kMuted, {.5f, .5f});
            l->setPosition({r.getMidX(), r.getMidY()});
            m_labels->addChild(l);
            if (onInfo) onInfo("");
            return;
        }

        // visible range
        auto first = std::lower_bound(m_points.begin(), m_points.end(), m_viewMin, [](Point const& p, double v) { return p.x < v; });
        auto last = std::upper_bound(m_points.begin(), m_points.end(), m_viewMax, [](double v, Point const& p) { return v < p.x; });
        double yMax = 0.0;
        for (auto it = first; it != last; ++it) yMax = std::max(yMax, it->y);
        if (yMax <= 0) yMax = 1.0;
        yMax = niceCeil(yMax * 1.05);

        auto yPx = [&](double y) { return r.origin.y + static_cast<float>(y / yMax) * r.size.height; };

        // grid + y labels
        for (int i = 0; i <= 4; i++) {
            double v = yMax * i / 4.0;
            float py = yPx(v);
            m_draw->drawSegment({r.getMinX(), py}, {r.getMaxX(), py}, .25f, grid);
            auto l = label(num(v, yMax < 10 ? 1 : 0), "chat", .4f, kMuted, {1.f, .5f});
            l->setPosition({r.getMinX() - 3.f, py});
            m_labels->addChild(l);
        }
        auto yt = label(m_yTitle, "chat", .4f, kMuted, {0.f, .5f});
        yt->setPosition({r.getMinX() + 3.f, r.getMaxY() - 5.f});
        m_labels->addChild(yt);
        for (int i = 0; i <= 4; i++) {
            double v = m_viewMin + (m_viewMax - m_viewMin) * i / 4.0;
            auto l = label(num(v, (m_viewMax - m_viewMin) < 20 ? 1 : 0), "chat", .4f, kMuted, {.5f, 1.f});
            l->setPosition({toPx(v), r.getMinY() - 2.f});
            m_labels->addChild(l);
        }
        auto xt = label(m_xTitle, "chat", .4f, kMuted, {1.f, 0.f});
        xt->setPosition({r.getMaxX() - 2.f, r.getMinY() + 2.f});
        m_labels->addChild(xt);

        size_t visible = static_cast<size_t>(last - first);
        float barW = visible > 0 ? std::clamp(r.size.width / static_cast<float>(visible) * .7f, 1.f, 8.f) : 2.f;

        if (m_line) {
            CCPoint prev;
            bool hasPrev = false;
            for (auto it = first; it != last; ++it) {
                CCPoint p{toPx(it->x), yPx(it->y)};
                if (hasPrev) m_draw->drawSegment(prev, p, .7f, toColor4F(kAccent, .9f));
                prev = p;
                hasPrev = true;
            }
        } else {
            // When there are more points than pixels, keep the narrowest window per pixel column
            // so tight inputs are never hidden by neighbours.
            int lastPx = std::numeric_limits<int>::min();
            Point const* columnPick = nullptr;
            auto flush = [&](Point const* p) {
                if (!p) return;
                float px = toPx(p->x);
                auto c = windowColor(p->window, m_edges);
                m_draw->drawRect({px - barW / 2, r.getMinY()}, {px + barW / 2, yPx(p->y)}, toColor4F(c, p->lower ? .45f : .9f), 0.f,
                    {0, 0, 0, 0});
                if (p->tight) m_draw->drawDot({px, yPx(p->y) + 2.5f}, 1.4f, {1.f, .35f, .35f, 1.f});
            };
            for (auto it = first; it != last; ++it) {
                int px = static_cast<int>(toPx(it->x));
                if (px != lastPx) {
                    flush(columnPick);
                    columnPick = &*it;
                    lastPx = px;
                } else if (it->y < columnPick->y) {
                    columnPick = &*it;
                }
            }
            flush(columnPick);
        }

        // hover marker + info
        if (m_hover >= 0 && m_hover < static_cast<int>(m_points.size())) {
            auto const& p = m_points[m_hover];
            float px = toPx(p.x);
            if (px >= r.getMinX() && px <= r.getMaxX()) {
                m_draw->drawSegment({px, r.getMinY()}, {px, r.getMaxY()}, .4f, {1.f, 1.f, 1.f, .6f});
                m_draw->drawDot({px, yPx(p.y)}, 2.5f, {1.f, 1.f, 1.f, 1.f});
            }
            if (onInfo) onInfo(describe(p));
        } else if (onInfo) {
            onInfo("Hover or tap a bar for details - drag to zoom into a region - click again to open");
        }

        // selection rectangle
        if (m_dragging && std::fabs(m_dragNow - m_dragStart) > 2.f) {
            float a = std::min(m_dragStart, m_dragNow), b = std::max(m_dragStart, m_dragNow);
            m_draw->drawRect({a, r.getMinY()}, {b, r.getMaxY()}, {.47f, .78f, 1.f, .18f}, .5f, {.47f, .78f, 1.f, .7f});
        }
    }

    std::string describe(Point const& p) const {
        auto const& a = Session::get().analysis();
        if (p.record >= a.inputs.size()) return "";
        auto const& rec = a.inputs[p.record];
        return fmt::format("Input #{}  t={} s  window {} f{}  {}  {}", rec.index + 1, num(rec.time, 3), num(p.window, 2),
            p.lower ? " (lower bound)" : "", statusName(rec.window.status), m_line ? "cumulative " + num(p.y, 2) : std::string());
    }

    static double niceCeil(double v) {
        if (v <= 0) return 1;
        double e = std::pow(10.0, std::floor(std::log10(v)));
        for (double m : {1.0, 2.0, 2.5, 5.0, 10.0}) {
            if (v <= m * e) return m * e;
        }
        return 10 * e;
    }
};

// ---------------------------------------------------------------------------
// Histogram of frame-window ranges
// ---------------------------------------------------------------------------

class HistogramView : public CCNode {
public:
    static HistogramView* create(CCSize size) {
        auto ret = new HistogramView();
        if (ret->init()) {
            ret->m_size = size;
            ret->setContentSize(size);
            ret->m_draw = CCDrawNode::create();
            ret->addChild(ret->m_draw);
            ret->m_labels = CCNode::create();
            ret->addChild(ret->m_labels);
            ret->autorelease();
            return ret;
        }
        delete ret;
        return nullptr;
    }

    void setData(Histogram const& h, bool cumulative) {
        m_draw->clear();
        m_labels->removeAllChildren();
        size_t n = h.counts.size();
        if (n == 0) return;
        std::vector<size_t> values = h.counts;
        if (cumulative) {
            for (size_t i = 1; i < n; i++) values[i] += values[i - 1];
        }
        size_t maxV = 1;
        for (auto v : values) maxV = std::max(maxV, v);
        float left = 20.f, bottom = 22.f, top = m_size.height - 14.f;
        float slot = (m_size.width - left - 10.f) / static_cast<float>(n);
        for (size_t i = 0; i < n; i++) {
            float x = left + slot * static_cast<float>(i);
            float hgt = (top - bottom) * static_cast<float>(values[i]) / static_cast<float>(maxV);
            auto c = bucketColor(i, n);
            m_draw->drawRect({x + slot * .15f, bottom}, {x + slot * .85f, bottom + std::max(hgt, .5f)}, toColor4F(c, .9f), 0.f, {0, 0, 0, 0});
            auto cnt = label(std::to_string(values[i]), "big", .3f, c, {.5f, 0.f});
            cnt->setPosition({x + slot / 2, bottom + hgt + 2.f});
            m_labels->addChild(cnt);
            auto name = label(cumulative ? "<" + num(i + 1 < n ? h.edges[i + 1] : INFINITY) : h.labels[i], "big", .26f, c, {.5f, 1.f});
            if (cumulative && i + 1 == n) name->setString("all");
            name->setPosition({x + slot / 2, bottom - 3.f});
            m_labels->addChild(name);
        }
        auto t = label(cumulative ? "Cumulative: inputs with a window below each edge (frames)" : "Inputs per frame-window range (frames)",
            "chat", .45f, kMuted, {0.f, .5f});
        t->setPosition({left, m_size.height - 6.f});
        m_labels->addChild(t);
    }

private:
    CCSize m_size;
    CCDrawNode* m_draw = nullptr;
    CCNode* m_labels = nullptr;
};

// ---------------------------------------------------------------------------

class GraphsTab : public Tab {
public:
    static GraphsTab* create(AnalyzerPopup* popup, CCSize size) {
        auto ret = new GraphsTab();
        if (ret->init(popup, size)) {
            ret->autorelease();
            return ret;
        }
        delete ret;
        return nullptr;
    }

    void refresh() override {
        rebuildControls();
        auto const& session = Session::get();
        auto const& a = session.analysis();
        auto s = Settings::load();
        auto stats = computeWindowStats(a, s.stats());
        m_graph->setVisible(!m_histogram);
        m_hist->setVisible(m_histogram);
        if (m_histogram) {
            m_hist->setData(stats.histogram, m_cumulative);
            m_info->setString(fmt::format("{} counted input(s) with a window. Ranges from the Display Ranges setting.", stats.withWindow).c_str());
            return;
        }

        std::vector<WindowGraph::Point> pts;
        auto const& pass = session.passProbabilities();
        bool useProb = m_cumulative && session.precision().has_value();
        double cum = 0.0;
        for (auto const& r : a.inputs) {
            if (!isCounted(r, s.count())) continue;
            if (m_filter == 3 && r.player2) continue;
            if (m_filter == 4 && !r.player2) continue;
            auto w = windowFrames(r, s.windowFps, a.tps, true);
            if (!w) continue;
            bool lower = r.window.status == WindowStatus::LowerBound;
            bool tight = *w <= s.tightThreshold + 1e-9;
            if (m_filter == 1 && !isMeasured(r.window.status)) continue;
            if (m_filter == 2 && !tight) continue;
            double x = m_byTime ? r.time : static_cast<double>(r.index + 1);
            double y = *w;
            if (m_cumulative) {
                if (useProb) {
                    double p = r.index < pass.size() ? pass[r.index] : std::nan("");
                    if (std::isfinite(p) && p > 0) cum += -std::log10(p);
                } else if (tight) {
                    cum += 1.0;
                }
                y = cum;
            }
            pts.push_back({r.index, x, y, *w, lower, tight});
        }
        std::string yTitle = !m_cumulative ? "window (frames @ " + num(s.windowFps) + " Hz)"
            : useProb ? "cumulative difficulty: -log10 P(reach) at current L"
                      : "cumulative count of inputs <= " + num(s.tightThreshold) + " frames";
        m_graph->setData(std::move(pts), yTitle, m_byTime ? "time (s)" : "input #", m_cumulative, s.stats().edges);
    }

    void tick(float) override {
        if (!m_histogram) m_graph->updateHover();
    }

private:
    WindowGraph* m_graph = nullptr;
    HistogramView* m_hist = nullptr;
    CCMenu* m_controls = nullptr;
    CCLabelBMFont* m_info = nullptr;
    bool m_histogram = false;
    bool m_cumulative = false;
    bool m_byTime = false;
    int m_filter = 0;

    bool init(AnalyzerPopup* popup, CCSize size) {
        if (!initTab(popup, size)) return false;
        m_controls = CCMenu::create();
        m_controls->setPosition({0.f, 0.f});
        this->addChild(m_controls, 5);

        CCSize area{size.width - 8.f, size.height - 44.f};
        m_graph = WindowGraph::create(area);
        m_graph->setPosition({4.f, 20.f});
        this->addChild(m_graph);
        m_hist = HistogramView::create(area);
        m_hist->setPosition({4.f, 20.f});
        this->addChild(m_hist);

        m_info = label("", "chat", .5f, kText);
        m_info->setPosition({6.f, 9.f});
        this->addChild(m_info);
        m_graph->onInfo = [this](std::string const& text) {
            m_info->setString(text.c_str());
            m_info->limitLabelWidth(m_size.width - 12.f, .5f, .25f);
        };
        m_graph->onOpen = [this](size_t record) { m_popup->showInputDetail(record); };
        return true;
    }

    void rebuildControls() {
        m_controls->removeAllChildren();
        float y = m_size.height - 11.f;
        float cursor = 6.f; // left edge of the next button
        auto add = [&](std::string const& text, int style, float width, std::function<void()> fn) {
            auto b = button(text, .36f, style, [this, fn](auto) { defer(this, fn); }, width);
            float w = width * .36f;
            b->setPosition({cursor + w / 2, y});
            m_controls->addChild(b);
            cursor += w + 6.f;
        };
        static char const* filters[] = {"All", "Measured", "Tight only", "Player 1", "Player 2"};
        add(m_histogram ? "Histogram" : "Per input", 1, 150.f, [this] { m_histogram = !m_histogram; refresh(); });
        add(m_cumulative ? "Cumulative" : "Individual", 1, 160.f, [this] { m_cumulative = !m_cumulative; refresh(); });
        if (!m_histogram) {
            add(m_byTime ? "X: Time" : "X: Input #", 2, 130.f, [this] { m_byTime = !m_byTime; refresh(); });
            add(std::string("Filter: ") + filters[m_filter], 2, 180.f, [this] { m_filter = (m_filter + 1) % 5; refresh(); });
            add("-", 2, 34.f, [this] { m_graph->zoom(2.0); });
            add("+", 2, 34.f, [this] { m_graph->zoom(.5); });
            add("<", 2, 34.f, [this] { m_graph->pan(-.5); });
            add(">", 2, 34.f, [this] { m_graph->pan(.5); });
            add("Reset", 2, 80.f, [this] { m_graph->resetView(); });
        }
    }
};

} // namespace

Tab* createGraphsTab(AnalyzerPopup* popup, CCSize size) {
    return GraphsTab::create(popup, size);
}

} // namespace fwa::ui
