#include <Geode/Geode.hpp>
#include <Geode/modify/MenuLayer.hpp>
#include <Geode/ui/BasedButtonSprite.hpp>

#include "PickerPopup.hpp"

using namespace geode::prelude;

class $modify(PickerMenuLayer, MenuLayer) {
    bool init() {
        if (!MenuLayer::init()) return false;

        // Extreme demon face on a round button, in the row at the bottom of
        // the main menu.
        if (auto menu = this->getChildByID("bottom-menu")) {
            auto sprite = CircleButtonSprite::createWithSpriteFrameName(
                "diffIcon_10_btn_001.png", 1.f,
                CircleBaseColor::Green, CircleBaseSize::MediumAlt
            );
            auto button = CCMenuItemSpriteExtra::create(
                sprite, this, menu_selector(PickerMenuLayer::onExtremePicker)
            );
            button->setID("picker-button"_spr);
            menu->addChild(button);
            menu->updateLayout();
        }
        return true;
    }

    void onExtremePicker(CCObject*) {
        if (auto popup = PickerPopup::create()) popup->show();
    }
};
