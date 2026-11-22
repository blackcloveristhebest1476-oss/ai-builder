#include "PickerPopup.hpp"
#include "Aredl.hpp"

#include <Geode/binding/ButtonSprite.hpp>
#include <Geode/binding/GJGameLevel.hpp>
#include <Geode/binding/GJSearchObject.hpp>
#include <Geode/binding/GameLevelManager.hpp>
#include <Geode/binding/GameStatsManager.hpp>
#include <Geode/binding/LevelBrowserLayer.hpp>
#include <Geode/ui/GeodeUI.hpp>
#include <Geode/ui/LoadingSpinner.hpp>
#include <Geode/ui/ScrollLayer.hpp>
#include <Geode/utils/file.hpp>

#include <chrono>

using namespace geode::prelude;

namespace {
    constexpr float POPUP_WIDTH = 440.f;
    constexpr float POPUP_HEIGHT = 290.f;
    constexpr float LIST_WIDTH = 400.f;
    constexpr float LIST_HEIGHT = 150.f;
    constexpr float ROW_HEIGHT = 38.f;

    // The list changes rarely; AREDL itself caches this endpoint for 15 minutes.
    constexpr int64_t CACHE_SECONDS = 6 * 60 * 60;
    // Pressing Refresh won't re-download a list newer than this.
    constexpr int64_t REFRESH_SECONDS = 10 * 60;

    std::filesystem::path cachePath() {
        return Mod::get()->getSaveDir() / "aredl-levels.json";
    }

    int64_t now() {
        return std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch()
        ).count();
    }

    // Every online level the player has 100% in normal mode.
    std::unordered_set<int> completedLevelIds(std::vector<picker::Level> const& levels) {
        std::unordered_set<int> ids;

        // Levels still in the saved levels list
        auto glm = GameLevelManager::get();
        if (glm && glm->m_onlineLevels) {
            for (auto [key, level] : CCDictionaryExt<gd::string, GJGameLevel*>(glm->m_onlineLevels)) {
                if (level && level->m_normalPercent.value() >= 100) {
                    ids.insert(level->m_levelID.value());
                }
            }
        }

        // Completions the game remembers even after a level was deleted
        if (auto gsm = GameStatsManager::get()) {
            for (auto const& level : levels) {
                if (gsm->hasCompletedOnlineLevel(level.id)) ids.insert(level.id);
            }
        }

        // Manually added IDs (e.g. beaten on another account)
        auto extra = Mod::get()->getSettingValue<std::string>("extra-completed");
        int current = 0;
        bool hasDigits = false;
        for (char c : extra + ",") {
            if (c >= '0' && c <= '9') {
                if (current < 100000000) current = current * 10 + (c - '0');
                hasDigits = true;
            } else {
                if (hasDigits) ids.insert(current);
                current = 0;
                hasDigits = false;
            }
        }
        return ids;
    }

    picker::Options readOptions() {
        auto mod = Mod::get();
        picker::Options options;

        auto mode = mod->getSettingValue<std::string>("mode");
        if (mode == "New hardest only") options.mode = picker::Mode::Harder;
        else if (mode == "Easier levels only") options.mode = picker::Mode::Easier;
        else options.mode = picker::Mode::Both;

        options.maxJump = static_cast<int>(mod->getSettingValue<int64_t>("max-jump"));
        options.count = static_cast<int>(mod->getSettingValue<int64_t>("pick-count"));
        options.includeLegacy = mod->getSettingValue<bool>("include-legacy");
        options.includeTwoPlayer = mod->getSettingValue<bool>("include-2p");
        options.useEnjoyment = mod->getSettingValue<bool>("use-enjoyment");
        return options;
    }

    std::string join(std::vector<std::string> const& parts, size_t max) {
        std::string out;
        for (size_t i = 0; i < parts.size() && i < max; i++) {
            if (i) out += ", ";
            out += parts[i];
        }
        return out;
    }

    void openSearch(SearchType type, std::string const& query) {
        auto search = GJSearchObject::create(type, query);
        CCDirector::get()->pushScene(
            CCTransitionFade::create(.5f, LevelBrowserLayer::scene(search))
        );
    }
}

PickerPopup* PickerPopup::create() {
    auto ret = new PickerPopup();
    if (ret->init()) {
        ret->autorelease();
        return ret;
    }
    delete ret;
    return nullptr;
}

bool PickerPopup::init() {
    if (!Popup::init(POPUP_WIDTH, POPUP_HEIGHT)) return false;

    this->setTitle("Extreme Picker");

    m_body = CCNode::create();
    m_body->setContentSize(m_mainLayer->getContentSize());
    m_body->setAnchorPoint({ 0.f, 0.f });
    m_body->setPosition(0.f, 0.f);
    m_mainLayer->addChild(m_body);

    // Bottom buttons
    auto makeButton = [](const char* text, const char* texture, auto&& callback) {
        auto sprite = ButtonSprite::create(text, "goldFont.fnt", texture, .7f);
        sprite->setScale(.75f);
        return CCMenuItemExt::createSpriteExtra(sprite, std::forward<decltype(callback)>(callback));
    };
    // Refresh re-reads your settings and completions; the list itself is only
    // downloaded again if the saved copy is more than a few minutes old.
    auto refresh = makeButton("Refresh", "GJ_button_01.png", [this](auto) { this->load(true); });
    auto all = makeButton("Open All", "GJ_button_02.png", [this](auto) { this->openAll(); });
    auto settings = makeButton("Settings", "GJ_button_04.png", [](auto) {
        openSettingsPopup(Mod::get());
    });

    m_buttonMenu->addChildAtPosition(refresh, Anchor::Bottom, { -120.f, 24.f });
    m_buttonMenu->addChildAtPosition(all, Anchor::Bottom, { 0.f, 24.f });
    m_buttonMenu->addChildAtPosition(settings, Anchor::Bottom, { 120.f, 24.f });

    this->load(false);
    return true;
}

// ---------------------------------------------------------------------------
// Data
// ---------------------------------------------------------------------------

bool PickerPopup::useCache() {
    auto body = file::readString(cachePath());
    if (!body) return false;
    auto levels = picker::parseLevels(body.unwrap());
    if (levels.empty()) return false;
    m_levels = std::move(levels);
    return true;
}

void PickerPopup::load(bool forceRefresh) {
    if (m_request.isPending()) return;

    auto age = now() - Mod::get()->getSavedValue<int64_t>("cache-time", 0);
    auto maxAge = forceRefresh ? REFRESH_SECONDS : CACHE_SECONDS;
    if (age >= 0 && age < maxAge && this->useCache()) {
        this->showResults();
        return;
    }
    this->fetch();
}

void PickerPopup::fetch() {
    this->showLoading();

    auto request = web::WebRequest();
    request.userAgent("ExtremePicker (Geode mod)");
    request.timeout(std::chrono::seconds(20));

    m_request.spawn(
        request.get(picker::AREDL_LEVELS_URL),
        [this](web::WebResponse response) {
            if (response.ok()) {
                auto body = response.string().unwrapOr("");
                auto levels = picker::parseLevels(body);
                if (!levels.empty()) {
                    m_levels = std::move(levels);
                    if (file::writeString(cachePath(), body)) {
                        Mod::get()->setSavedValue<int64_t>("cache-time", now());
                    }
                    this->showResults();
                    return;
                }
            }

            // Offline or the API is down: an old list is better than nothing
            if (this->useCache()) {
                Notification::create(
                    "Couldn't reach AREDL, using saved list", NotificationIcon::Warning
                )->show();
                this->showResults();
                return;
            }
            this->showMessage(fmt::format(
                "Couldn't load the AREDL list (HTTP {}).\nCheck your connection and press Refresh.",
                response.code()
            ));
        }
    );
}

// ---------------------------------------------------------------------------
// UI
// ---------------------------------------------------------------------------

void PickerPopup::clearBody() {
    m_body->removeAllChildren();
    m_pickIds.clear();
}

void PickerPopup::showLoading() {
    this->clearBody();
    auto spinner = LoadingSpinner::create(40.f);
    m_body->addChildAtPosition(spinner, Anchor::Center, { 0.f, 10.f });

    auto label = CCLabelBMFont::create("Loading the AREDL...", "goldFont.fnt");
    label->setScale(.5f);
    m_body->addChildAtPosition(label, Anchor::Center, { 0.f, -30.f });
}

void PickerPopup::showMessage(std::string const& text) {
    this->clearBody();
    auto label = CCLabelBMFont::create(text.c_str(), "bigFont.fnt");
    label->setAlignment(kCCTextAlignmentCenter);
    label->limitLabelWidth(POPUP_WIDTH - 60.f, .45f, .1f);
    m_body->addChildAtPosition(label, Anchor::Center, { 0.f, 10.f });
}

void PickerPopup::showResults() {
    this->clearBody();

    auto report = picker::recommend(m_levels, completedLevelIds(m_levels), readOptions());

    // --- Summary -----------------------------------------------------------
    std::string summary;
    std::string style;
    if (report.hardest) {
        summary = fmt::format(
            "{} extreme{} beaten  -  hardest: {} (#{})",
            report.completedCount, report.completedCount == 1 ? "" : "s",
            report.hardest->name, report.hardest->position
        );
        std::vector<std::string> top;
        for (auto const& stat : report.profile) {
            top.push_back(fmt::format("{} {:.0f}%", stat.tag, stat.userRate * 100.0));
        }
        style = "Your tags: " + join(top, 6);
    } else {
        summary = "No beaten extremes found yet";
        style = "Showing the easiest, most enjoyed extremes to start with";
    }

    auto summaryLabel = CCLabelBMFont::create(summary.c_str(), "bigFont.fnt");
    summaryLabel->limitLabelWidth(LIST_WIDTH, .4f, .1f);
    m_body->addChildAtPosition(summaryLabel, Anchor::Top, { 0.f, -42.f });

    auto styleLabel = CCLabelBMFont::create(style.c_str(), "chatFont.fnt");
    styleLabel->setColor({ 180, 230, 255 });
    styleLabel->limitLabelWidth(LIST_WIDTH, .7f, .1f);
    m_body->addChildAtPosition(styleLabel, Anchor::Top, { 0.f, -58.f });

    if (report.picks.empty()) {
        auto label = CCLabelBMFont::create(
            "No levels match your settings.\nTry a bigger max jump or another mode.", "bigFont.fnt"
        );
        label->setAlignment(kCCTextAlignmentCenter);
        label->limitLabelWidth(LIST_WIDTH - 20.f, .45f, .1f);
        m_body->addChildAtPosition(label, Anchor::Center, { 0.f, -5.f });
        return;
    }

    // --- List ----------------------------------------------------------------
    const float listX = (POPUP_WIDTH - LIST_WIDTH) / 2.f;
    const float listY = 48.f;

    auto bg = CCLayerColor::create({ 0, 0, 0, 90 }, LIST_WIDTH, LIST_HEIGHT);
    bg->setPosition(listX, listY);
    m_body->addChild(bg);

    auto scroll = ScrollLayer::create({ LIST_WIDTH, LIST_HEIGHT });
    scroll->setPosition(listX, listY);
    m_body->addChild(scroll);

    const float contentHeight = std::max(LIST_HEIGHT, ROW_HEIGHT * report.picks.size());
    scroll->m_contentLayer->setContentSize({ LIST_WIDTH, contentHeight });

    int rank = 1;
    for (auto const& pick : report.picks) {
        auto row = this->createRow(rank, pick, LIST_WIDTH, ROW_HEIGHT);
        row->setPosition(0.f, contentHeight - ROW_HEIGHT * rank);
        scroll->m_contentLayer->addChild(row);
        m_pickIds.push_back(pick.level->id);
        rank++;
    }
    scroll->scrollToTop();
}

CCNode* PickerPopup::createRow(int rank, picker::Pick const& pick, float width, float height) {
    auto row = CCNode::create();
    row->setContentSize({ width, height });
    row->setAnchorPoint({ 0.f, 0.f });

    if (rank % 2 == 0) {
        auto stripe = CCLayerColor::create({ 255, 255, 255, 14 }, width, height);
        row->addChild(stripe);
    }

    auto rankLabel = CCLabelBMFont::create(fmt::format("{}", rank).c_str(), "goldFont.fnt");
    rankLabel->limitLabelWidth(22.f, .6f, .1f);
    rankLabel->setPosition(18.f, height / 2.f);
    row->addChild(rankLabel);

    auto nameLabel = CCLabelBMFont::create(pick.level->name.c_str(), "bigFont.fnt");
    nameLabel->setAnchorPoint({ 0.f, .5f });
    nameLabel->limitLabelWidth(width - 120.f, .5f, .1f);
    nameLabel->setPosition(38.f, height - 13.f);
    row->addChild(nameLabel);

    std::string info = fmt::format("#{}", pick.level->position);
    if (pick.level->legacy) info += " (legacy)";
    if (pick.match > 0) info += fmt::format("  |  {:.0f}% match", pick.match * 100.0);
    auto const& tags = pick.sharedTags.empty() ? pick.level->tags : pick.sharedTags;
    if (!tags.empty()) info += "  |  " + join(tags, 5);

    auto infoLabel = CCLabelBMFont::create(info.c_str(), "chatFont.fnt");
    infoLabel->setAnchorPoint({ 0.f, .5f });
    infoLabel->setColor({ 200, 200, 200 });
    infoLabel->limitLabelWidth(width - 120.f, .65f, .1f);
    infoLabel->setPosition(38.f, 10.f);
    row->addChild(infoLabel);

    auto menu = CCMenu::create();
    menu->setPosition(0.f, 0.f);
    menu->setContentSize({ width, height });
    row->addChild(menu);

    auto viewSprite = ButtonSprite::create("View", "goldFont.fnt", "GJ_button_01.png", .7f);
    viewSprite->setScale(.7f);
    auto view = CCMenuItemExt::createSpriteExtra(viewSprite, [this, id = pick.level->id](auto) {
        // Closing the popup destroys this button (and this lambda), so it
        // has to be the very last thing that happens here.
        openSearch(SearchType::Search, fmt::format("{}", id));
        this->onClose(nullptr);
    });
    view->setPosition(width - 38.f, height / 2.f);
    menu->addChild(view);

    return row;
}

void PickerPopup::openAll() {
    if (m_pickIds.empty()) return;
    std::string query;
    for (int id : m_pickIds) {
        if (!query.empty()) query += ",";
        query += fmt::format("{}", id);
    }
    // Same search type the game uses for map packs: a list of level IDs
    openSearch(SearchType::MapPackOnClick, query);
    this->onClose(nullptr);
}
