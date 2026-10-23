#pragma once

#include <Geode/Geode.hpp>
#include <Geode/ui/Popup.hpp>
#include <Geode/ui/TextInput.hpp>
#include <Geode/ui/LoadingSpinner.hpp>
#include <Geode/utils/async.hpp>
#include <Geode/utils/web.hpp>

#include "Design.hpp"

class AIBuilderPopup : public geode::Popup {
protected:
    EditorUI* m_editorUI = nullptr;
    ai_builder::SelectionInfo m_selection;

    geode::TextInput* m_input = nullptr;
    CCMenuItemToggler* m_replaceToggle = nullptr;
    cocos2d::CCLabelBMFont* m_replaceLabel = nullptr;
    cocos2d::CCLabelBMFont* m_statusLabel = nullptr;
    CCMenuItemSpriteExtra* m_generateBtn = nullptr;
    geode::LoadingSpinner* m_spinner = nullptr;

    geode::async::TaskHolder<geode::utils::web::WebResponse> m_task;
    bool m_busy = false;

    bool init(EditorUI* ui);

    void onGenerate(cocos2d::CCObject*);
    void onSettings(cocos2d::CCObject*);
    void onExample(cocos2d::CCObject*);
    void onClose(cocos2d::CCObject*) override;

    void setBusy(bool busy, std::string const& status);
    void setStatus(std::string const& status, cocos2d::ccColor3B color = {255, 255, 255});
    void handleResponse(geode::utils::web::WebResponse res);

public:
    static AIBuilderPopup* create(EditorUI* ui);
};
