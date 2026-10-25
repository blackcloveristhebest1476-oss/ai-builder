#include "AnalyzerPopup.hpp"
#include "../Sounds.hpp"
#include "../core/Util.hpp"

#include <Geode/binding/CCMenuItemToggler.hpp>
#include <Geode/binding/FLAlertLayer.hpp>

#include <algorithm>

using namespace geode::prelude;

namespace fwa::ui {

namespace {

constexpr int kRowsPerPage = 9;

class SoundsTab : public Tab {
public:
    static SoundsTab* create(AnalyzerPopup* popup, CCSize size) {
        auto ret = new SoundsTab();
        if (ret->init(popup, size)) {
            ret->autorelease();
            return ret;
        }
        delete ret;
        return nullptr;
    }

    void refresh() override {
        m_enabled->toggle(sounds::enabled());
        m_volumeLabel->setString(fmt::format("{}%", static_cast<int>(std::round(sounds::volume() * 100))).c_str());
        rebuildRows();
    }

private:
    CCMenu* m_menu = nullptr;      // static controls
    CCMenu* m_rowMenu = nullptr;   // per-range rows (rebuilt)
    CCNode* m_rows = nullptr;
    CCMenuItemToggler* m_enabled = nullptr;
    CCLabelBMFont* m_volumeLabel = nullptr;
    CCLabelBMFont* m_pageLabel = nullptr;
    int m_page = 0;
    geode::async::TaskHolder<file::PickResult> m_pick;

    bool init(AnalyzerPopup* popup, CCSize size) {
        if (!initTab(popup, size)) return false;
        m_menu = CCMenu::create();
        m_menu->setPosition({0.f, 0.f});
        this->addChild(m_menu, 5);
        m_rowMenu = CCMenu::create();
        m_rowMenu->setPosition({0.f, 0.f});
        this->addChild(m_rowMenu, 5);
        m_rows = CCNode::create();
        this->addChild(m_rows, 4);

        float top = size.height - 13.f;
        m_enabled = toggle(m_menu, this, {14.f, top}, "Play sounds during playback", sounds::enabled(),
            [](bool v) { sounds::setEnabled(v); }, .45f);

        auto vol = label("Volume", "big", .3f, kText);
        vol->setPosition({250.f, top});
        this->addChild(vol);
        auto minus = button("-", .38f, 2, [this](auto) { changeVolume(-.1f); }, 30.f);
        minus->setPosition({310.f, top});
        m_menu->addChild(minus);
        m_volumeLabel = label("", "big", .32f, kAccent, {.5f, .5f});
        m_volumeLabel->setPosition({345.f, top});
        this->addChild(m_volumeLabel);
        auto plus = button("+", .38f, 2, [this](auto) { changeVolume(.1f); }, 30.f);
        plus->setPosition({380.f, top});
        m_menu->addChild(plus);

        auto hint = label("Each frame-window range can play a sound when one of its clicks is played back.",
            "chat", .45f, kMuted);
        hint->setPosition({10.f, top - 17.f});
        hint->limitLabelWidth(size.width - 20.f, .45f, .2f);
        this->addChild(hint);

        auto prev = CCMenuItemExt::createSpriteExtraWithFrameName("GJ_arrow_03_001.png", .3f, [this](auto) { changePage(-1); });
        prev->setPosition({190.f, 9.f});
        m_menu->addChild(prev);
        auto next = CCMenuItemExt::createSpriteExtraWithFrameName("GJ_arrow_03_001.png", .3f, [this](auto) { changePage(1); });
        static_cast<CCSprite*>(next->getNormalImage())->setFlipX(true);
        next->setPosition({260.f, 9.f});
        m_menu->addChild(next);
        m_pageLabel = label("", "big", .28f, kText, {.5f, .5f});
        m_pageLabel->setPosition({225.f, 9.f});
        this->addChild(m_pageLabel);
        return true;
    }

    void changeVolume(float delta) {
        sounds::setVolume(std::round((sounds::volume() + delta) * 10.f) / 10.f);
        m_volumeLabel->setString(fmt::format("{}%", static_cast<int>(std::round(sounds::volume() * 100))).c_str());
    }

    void changePage(int delta) {
        auto edges = Settings::load().stats().edges;
        int pages = std::max(1, static_cast<int>((edges.size() + kRowsPerPage - 1) / kRowsPerPage));
        m_page = std::clamp(m_page + delta, 0, pages - 1);
        defer(this, [this] { rebuildRows(); });
    }

    /// Choices for the < > buttons: None, then every preset.
    static std::vector<std::string> choices() {
        std::vector<std::string> out = {""};
        for (auto const& p : sounds::presets()) out.push_back(p.file);
        return out;
    }

    void cycle(double edge, int dir) {
        auto list = choices();
        auto current = sounds::soundForEdge(edge);
        auto it = std::find(list.begin(), list.end(), current);
        int idx = it == list.end() ? 0 : static_cast<int>(it - list.begin());
        idx = (idx + dir + static_cast<int>(list.size())) % static_cast<int>(list.size());
        sounds::setSoundForEdge(edge, list[idx]);
        sounds::play(list[idx]);
        defer(this, [this] { rebuildRows(); });
    }

    void pickCustom(double edge) {
        file::FilePickOptions opts;
        opts.filters.push_back({"Audio", {"*.ogg", "*.mp3", "*.wav"}});
        m_pick.spawn(file::pick(file::PickMode::OpenFile, opts), [this, edge](file::PickResult res) {
            if (!res) {
                FLAlertLayer::create("Sound", "The file picker failed: " + std::string(res.unwrapErr()), "OK")->show();
                return;
            }
            auto picked = std::move(res).unwrap();
            if (!picked) return;
            auto path = utils::string::pathToString(*picked);
            sounds::setSoundForEdge(edge, path);
            sounds::play(path);
            rebuildRows();
        });
    }

    void rebuildRows() {
        m_rows->removeAllChildren();
        m_rowMenu->removeAllChildren();
        auto s = Settings::load();
        auto stats = s.stats();
        auto const& edges = stats.edges;
        auto labels = bucketLabels(edges, stats.integerLabels);
        size_t n = edges.size();
        int pages = std::max(1, static_cast<int>((n + kRowsPerPage - 1) / kRowsPerPage));
        m_page = std::clamp(m_page, 0, pages - 1);
        m_pageLabel->setString(fmt::format("Page {}/{}", m_page + 1, pages).c_str());

        float y = m_size.height - 50.f;
        float rowH = 14.5f;
        // Narrowest ranges first: they are the ones most worth hearing.
        for (int i = 0; i < kRowsPerPage; i++) {
            size_t bucket = static_cast<size_t>(m_page * kRowsPerPage + i);
            if (bucket >= n) break;
            double edge = edges[bucket];
            auto color = bucketColor(bucket, n);
            auto name = label(labels[bucket], "big", .36f, color);
            name->setPosition({14.f, y});
            m_rows->addChild(name);

            auto current = sounds::soundForEdge(edge);
            auto sound = label(sounds::displayName(current), "chat", .55f, current.empty() ? kMuted : kText, {.5f, .5f});
            sound->setPosition({190.f, y});
            sound->limitLabelWidth(110.f, .55f, .25f);
            m_rows->addChild(sound);

            auto left = button("<", .34f, 2, [this, edge](auto) { cycle(edge, -1); }, 30.f);
            left->setPosition({120.f, y});
            m_rowMenu->addChild(left);
            auto right = button(">", .34f, 2, [this, edge](auto) { cycle(edge, 1); }, 30.f);
            right->setPosition({260.f, y});
            m_rowMenu->addChild(right);
            auto custom = button("File...", .34f, 1, [this, edge](auto) { pickCustom(edge); }, 90.f);
            custom->setPosition({310.f, y});
            m_rowMenu->addChild(custom);
            auto test = button("Test", .34f, 0, [current](auto) { sounds::play(current); }, 70.f);
            test->setPosition({360.f, y});
            m_rowMenu->addChild(test);
            auto clear = button("x", .34f, 3, [this, edge](auto) {
                sounds::setSoundForEdge(edge, "");
                defer(this, [this] { rebuildRows(); });
            }, 30.f);
            clear->setPosition({400.f, y});
            m_rowMenu->addChild(clear);
            y -= rowH;
        }
    }
};

} // namespace

Tab* createSoundsTab(AnalyzerPopup* popup, CCSize size) {
    return SoundsTab::create(popup, size);
}

} // namespace fwa::ui
