#include "AnalyzerPopup.hpp"
#include "../core/Util.hpp"
#include "../engine/MeasurementEngine.hpp"

#include <Geode/ui/MDPopup.hpp>

using namespace geode::prelude;

namespace fwa::ui {

namespace {

class DashboardTab : public Tab {
public:
    static DashboardTab* create(AnalyzerPopup* popup, CCSize size) {
        auto ret = new DashboardTab();
        if (ret->initTab(popup, size)) {
            ret->autorelease();
            return ret;
        }
        delete ret;
        return nullptr;
    }

    void refresh() override {
        this->removeAllChildren();
        auto const& session = Session::get();
        auto const& a = session.analysis();
        auto s = Settings::load();
        auto stats = computeWindowStats(a, s.stats());

        auto menu = CCMenu::create();
        menu->setPosition({0.f, 0.f});
        this->addChild(menu, 5);

        // ---- header ----
        auto title = label(sourceSummary(a), "big", .4f, a.source.kind == SourceKind::Sample ? kWarn : kText);
        title->setPosition({8.f, m_size.height - 12.f});
        title->limitLabelWidth(280.f, .4f, .2f);
        this->addChild(title);

        std::string status;
        ccColor3B statusCol = kMuted;
        if (!session.hasData()) {
            status = "Use Import Macro below, or Sample Data to try the interface.";
        } else if (a.source.kind == SourceKind::Sample) {
            status = "SAMPLE DATA - synthetic values, nothing here was measured";
            statusCol = kWarn;
        } else if (a.measurement.performed) {
            status = "Measured in-game " + a.measurement.finishedAt + (a.measurement.levelName.empty() ? "" : " on " + a.measurement.levelName);
            statusCol = kGood;
        } else if (a.source.kind == SourceKind::WindowData) {
            status = "Frame windows supplied by the file (not measured by this mod)";
            statusCol = kAccent;
        } else {
            status = "Not measured yet - open the level and press Measure";
            statusCol = kWarn;
        }
        auto st = label(status, "chat", .55f, statusCol);
        st->setPosition({8.f, m_size.height - 27.f});
        st->limitLabelWidth(280.f, .55f, .3f);
        this->addChild(st);

        // ---- stats grid ----
        float colW = 132.f;
        float y0 = m_size.height - 46.f;
        auto put = [&](int col, int row, std::string const& name, std::string const& value, ccColor3B c = kText) {
            auto r = statRow(name, value, colW, c, .3f);
            r->setPosition({8.f + col * (colW + 14.f), y0 - row * 14.f});
            this->addChild(r);
        };
        auto fr = [&](double v) { return num(v, 2) + " f"; };
        bool any = stats.withWindow > 0;
        put(0, 0, "Total inputs", std::to_string(stats.totalInputs));
        put(0, 1, s.countReleases ? "Counted (press+release)" : "Counted presses", std::to_string(stats.countedInputs));
        put(0, 2, "With a window", std::to_string(stats.withWindow), any ? kGood : kMuted);
        put(0, 3, "Verified", std::to_string(stats.verified), statusColor(WindowStatus::Verified));
        put(0, 4, "Lower bound", std::to_string(stats.lowerBound), statusColor(WindowStatus::LowerBound));
        put(0, 5, "Not measurable", std::to_string(stats.baselineFailed + stats.inconsistent),
            stats.baselineFailed + stats.inconsistent ? kWarn : kMuted);
        put(0, 6, "Unknown", std::to_string(stats.unknown), kMuted);
        put(0, 7, "Supplied / estimated", std::to_string(stats.userSupplied) + " / " + std::to_string(stats.estimated),
            stats.estimated ? statusColor(WindowStatus::Estimated) : kText);

        put(1, 0, "Average window", any ? fr(stats.average) : "-");
        put(1, 1, "Median window", any ? fr(stats.median) : "-");
        put(1, 2, "Narrowest verified", stats.narrowestVerifiedIndex
            ? fr(stats.narrowestVerified) + " (#" + std::to_string(*stats.narrowestVerifiedIndex + 1) + ")" : "-",
            stats.narrowestVerifiedIndex ? windowColor(stats.narrowestVerified, s.stats().edges) : kMuted);
        put(1, 3, "Widest verified", stats.widestVerifiedIndex
            ? fr(stats.widestVerified) + " (#" + std::to_string(*stats.widestVerifiedIndex + 1) + ")" : "-",
            stats.widestVerifiedIndex ? windowColor(stats.widestVerified, s.stats().edges) : kMuted);
        put(1, 4, "Frame perfect (<=" + num(s.framePerfectThreshold) + ")", std::to_string(stats.framePerfect),
            stats.framePerfect ? kBad : kText);
        put(1, 5, "Extremely tight (<=" + num(s.tightThreshold) + ")", std::to_string(stats.tight),
            stats.tight ? kWarn : kText);
        put(1, 6, "Game / window FPS", num(a.tps) + " / " + num(s.windowFps));
        put(1, 7, "Duration", session.hasData() ? formatDuration(a.durationSeconds()) : "-");

        // ---- precision summary ----
        auto const& prec = session.precision();
        std::string precText = "Required precision: not calculated";
        ccColor3B precCol = kMuted;
        if (prec) {
            if (prec->solved) {
                precText = "Required precision L* = " + num(prec->precision, 3) + " sigma/s  (target " +
                    formatDuration(s.targetSeconds) + ")";
            } else {
                precText = "Fixed L = " + num(prec->precision, 3) + "  ->  expected " + formatDuration(prec->stats.expectedTotalTime);
            }
            precCol = kAccent;
        }
        auto pl = label(precText, "big", .32f, precCol);
        pl->setPosition({8.f, 34.f});
        pl->limitLabelWidth(285.f, .32f, .18f);
        this->addChild(pl);

        // Measurement progress
        std::string progress;
        if (MeasurementEngine::get()->active()) progress = "Measurement running...";
        else if (session.hasData() && a.source.kind == SourceKind::Macro) {
            size_t measurable = 0, done = 0;
            for (auto const& r : a.inputs) {
                if (!isMeasurable(r, s.count())) continue;
                measurable++;
                if (isMeasured(r.window.status) || r.window.status == WindowStatus::BaselineFailed) done++;
            }
            progress = "Analysis progress: " + std::to_string(done) + " / " + std::to_string(measurable) + " counted inputs measured";
        }
        if (!progress.empty()) {
            auto pg = label(progress, "chat", .55f, kMuted);
            pg->setPosition({8.f, 18.f});
            this->addChild(pg);
        }

        auto details = button("Details", .4f, 2, [](auto) { showDetails(); }, 90.f);
        details->setPosition({255.f, 18.f});
        menu->addChild(details);
        size_t warnings = 0;
        for (auto const& d : a.source.diagnostics) warnings += d.severity != Severity::Info;
        if (warnings) {
            auto w = label(std::to_string(warnings) + "!", "big", .3f, kWarn, {.5f, .5f});
            w->setPosition({287.f, 26.f});
            this->addChild(w, 6);
        }

        // ---- frame-window ranges (reference-style counter) ----
        auto box = panel({142.f, m_size.height - 8.f}, 120);
        box->setPosition({302.f, 4.f});
        this->addChild(box);
        auto h = label("Frame windows", "gold", .45f, kText, {.5f, .5f});
        h->setPosition({373.f, m_size.height - 14.f});
        this->addChild(h);
        auto const& hist = stats.histogram;
        size_t n = hist.counts.size();
        float top = m_size.height - 32.f;
        float rowH = std::min(16.f, (top - 10.f) / static_cast<float>(std::max<size_t>(n, 1)));
        float fontScale = std::min(.5f, rowH / 32.f);
        for (size_t k = 0; k < n; k++) {
            size_t bucket = n - 1 - k; // widest range on top, like the reference overlay
            auto color = bucketColor(bucket, n);
            float y = top - rowH * static_cast<float>(k);
            auto name = label(hist.labels[bucket] + ":", "big", fontScale, color);
            name->setPosition({312.f, y});
            this->addChild(name);
            auto count = label(std::to_string(hist.counts[bucket]), "big", fontScale, color, {1.f, .5f});
            count->setPosition({436.f, y});
            this->addChild(count);
        }
        if (!s.edgesError.empty()) {
            auto e = label("Display ranges setting invalid; using defaults", "chat", .4f, kWarn, {.5f, .5f});
            e->setPosition({373.f, 10.f});
            this->addChild(e);
        }
    }

    static void showDetails() {
        auto const& a = Session::get().analysis();
        std::string md = "## Source\n\n";
        md += "- **File:** " + (a.source.fileName.empty() ? std::string("-") : a.source.fileName) + "\n";
        md += "- **Format:** " + (a.source.formatName.empty() ? std::string("-") : a.source.formatName) + "\n";
        md += "- **Inputs:** " + std::to_string(a.inputs.size()) + "\n";
        md += "- **Physics rate:** " + num(a.tps) + " ticks/s\n";
        if (a.source.macroMeta) {
            auto const& m = *a.source.macroMeta;
            md += "- **Level:** " + (m.levelName.empty() ? std::string("-") : m.levelName) + " (ID " + std::to_string(m.levelId) + ")\n";
            md += "- **Bot:** " + (m.botName.empty() ? std::string("-") : m.botName) + " v" + std::to_string(m.botVersion) + "\n";
            md += "- **Author:** " + (m.author.empty() ? std::string("-") : m.author) + "\n";
            md += "- **Game version:** " + (m.gameVersionText.empty() ? std::string("-") : m.gameVersionText) + "\n";
            md += "- **Framerate:** " + num(m.framerate) + (m.framerateFromFile ? " (from file)" : " (not stored, assumed)") + "\n";
            md += "- **Platformer:** " + std::string(m.platformer ? "yes" : "no") + ", **LDM:** " + (m.ldm ? "yes" : "no") +
                ", **coins:** " + std::to_string(m.coins) + ", **seed:** " + std::to_string(m.seed) + "\n";
            if (m.storedDurationSeconds) md += "- **Stored duration:** " + num(*m.storedDurationSeconds, 3) + " s\n";
            if (!m.deathFrames.empty()) md += "- **Recorded deaths:** " + std::to_string(m.deathFrames.size()) + "\n";
            if (!m.description.empty()) md += "- **Description:** " + m.description + "\n";
        }
        if (a.measurement.performed || !a.measurement.notes.empty()) {
            md += "\n## Measurement\n\n";
            md += "- **Finished:** " + a.measurement.finishedAt + "\n";
            md += "- **Level:** " + a.measurement.levelName + " (ID " + std::to_string(a.measurement.levelId) + ")\n";
            md += "- **Max shift:** " + std::to_string(a.measurement.maxShiftTicks) + " ticks, **horizon:** " +
                num(a.measurement.horizonSeconds, 2) + " s, **edge recheck:** " + (a.measurement.recheckBorders ? "on" : "off") + "\n";
            md += "- **Determinism check:** " + std::string(!a.measurement.determinismChecked ? "not run" :
                a.measurement.determinismPassed ? "passed" : "FAILED") + "\n";
            md += "- **Result:** " + a.measurement.notes + "\n";
        }
        md += "\n## Messages\n\n";
        if (a.source.diagnostics.empty()) md += "No warnings.\n";
        for (auto const& d : a.source.diagnostics) {
            md += std::string("- ") + (d.severity == Severity::Error ? "<cr>Error:</c> " : d.severity == Severity::Warning ? "<cy>Warning:</c> " : "") +
                d.message + "\n";
        }
        md += "\n## What the statuses mean\n\n"
              "- **Verified:** measured by replaying the real level with the input moved.\n"
              "- **Lower bound:** every tested timing survived until the search limit; the true window is at least this big.\n"
              "- **Inconsistent:** repeated trials disagreed; not trusted.\n"
              "- **Baseline failed:** the unmodified macro does not survive this input, so nothing can be measured.\n"
              "- **User supplied:** imported from a frame-window file or typed in by you.\n"
              "- **Estimated:** a default you chose, or sample data. Never a measurement.\n"
              "- **Unknown:** no data yet.\n";
        MDPopup::create("Details", md, "OK")->show();
    }
};

} // namespace

Tab* createDashboardTab(AnalyzerPopup* popup, CCSize size) {
    return DashboardTab::create(popup, size);
}

} // namespace fwa::ui
