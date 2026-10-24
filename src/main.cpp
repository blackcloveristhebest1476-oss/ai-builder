// Frame Window Analyzer - entry points: pause-menu button and keyboard shortcut.

#include "engine/MeasurementEngine.hpp"
#include "ui/AnalyzerPopup.hpp"

#include <Geode/Geode.hpp>
#include <Geode/binding/ButtonSprite.hpp>
#include <Geode/modify/PauseLayer.hpp>

using namespace geode::prelude;

class $modify(FWAPauseLayer, PauseLayer) {
    void customSetup() {
        PauseLayer::customSetup();

        CCNode* sprite = nullptr;
        if (auto icon = CCSprite::createWithSpriteFrameName("GJ_statsBtn_001.png")) {
            icon->setScale(.62f);
            sprite = icon;
        } else {
            auto bs = ButtonSprite::create("FWA", "bigFont.fnt", "GJ_button_04.png", .7f);
            bs->setScale(.6f);
            sprite = bs;
        }
        auto btn = CCMenuItemExt::createSpriteExtra(sprite, [](auto) {
            fwa::ui::AnalyzerPopup::open();
        });
        btn->setID("frame-window-analyzer-button"_spr);

        // Prefer the right-side menu provided by the node-ids mod.
        if (auto menu = this->getChildByID("right-button-menu")) {
            menu->addChild(btn);
            menu->updateLayout();
        } else {
            auto winSize = CCDirector::get()->getWinSize();
            auto menu2 = CCMenu::create();
            menu2->setID("frame-window-analyzer-menu"_spr);
            menu2->setPosition({winSize.width - 30.f, winSize.height / 2});
            menu2->addChild(btn);
            this->addChild(menu2, 10);
        }
    }
};

$on_mod(Loaded) {
    listenForKeybindSettingPresses("open-analyzer", [](Keybind const&, bool down, bool repeat, double) {
        if (!down || repeat) return;
        // Don't open on top of text fields or while a measurement drives the game.
        if (fwa::MeasurementEngine::get()->active()) return;
        fwa::ui::AnalyzerPopup::open();
    });
}
