// Click Corrector - pause-menu button and the small control popup.

#include "Assist.hpp"

#include <Geode/Geode.hpp>
#include <Geode/binding/ButtonSprite.hpp>
#include <Geode/modify/PauseLayer.hpp>
#include <Geode/ui/GeodeUI.hpp>
#include <Geode/ui/Notification.hpp>
#include <Geode/ui/Popup.hpp>
#include <Geode/ui/TextArea.hpp>
#include <Geode/utils/async.hpp>
#include <Geode/utils/file.hpp>

using namespace geode::prelude;
using cc::Assist;

namespace {

constexpr float kWidth = 330.f;
constexpr float kHeight = 230.f;

CCMenuItemSpriteExtra* textButton(char const* text, char const* bg, float scale,
                                  std::function<void()> callback) {
    auto spr = ButtonSprite::create(text, "bigFont.fnt", bg, .8f);
    spr->setScale(scale);
    return CCMenuItemExt::createSpriteExtra(spr, [cb = std::move(callback)](CCMenuItemSpriteExtra*) {
        if (cb) cb();
    });
}

class CorrectorPopup : public Popup {
public:
    static CorrectorPopup* create() {
        auto ret = new CorrectorPopup();
        if (ret->init()) {
            ret->autorelease();
            return ret;
        }
        delete ret;
        return nullptr;
    }

protected:
    bool init() {
        if (!Popup::init(kWidth, kHeight)) return false;
        this->setTitle("Click Corrector");

        auto menu = CCMenu::create();
        menu->setPosition({0.f, 0.f});
        m_mainLayer->addChild(menu, 2);

        // On / off
        auto toggler = CCMenuItemExt::createTogglerWithStandardSprites(.7f, [this](CCMenuItemToggler* item) {
            // GD calls the handler before flipping the state.
            Mod::get()->setSettingValue<bool>("enabled", !item->isToggled());
            Assist::get().reloadSettings();
            refresh();
        });
        toggler->toggle(Mod::get()->getSettingValue<bool>("enabled"));
        toggler->setPosition({38.f, 96.f});
        menu->addChild(toggler);
        auto onLabel = CCLabelBMFont::create("Correct my clicks", "bigFont.fnt");
        onLabel->setScale(.4f);
        onLabel->setAnchorPoint({0.f, .5f});
        onLabel->setPosition({56.f, 96.f});
        m_mainLayer->addChild(onLabel);

        // Frames early:  [-]  10  [+]
        auto framesCaption = CCLabelBMFont::create("Frames early", "bigFont.fnt");
        framesCaption->setScale(.4f);
        framesCaption->setAnchorPoint({0.f, .5f});
        framesCaption->setPosition({24.f, 64.f});
        m_mainLayer->addChild(framesCaption);

        auto minus = textButton("-", "GJ_button_04.png", .7f, [this] { changeFrames(-1); });
        minus->setPosition({160.f, 64.f});
        menu->addChild(minus);
        auto plus = textButton("+", "GJ_button_04.png", .7f, [this] { changeFrames(1); });
        plus->setPosition({250.f, 64.f});
        menu->addChild(plus);
        m_frames = CCLabelBMFont::create("", "goldFont.fnt");
        m_frames->setScale(.7f);
        m_frames->setPosition({205.f, 65.f});
        m_mainLayer->addChild(m_frames);
        m_framesMs = CCLabelBMFont::create("", "chatFont.fnt");
        m_framesMs->setScale(.6f);
        m_framesMs->setAnchorPoint({0.f, .5f});
        m_framesMs->setPosition({268.f, 64.f});
        m_mainLayer->addChild(m_framesMs);

        // Bottom row
        auto import = textButton("Import Macro", "GJ_button_01.png", .55f, [this] { onImport(); });
        import->setPosition({75.f, 28.f});
        menu->addChild(import);
        auto remove = textButton("Delete", "GJ_button_06.png", .55f, [this] { onDelete(); });
        remove->setPosition({176.f, 28.f});
        menu->addChild(remove);
        auto settings = textButton("Settings", "GJ_button_04.png", .55f, [] {
            geode::openSettingsPopup(Mod::get());
        });
        settings->setPosition({262.f, 28.f});
        menu->addChild(settings);

        refresh();
        return true;
    }

    void changeFrames(int by) {
        auto mod = Mod::get();
        int64_t value = std::clamp<int64_t>(mod->getSettingValue<int64_t>("frames-early") + by, 1, 120);
        mod->setSettingValue<int64_t>("frames-early", value);
        Assist::get().reloadSettings();
        refresh();
    }

    void refresh() {
        auto& assist = Assist::get();
        int64_t frames = Mod::get()->getSettingValue<int64_t>("frames-early");
        m_frames->setString(std::to_string(frames).c_str());
        m_framesMs->setString(fmt::format("{:.0f} ms", static_cast<double>(frames) * 1000.0 / 240.0).c_str());

        std::string text;
        if (!assist.layer()) {
            text = "Open a level to use Click Corrector.";
        } else if (!assist.unsupported().empty()) {
            text = assist.unsupported();
        } else if (!assist.hasReference()) {
            text = "No reference run for this level yet, so nothing can be corrected.\n"
                   "Beat the level once (practice mode with checkpoints counts) or import a macro of it.";
        } else {
            text = fmt::format("Reference: {} clicks, from {}.\n", assist.referenceClicks(), assist.referenceSource());
            text += assist.enabled()
                ? fmt::format("A click 1 to {} frames early is moved to the right frame. Corrected this attempt: {}.",
                      frames, assist.corrected())
                : std::string("Correction is switched off.");
        }
        if (m_status) m_status->removeFromParent();
        m_status = SimpleTextArea::create(text, "chatFont.fnt", .7f, kWidth - 40.f);
        m_status->setAnchorPoint({.5f, 1.f});
        m_status->setPosition({kWidth / 2, kHeight - 38.f});
        m_mainLayer->addChild(m_status);
    }

    void onImport() {
        if (!Assist::get().layer()) {
            FLAlertLayer::create("Import", "Open the level first.", "OK")->show();
            return;
        }
        file::FilePickOptions opts;
        opts.filters.push_back({"Macros", {"*.gdr2", "*.gdr", "*.json"}});
        opts.filters.push_back({"All files", {"*.*"}});
        m_pick.spawn(file::pick(file::PickMode::OpenFile, opts), [this](file::PickResult res) {
            if (!res) {
                FLAlertLayer::create("Import", "The file picker failed: " + std::string(res.unwrapErr()), "OK")->show();
                return;
            }
            auto picked = std::move(res).unwrap();
            if (!picked) return; // cancelled
            std::string note;
            if (auto err = Assist::get().importFile(*picked, &note)) {
                FLAlertLayer::create("Could not import", *err, "OK")->show();
                return;
            }
            Notification::create("Macro imported as this level's reference", NotificationIcon::Success)->show();
            if (!note.empty()) FLAlertLayer::create("Check the level", note + " It is used anyway.", "OK")->show();
            refresh();
        });
    }

    void onDelete() {
        if (!Assist::get().hasReference()) return;
        geode::createQuickPopup("Delete reference",
            "Delete this level's reference run? Clicks cannot be corrected here until you beat the level again "
            "or import a macro.",
            "Cancel", "Delete", [this](FLAlertLayer*, bool second) {
                if (!second) return;
                Assist::get().deleteReference();
                refresh();
            });
    }

    CCLabelBMFont* m_frames = nullptr;
    CCLabelBMFont* m_framesMs = nullptr;
    SimpleTextArea* m_status = nullptr;
    async::TaskHolder<file::PickResult> m_pick;
};

} // namespace

class $modify(CorrectorPauseLayer, PauseLayer) {
    void customSetup() {
        PauseLayer::customSetup();

        auto sprite = ButtonSprite::create("CC", "bigFont.fnt", "GJ_button_04.png", .7f);
        sprite->setScale(.6f);
        auto btn = CCMenuItemExt::createSpriteExtra(sprite, [](auto) {
            if (auto popup = CorrectorPopup::create()) popup->show();
        });
        btn->setID("click-corrector-button"_spr);

        // Prefer the right-side menu provided by the node-ids mod.
        if (auto menu = this->getChildByID("right-button-menu")) {
            menu->addChild(btn);
            menu->updateLayout();
        } else {
            auto winSize = CCDirector::get()->getWinSize();
            auto menu2 = CCMenu::create();
            menu2->setID("click-corrector-menu"_spr);
            menu2->setPosition({winSize.width - 30.f, winSize.height / 2});
            menu2->addChild(btn);
            this->addChild(menu2, 10);
        }
    }
};
