#include "AnalyzerPopup.hpp"
#include "../core/Util.hpp"
#include "../engine/MeasurementEngine.hpp"
#include "../engine/PlaybackEngine.hpp"
#include "../engine/RecordEngine.hpp"

#include <Geode/binding/FLAlertLayer.hpp>
#include <Geode/binding/PauseLayer.hpp>
#include <Geode/binding/PlayLayer.hpp>
#include <Geode/binding/GJGameLevel.hpp>
#include <Geode/ui/GeodeUI.hpp>
#include <Geode/ui/MDPopup.hpp>
#include <Geode/ui/Notification.hpp>
#include <Geode/ui/TextArea.hpp>

#include <algorithm>
#include <cmath>

using namespace geode::prelude;

namespace fwa::ui {

namespace {

constexpr float kWidth = 470.f;
constexpr float kHeight = 290.f;
const CCPoint kContentOrigin{10.f, 46.f};
const CCSize kContentSize{450.f, 190.f};

AnalyzerPopup* s_current = nullptr;

char const* kTabNames[] = {"Dashboard", "Inputs", "Graphs", "Sounds", "Precision", "Settings"};

std::string settingsSignature() {
    auto s = Settings::load();
    return num(s.windowFps) + "|" + num(s.gameFps) + "|" + num(s.respawnSeconds) + "|" + (s.countReleases ? "1" : "0") +
        (s.includeLowerBounds ? "1" : "0") + (s.nerve ? "1" : "0") + (s.fatigue ? "1" : "0") + (s.cps ? "1" : "0") + "|" +
        num(s.framePerfectThreshold) + "|" + num(s.tightThreshold) + "|" + Mod::get()->getSettingValue<std::string>("histogram-edges") +
        "|" + s.theme + "|" + num(s.targetSeconds) + "|" + num(s.fixedPrecision, 6) + "|" + num(s.kt, 12) + num(s.ku, 12) + num(s.kc, 12);
}

/// Generic popup with a message and a column of buttons.
class ChoicePopup : public Popup {
public:
    struct Choice {
        std::string label;
        int style;
        std::function<void()> action;
    };

    static ChoicePopup* create(std::string const& title, std::string const& text, std::vector<Choice> choices) {
        auto ret = new ChoicePopup();
        if (ret->init(title, text, std::move(choices))) {
            ret->autorelease();
            return ret;
        }
        delete ret;
        return nullptr;
    }

protected:
    bool init(std::string const& title, std::string const& text, std::vector<Choice> choices) {
        auto area = SimpleTextArea::create(text, "chatFont.fnt", .7f, 290.f);
        float textH = area->getContentSize().height;
        float h = std::clamp(70.f + textH + 30.f * static_cast<float>(choices.size()), 150.f, 310.f);
        if (!Popup::init(320.f, h)) return false;
        this->setTitle(title.c_str());
        area->setAnchorPoint({.5f, 1.f});
        area->setPosition({160.f, h - 36.f});
        m_mainLayer->addChild(area);
        auto menu = CCMenu::create();
        menu->setPosition({0.f, 0.f});
        m_mainLayer->addChild(menu);
        float y = 25.f + 30.f * static_cast<float>(choices.size() - 1);
        for (auto& c : choices) {
            auto action = c.action;
            auto btn = button(c.label, .6f, c.style, [this, action](auto) {
                this->onClose(nullptr);
                if (action) action();
            }, 200.f);
            btn->setPosition({160.f, y});
            menu->addChild(btn);
            y -= 30.f;
        }
        return true;
    }
};

void startMeasurement(std::vector<size_t> targets) {
    auto settings = Settings::load();
    if (auto popup = AnalyzerPopup::current()) popup->onClose(nullptr);
    if (auto scene = CCDirector::get()->getRunningScene()) {
        if (auto pause = scene->getChildByType<PauseLayer>(0)) pause->onResume(nullptr);
    }
    // Start on the next frame, after the pause menu is gone and the level resumed.
    Loader::get()->queueInMainThread([targets = std::move(targets), settings]() mutable {
        auto pl = PlayLayer::get();
        MeasurementEngine::Config cfg;
        cfg.maxShift = settings.maxShift;
        cfg.horizonTicks = static_cast<uint64_t>(std::llround(settings.horizonSeconds * 240.0));
        cfg.recheck = settings.recheck;
        cfg.determinism = settings.determinismCheck;
        cfg.wholeClick = settings.wholeClick;
        cfg.tap = settings.tap;
        cfg.tapTicks = settings.tapTicks;
        cfg.budgetMs = settings.budgetMs;
        cfg.targets = std::move(targets);
        auto err = MeasurementEngine::get()->start(pl, std::move(cfg), [](std::string const& summary) {
            Loader::get()->queueInMainThread([summary] {
                // If the level was left, don't attach popups to a scene that is going away.
                if (!PlayLayer::get()) {
                    Notification::create("Frame Window Analyzer: " + summary.substr(0, summary.find('\n')),
                        NotificationIcon::Info, 5.f)->show();
                    return;
                }
                AnalyzerPopup::open(AnalyzerPopup::Inputs);
                if (auto p = AnalyzerPopup::current()) p->setFooter(summary.substr(0, summary.find('\n')), kAccent);
                FLAlertLayer::create("Frame Window Analyzer", summary, "OK")->show();
            });
        });
        if (err) {
            FLAlertLayer::create("Measurement", *err, "OK")->show();
        }
    });
}

} // namespace

// ---------------------------------------------------------------------------
// Tab base
// ---------------------------------------------------------------------------

bool Tab::initTab(AnalyzerPopup* popup, CCSize size) {
    if (!CCNode::init()) return false;
    m_popup = popup;
    m_size = size;
    this->setContentSize(size);
    this->setAnchorPoint({0.f, 0.f});
    return true;
}

std::string sourceSummary(Analysis const& a) {
    switch (a.source.kind) {
        case SourceKind::None: return "No data loaded";
        case SourceKind::Sample: return "Sample data (synthetic - not measured)";
        default: break;
    }
    std::string s = a.source.fileName.empty() ? a.source.formatName : a.source.fileName + "  (" + a.source.formatName + ")";
    return s;
}

std::string windowText(InputRecord const& r, Settings const& s) {
    if (r.window.status == WindowStatus::Excluded) return "-";
    auto w = windowFrames(r, s.windowFps, Session::get().analysis().tps, true);
    if (!w) return "?";
    std::string t = num(*w, 2);
    if (r.window.status == WindowStatus::LowerBound) t = ">=" + t;
    return t;
}

// ---------------------------------------------------------------------------
// AnalyzerPopup
// ---------------------------------------------------------------------------

AnalyzerPopup* AnalyzerPopup::create(int tab) {
    auto ret = new AnalyzerPopup();
    if (ret->init(tab)) {
        ret->autorelease();
        return ret;
    }
    delete ret;
    return nullptr;
}

AnalyzerPopup* AnalyzerPopup::current() {
    return s_current;
}

void AnalyzerPopup::open(int tab) {
    if (MeasurementEngine::get()->active()) {
        Notification::create("A measurement is running. Press Esc to stop it first.", NotificationIcon::Warning)->show();
        return;
    }
    if (s_current) {
        s_current->selectTab(tab);
        return;
    }
    // Never leave a level running behind the popup.
    if (auto pl = PlayLayer::get()) {
        if (!pl->m_isPaused && !pl->m_hasCompletedLevel) pl->pauseGame(false);
    }
    if (auto p = AnalyzerPopup::create(tab)) p->show();
}

AnalyzerPopup::~AnalyzerPopup() {
    if (s_current == this) s_current = nullptr;
}

bool AnalyzerPopup::init(int tab) {
    if (!Popup::init(kWidth, kHeight)) return false;
    s_current = this;
    reloadTheme();
    this->setTitle("Frame Window Analyzer", "goldFont.fnt", .75f, 18.f);

    m_tabMenu = CCMenu::create();
    m_tabMenu->setPosition({0.f, 0.f});
    m_mainLayer->addChild(m_tabMenu, 5);

    auto contentBg = panel({kContentSize.width, kContentSize.height + 2.f}, 90);
    contentBg->setPosition(kContentOrigin - CCPoint{0.f, 1.f});
    m_mainLayer->addChild(contentBg);

    for (int i = 0; i < TabCount; i++) {
        Tab* t = nullptr;
        switch (i) {
            case Dashboard: t = createDashboardTab(this, kContentSize); break;
            case Inputs: t = createInputsTab(this, kContentSize); break;
            case Graphs: t = createGraphsTab(this, kContentSize); break;
            case Sounds: t = createSoundsTab(this, kContentSize); break;
            case Precision: t = createPrecisionTab(this, kContentSize); break;
            default: t = createSettingsTab(this, kContentSize); break;
        }
        t->setPosition(kContentOrigin);
        t->setVisible(false);
        m_mainLayer->addChild(t, 2);
        m_tabs.push_back(t);
    }

    // Bottom action bar
    auto actions = CCMenu::create();
    actions->setPosition({0.f, 0.f});
    m_mainLayer->addChild(actions, 5);
    struct Action { char const* text; int style; std::function<void()> fn; };
    std::vector<Action> list = {
        {"Import", 0, [this] { this->onImport(); }},
        {"Record", 3, [this] { this->onRecord(); }},
        {"Sample", 1, [this] { this->onSample(); }},
        {"Measure", 0, [this] { this->onMeasure(); }},
        {"Playback", 0, [this] { this->onPlayback(); }},
        {"Export", 1, [this] { this->onExport(); }},
        {"Saves", 2, [this] { this->onLoadSaved(); }},
    };
    float x = 38.f;
    for (auto& a : list) {
        auto fn = a.fn;
        auto btn = button(a.text, .42f, a.style, [fn](auto) { fn(); }, 140.f);
        btn->setPosition({x, 18.f});
        actions->addChild(btn);
        x += 65.5f;
    }

    m_footer = label("", "chat", .55f, kMuted, {.5f, .5f});
    m_footer->setPosition({kWidth / 2, 38.f});
    m_mainLayer->addChild(m_footer, 5);
    if (!Session::get().lastMessage().empty()) setFooter(Session::get().lastMessage());
    else if (!Session::get().hasData()) setFooter("Import a macro (GDR / GDR2 / JSON) or try the sample data.");

    m_seenRevision = Session::get().revision();
    m_seenSettings = settingsSignature();
    selectTab(tab);
    this->scheduleUpdate();
    return true;
}

void AnalyzerPopup::setFooter(std::string const& text, ccColor3B color) {
    if (!m_footer) return;
    auto firstLine = text.substr(0, text.find('\n'));
    m_footer->setString(firstLine.c_str());
    m_footer->setColor(color);
    m_footer->limitLabelWidth(kWidth - 30.f, .55f, .25f);
}

void AnalyzerPopup::buildTabBar() {
    m_tabMenu->removeAllChildren();
    float spacing = 74.f;
    float x0 = kWidth / 2 - spacing * (TabCount - 1) / 2.f;
    for (int i = 0; i < TabCount; i++) {
        auto btn = button(kTabNames[i], .4f, i == m_tab ? 0 : 2, [this, i](auto) {
            defer(this, [this, i] { this->selectTab(i); });
        }, 170.f);
        btn->setPosition({x0 + spacing * i, kHeight - 42.f});
        m_tabMenu->addChild(btn);
    }
}

void AnalyzerPopup::selectTab(int tab) {
    m_tab = std::clamp(tab, 0, static_cast<int>(TabCount) - 1);
    for (int i = 0; i < static_cast<int>(m_tabs.size()); i++) m_tabs[i]->setVisible(i == m_tab);
    buildTabBar();
    m_tabs[m_tab]->refresh();
}

void AnalyzerPopup::refreshAll() {
    for (auto t : m_tabs) t->refresh();
}

void AnalyzerPopup::update(float dt) {
    auto& session = Session::get();
    bool finished = session.pollPrecision();
    static float settingsTimer = 0.f;
    settingsTimer += dt;
    bool settingsChanged = false;
    if (settingsTimer > .5f) {
        settingsTimer = 0.f;
        auto sig = settingsSignature();
        if (sig != m_seenSettings) {
            m_seenSettings = sig;
            settingsChanged = true;
            reloadTheme();
        }
    }
    if (finished || settingsChanged || session.revision() != m_seenRevision) {
        m_seenRevision = session.revision();
        refreshAll();
        if (finished && !session.precisionMessage().empty()) {
            setFooter(session.precisionMessage(), session.precision() ? kGood : kWarn);
        }
    }
    if (m_tab >= 0 && m_tab < static_cast<int>(m_tabs.size())) m_tabs[m_tab]->tick(dt);
}

void AnalyzerPopup::onClose(CCObject* sender) {
    m_pick.cancel();
    if (s_current == this) s_current = nullptr;
    Popup::onClose(sender);
}

void AnalyzerPopup::showInputDetail(size_t recordIndex) {
    if (auto p = InputDetailPopup::create(recordIndex)) p->show();
}

void AnalyzerPopup::pickAndImport(std::optional<std::filesystem::path> startDir) {
    file::FilePickOptions opts;
    opts.defaultPath = startDir;
    opts.filters.push_back({"Macros and frame-window data", {"*.gdr", "*.gdr2", "*.json", "*.csv", "*.txt", "*.tsv"}});
    opts.filters.push_back({"All files", {"*.*"}});
    m_pick.spawn(file::pick(file::PickMode::OpenFile, opts), [this](file::PickResult res) {
        if (!res) {
            FLAlertLayer::create("Import", "The file picker failed: " + std::string(res.unwrapErr()), "OK")->show();
            return;
        }
        auto picked = std::move(res).unwrap();
        if (!picked) return; // cancelled
        auto err = Session::get().importFile(*picked);
        if (err) {
            setFooter("Import failed.", kBad);
            FLAlertLayer::create("Could not import", *err, "OK")->show();
            return;
        }
        auto const& a = Session::get().analysis();
        size_t warnings = 0;
        for (auto const& d : a.source.diagnostics) warnings += d.severity != Severity::Info;
        setFooter(Session::get().lastMessage() + (warnings ? "  (" + std::to_string(warnings) + " warning(s) - see Dashboard)" : ""),
            warnings ? kWarn : kGood);
        selectTab(Dashboard);
        refreshAll();
    });
}

void AnalyzerPopup::onImport() {
    pickAndImport(std::nullopt);
}

void AnalyzerPopup::onSample() {
    auto load = [this] {
        Session::get().loadSample();
        setFooter(Session::get().lastMessage(), kWarn);
        refreshAll();
    };
    if (Session::get().hasData() && Session::get().analysis().source.kind != SourceKind::Sample) {
        createQuickPopup("Sample Data", "Replace the loaded data with synthetic sample data? (Unsaved results are lost.)",
            "Cancel", "Replace", [load](auto, bool yes) { if (yes) load(); });
        return;
    }
    load();
}

void AnalyzerPopup::onMeasure() {
    auto pl = PlayLayer::get();
    if (auto err = MeasurementEngine::checkCanMeasure(pl)) {
        FLAlertLayer::create("Can't measure here", *err, "OK")->show();
        return;
    }
    if (auto p = MeasureSetupPopup::create([](std::vector<size_t> targets) { startMeasurement(std::move(targets)); })) {
        p->show();
    }
}

void AnalyzerPopup::onRecord() {
    auto recorder = RecordEngine::get();
    if (recorder->active()) {
        auto popup = ChoicePopup::create("Recording",
            "Recording attempt " + std::to_string(recorder->attempt()) + " with " + std::to_string(recorder->inputCount()) +
            " input(s) so far. Finishing the level loads it automatically. You can also stop now:",
            {
                {"Use this attempt", 0, [] { RecordEngine::get()->finish(true, "stopped by you"); }},
                {"Discard recording", 3, [] { RecordEngine::get()->finish(false, ""); }},
                {"Keep recording", 1, [] {}},
            });
        if (popup) popup->show();
        return;
    }
    auto pl = PlayLayer::get();
    if (!pl) {
        FLAlertLayer::create("Record", "Open a level first. Recording captures your own inputs while you play it.", "OK")->show();
        return;
    }
    if (MeasurementEngine::get()->active() || PlaybackEngine::get()->active()) {
        FLAlertLayer::create("Record", "Stop the measurement or playback first.", "OK")->show();
        return;
    }
    auto popup = ChoicePopup::create("Record",
        "Play the level normally. Every click is recorded on the game tick it happens. Dying starts a new attempt "
        "(practice checkpoints are supported). When you beat the level, the run is saved as a .gdr2 file and loaded "
        "here automatically, ready to Measure or Play back. Click Between Frames is switched off while recording.",
        {
            {"Start Recording", 0, [] {
                if (auto popup = AnalyzerPopup::current()) popup->onClose(nullptr);
                if (auto scene = CCDirector::get()->getRunningScene()) {
                    if (auto pause = scene->getChildByType<PauseLayer>(0)) pause->onResume(nullptr);
                }
                Loader::get()->queueInMainThread([] {
                    if (auto err = RecordEngine::get()->start(PlayLayer::get())) {
                        FLAlertLayer::create("Record", *err, "OK")->show();
                    }
                });
            }},
        });
    if (popup) popup->show();
}

void AnalyzerPopup::onPlayback() {
    auto pl = PlayLayer::get();
    if (auto err = MeasurementEngine::checkCanMeasure(pl)) {
        FLAlertLayer::create("Can't play back here", *err, "OK")->show();
        return;
    }
    auto s = Settings::load();
    size_t fp = PlaybackEngine::countMarked(MarkerMode::FramePerfect);
    size_t tight = PlaybackEngine::countMarked(MarkerMode::Tight);
    size_t all = PlaybackEngine::countMarked(MarkerMode::AllMeasured);
    std::string text =
        "Replays the macro in this level at normal speed. Whenever a marked click happens, a ring with its frame "
        "window appears where the player is. Rings only appear on inputs that have a window (measure first). "
        "Your inputs are ignored; Esc stops. Deaths and completions are not saved.";
    if (all == 0) text += "\n\nNo inputs have a window yet, so no rings will appear.";
    auto launch = [](MarkerMode mode) {
        if (auto popup = AnalyzerPopup::current()) popup->onClose(nullptr);
        if (auto scene = CCDirector::get()->getRunningScene()) {
            if (auto pause = scene->getChildByType<PauseLayer>(0)) pause->onResume(nullptr);
        }
        Loader::get()->queueInMainThread([mode] {
            if (auto err = PlaybackEngine::get()->start(PlayLayer::get(), mode)) {
                FLAlertLayer::create("Playback", *err, "OK")->show();
            }
        });
    };
    auto popup = ChoicePopup::create("Playback", text, {
        {"Frame perfect (<= " + num(s.framePerfectThreshold) + " f): " + std::to_string(fp), 0, [launch] { launch(MarkerMode::FramePerfect); }},
        {"Tight (<= " + num(s.tightThreshold) + " f): " + std::to_string(tight), 1, [launch] { launch(MarkerMode::Tight); }},
        {"Every measured input: " + std::to_string(all), 1, [launch] { launch(MarkerMode::AllMeasured); }},
    });
    if (popup) popup->show();
}

void AnalyzerPopup::exportTo(std::string const& kind) {
    auto& session = Session::get();
    std::string content, ext, suffix;
    if (kind == "json") { content = session.exportJson(); ext = ".json"; suffix = "_analysis"; }
    else if (kind == "csv") { content = session.exportCsv(); ext = ".csv"; suffix = "_inputs"; }
    else { content = session.exportNandl(); ext = ".json"; suffix = "_nandl-calculator"; }

    file::FilePickOptions opts;
    opts.defaultPath = session.analysesDir() / (session.suggestedBaseName() + suffix + ext);
    opts.filters.push_back({ext == ".csv" ? "CSV table" : "JSON", {"*" + ext}});
    m_pick.spawn(file::pick(file::PickMode::SaveFile, opts), [this, content = std::move(content), ext](file::PickResult res) {
        if (!res) {
            FLAlertLayer::create("Export", "The file picker failed: " + std::string(res.unwrapErr()), "OK")->show();
            return;
        }
        auto picked = std::move(res).unwrap();
        if (!picked) return;
        auto path = *picked;
        if (!path.has_extension()) path += ext;
        auto w = file::writeString(path, content);
        if (!w) {
            FLAlertLayer::create("Export failed", std::string(w.unwrapErr()), "OK")->show();
            return;
        }
        setFooter("Exported " + utils::string::pathToString(path.filename()), kGood);
        Notification::create("Exported " + utils::string::pathToString(path.filename()), NotificationIcon::Success)->show();
    });
}

void AnalyzerPopup::onExport() {
    if (!Session::get().hasData()) {
        FLAlertLayer::create("Export", "Nothing to export yet. Import a macro or load sample data first.", "OK")->show();
        return;
    }
    auto popup = ChoicePopup::create("Export",
        "JSON keeps everything (inputs, windows, labels, settings, results) and can be loaded again. "
        "CSV is one row per input for spreadsheets. NaNDL JSON can be imported on nandl.pages.dev to cross-check.",
        {
            {"Analysis JSON", 0, [this] { exportTo("json"); }},
            {"Inputs CSV", 1, [this] { exportTo("csv"); }},
            {"NaNDL Calculator JSON", 1, [this] { exportTo("nandl"); }},
        });
    if (popup) popup->show();
}

void AnalyzerPopup::onLoadSaved() {
    auto& session = Session::get();
    auto popup = ChoicePopup::create("Saved Analyses",
        "Analyses are saved to this mod's save folder (automatically after each measurement and calculation if "
        "Automatic Saving is on). Your macro files are never changed.",
        {
            {"Save Now", 0, [this] {
                auto r = Session::get().saveToFolder("manual");
                if (auto err = std::get_if<std::string>(&r)) {
                    FLAlertLayer::create("Save", *err, "OK")->show();
                } else {
                    setFooter("Saved " + utils::string::pathToString(std::get<std::filesystem::path>(r).filename()), kGood);
                }
            }},
            {"Load a Saved Analysis", 1, [this, dir = session.analysesDir()] { pickAndImport(dir); }},
            {"Open Saves Folder", 2, [dir = session.analysesDir()] { file::openFolder(dir); }},
        });
    if (popup) popup->show();
}

// ---------------------------------------------------------------------------
// MeasureSetupPopup
// ---------------------------------------------------------------------------

MeasureSetupPopup* MeasureSetupPopup::create(std::function<void(std::vector<size_t>)> onStart) {
    auto ret = new MeasureSetupPopup();
    if (ret->init(std::move(onStart))) {
        ret->autorelease();
        return ret;
    }
    delete ret;
    return nullptr;
}

std::vector<size_t> MeasureSetupPopup::selectedTargets(std::string* error) const {
    auto const& a = Session::get().analysis();
    auto s = Settings::load();
    auto count = s.count();
    size_t lo = 1, hi = a.inputs.size();
    auto text = trim(m_range ? std::string(m_range->getString()) : std::string("all"));
    auto lower = toLower(text);
    if (!lower.empty() && lower != "all") {
        auto dash = lower.find('-');
        try {
            if (dash == std::string::npos) {
                lo = hi = static_cast<size_t>(std::stoul(lower));
            } else {
                lo = static_cast<size_t>(std::stoul(lower.substr(0, dash)));
                hi = static_cast<size_t>(std::stoul(lower.substr(dash + 1)));
            }
        } catch (...) {
            if (error) *error = "Range must be 'all', a number, or 'from-to' (input numbers).";
            return {};
        }
        if (lo < 1 || hi < lo) {
            if (error) *error = "Invalid range.";
            return {};
        }
    }
    std::vector<size_t> out;
    for (auto const& r : a.inputs) {
        size_t n = r.index + 1;
        if (n < lo || n > hi) continue;
        if (!isMeasurable(r, count)) continue;
        if (m_onlyUnmeasured && isMeasured(r.window.status)) continue;
        out.push_back(r.index);
    }
    if (out.empty() && error) *error = "No measurable inputs in this range (presses only unless 'Count Releases' is on).";
    return out;
}

bool MeasureSetupPopup::init(std::function<void(std::vector<size_t>)> onStart) {
    if (!Popup::init(380.f, 250.f)) return false;
    m_onStart = std::move(onStart);
    this->setTitle("Measure Frame Windows");

    auto const& a = Session::get().analysis();
    auto s = Settings::load();
    auto pl = PlayLayer::get();

    std::string levelLine = "Current level: ";
    if (pl && pl->m_level) {
        levelLine += std::string(pl->m_level->m_levelName) + " (ID " + std::to_string(pl->m_level->m_levelID.value()) + ")";
    }
    std::string macroLine = "Macro: " + sourceSummary(a);
    bool mismatch = false;
    if (a.source.macroMeta && a.source.macroMeta->levelId != 0 && pl && pl->m_level &&
        static_cast<int64_t>(a.source.macroMeta->levelId) != static_cast<int64_t>(pl->m_level->m_levelID.value())) {
        mismatch = true;
    }

    std::string text =
        levelLine + "\n" + macroLine +
        "\n\nEach selected input is replayed from the start of the level with that one input moved earlier and later, "
        "one tick at a time, until the player dies" + std::string(s.tap ? " (as a " + std::to_string(s.tapTicks) + "-tick tap, like a frame-step tool)" : s.wholeClick ? " (the press moves together with its release)" : " (only the press moves)") + ". Max shift " + std::to_string(s.maxShift) + " ticks, survival horizon " +
        num(s.horizonSeconds, 2) + " s" + (s.recheck ? ", edges rechecked" : "") + (s.determinismCheck ? ", determinism checked" : "") +
        ". Your inputs are ignored while it runs; press Esc to stop. Practice-mode, start positions and completions are never saved.";
    auto area = SimpleTextArea::create(text, "chatFont.fnt", .65f, 350.f);
    area->setAnchorPoint({.5f, 1.f});
    area->setPosition({190.f, 212.f});
    m_mainLayer->addChild(area);

    if (mismatch) {
        auto warn = label("Warning: the macro was recorded on level ID " + std::to_string(a.source.macroMeta->levelId) +
            ". On another level the results are meaningless.", "chat", .6f, kBad, {.5f, .5f});
        warn->setPosition({190.f, 104.f});
        warn->limitLabelWidth(350.f, .6f, .3f);
        m_mainLayer->addChild(warn);
    }

    auto menu = CCMenu::create();
    menu->setPosition({0.f, 0.f});
    m_mainLayer->addChild(menu);

    auto rangeLabel = label("Inputs (all, 12, or 10-80):", "big", .32f);
    rangeLabel->setPosition({20.f, 82.f});
    m_mainLayer->addChild(rangeLabel);
    m_range = TextInput::create(110.f, "all", "chatFont.fnt");
    m_range->setFilter("0123456789-alAL ");
    m_range->setString("all");
    m_range->setPosition({300.f, 82.f});
    m_range->setCallback([this](std::string const&) { recompute(); });
    m_mainLayer->addChild(m_range);

    toggle(menu, m_mainLayer, {30.f, 60.f}, "Only inputs not measured yet", false, [this](bool v) {
        m_onlyUnmeasured = v;
        recompute();
    }, .5f);

    m_summary = label("", "chat", .6f, kAccent);
    m_summary->setPosition({20.f, 40.f});
    m_mainLayer->addChild(m_summary);

    auto start = button("Start", .65f, 0, [this](auto) {
        std::string err;
        auto targets = selectedTargets(&err);
        if (targets.empty()) {
            FLAlertLayer::create("Measure", err, "OK")->show();
            return;
        }
        auto cb = m_onStart;
        this->onClose(nullptr);
        if (cb) cb(std::move(targets));
    });
    start->setPosition({320.f, 25.f});
    menu->addChild(start);
    recompute();
    (void)a;
    return true;
}

void MeasureSetupPopup::recompute() {
    std::string err;
    auto targets = selectedTargets(&err);
    if (targets.empty()) {
        m_summary->setString(err.c_str());
        m_summary->setColor(kWarn);
    } else {
        auto const& a = Session::get().analysis();
        // Rough cost: each trial replays from the level start to the input.
        double seconds = 0.0;
        for (auto i : targets) seconds += a.inputs[i].time;
        std::string est = std::to_string(targets.size()) + " input(s), roughly " +
            std::to_string(targets.size() * 10) + " replays covering ~" + formatDuration(seconds * 10.0) + " of gameplay";
        m_summary->setString(est.c_str());
        m_summary->setColor(kAccent);
    }
    m_summary->limitLabelWidth(270.f, .6f, .3f);
}

// ---------------------------------------------------------------------------
// ValuePromptPopup
// ---------------------------------------------------------------------------

ValuePromptPopup* ValuePromptPopup::create(std::string const& title, std::string const& text, std::string const& initial,
                                           std::function<void(std::string const&)> onOk) {
    auto ret = new ValuePromptPopup();
    if (ret->init(title, text, initial, std::move(onOk))) {
        ret->autorelease();
        return ret;
    }
    delete ret;
    return nullptr;
}

bool ValuePromptPopup::init(std::string const& title, std::string const& text, std::string const& initial,
                            std::function<void(std::string const&)> onOk) {
    if (!Popup::init(300.f, 170.f)) return false;
    m_onOk = std::move(onOk);
    this->setTitle(title.c_str());
    auto area = SimpleTextArea::create(text, "chatFont.fnt", .65f, 270.f);
    area->setAnchorPoint({.5f, 1.f});
    area->setPosition({150.f, 135.f});
    m_mainLayer->addChild(area);
    m_input = TextInput::create(160.f, "value", "chatFont.fnt");
    m_input->setCommonFilter(CommonFilter::Float);
    m_input->setString(initial);
    m_input->setPosition({150.f, 62.f});
    m_mainLayer->addChild(m_input);
    auto menu = CCMenu::create();
    menu->setPosition({0.f, 0.f});
    m_mainLayer->addChild(menu);
    auto ok = button("OK", .6f, 0, [this](auto) {
        std::string value = m_input->getString();
        auto cb = m_onOk;
        this->onClose(nullptr);
        if (cb) cb(value);
    });
    ok->setPosition({150.f, 24.f});
    menu->addChild(ok);
    return true;
}

} // namespace fwa::ui
