#include <Geode/Geode.hpp>
#include <Geode/modify/EditorUI.hpp>
#include <Geode/binding/ButtonSprite.hpp>

#include "AIBuilderPopup.hpp"

using namespace geode::prelude;

class $modify(AIEditorUI, EditorUI) {
    bool init(LevelEditorLayer* layer) {
        if (!EditorUI::init(layer)) return false;

        auto spr = ButtonSprite::create("AI", "bigFont.fnt", "GJ_button_04.png", .8f);
        spr->setScale(.75f);
        auto btn = CCMenuItemSpriteExtra::create(spr, this, menu_selector(AIEditorUI::onAIBuilder));
        btn->setID("ai-builder-button"_spr);

        // Top-left next to undo / redo / delete (always visible in the editor).
        if (auto menu = this->getChildByID("undo-menu")) {
            menu->addChild(btn);
            menu->updateLayout();
        } else {
            auto winSize = CCDirector::get()->getWinSize();
            auto menu2 = CCMenu::create();
            menu2->setID("ai-builder-menu"_spr);
            menu2->setPosition(ccp(winSize.width / 2 - 120.f, winSize.height - 25.f));
            menu2->addChild(btn);
            this->addChild(menu2, 100);
        }
        return true;
    }

    void onAIBuilder(CCObject*) {
        if (auto popup = AIBuilderPopup::create(this)) {
            popup->show();
        }
    }
};
