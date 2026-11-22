#pragma once

#include <Geode/Geode.hpp>
#include <Geode/utils/async.hpp>
#include <Geode/utils/web.hpp>

#include "Recommender.hpp"

class PickerPopup : public geode::Popup {
protected:
    geode::async::TaskHolder<geode::utils::web::WebResponse> m_request;
    std::vector<picker::Level> m_levels;
    std::vector<int> m_pickIds;

    // Everything that gets rebuilt when the data changes lives in here
    cocos2d::CCNode* m_body = nullptr;

    bool init() override;

    void load(bool forceRefresh);
    void fetch();
    bool useCache();

    void clearBody();
    void showLoading();
    void showMessage(std::string const& text);
    void showResults();

    cocos2d::CCNode* createRow(int rank, picker::Pick const& pick, float width, float height);
    void openAll();

public:
    static PickerPopup* create();
};
