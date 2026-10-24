#pragma once

#include "../Session.hpp"
#include "Theme.hpp"

#include <Geode/Geode.hpp>
#include <Geode/ui/Popup.hpp>
#include <Geode/ui/TextInput.hpp>
#include <Geode/utils/async.hpp>
#include <Geode/utils/file.hpp>

#include <functional>
#include <string>
#include <vector>

namespace fwa::ui {

class AnalyzerPopup;

/// Base class for one tab of the analyzer. Each tab rebuilds its content in
/// refresh(), which the popup calls whenever the session data or settings change.
class Tab : public cocos2d::CCNode {
public:
    virtual void refresh() = 0;
    /// Called every frame while the tab is visible.
    virtual void tick(float) {}

protected:
    bool initTab(AnalyzerPopup* popup, cocos2d::CCSize size);
    AnalyzerPopup* m_popup = nullptr;
    cocos2d::CCSize m_size;
};

class AnalyzerPopup : public geode::Popup {
public:
    enum TabId { Dashboard = 0, Inputs, Graphs, Precision, SettingsTabId, TabCount };

    static AnalyzerPopup* create(int tab = Dashboard);
    /// Opens the analyzer (pausing the level first if one is running).
    static void open(int tab = Dashboard);
    static AnalyzerPopup* current();

    void selectTab(int tab);
    void refreshAll();
    void showInputDetail(size_t recordIndex);

    // actions
    void onImport();
    void onSample();
    void onMeasure();
    void onPlayback();
    void onRecord();
    void onExport();
    void onLoadSaved();
    void setFooter(std::string const& text, cocos2d::ccColor3B color = kMuted);
    void onClose(cocos2d::CCObject*) override;

protected:
    bool init(int tab);
    void update(float dt) override;
    ~AnalyzerPopup() override;

    void buildTabBar();
    void pickAndImport(std::optional<std::filesystem::path> startDir);
    void exportTo(std::string const& kind);

    std::vector<Tab*> m_tabs;
    cocos2d::CCMenu* m_tabMenu = nullptr;
    int m_tab = Dashboard;
    uint64_t m_seenRevision = 0;
    std::string m_seenSettings;
    cocos2d::CCLabelBMFont* m_footer = nullptr;
    geode::async::TaskHolder<geode::utils::file::PickResult> m_pick;
};

/// Shown before an in-game measurement starts.
class MeasureSetupPopup : public geode::Popup {
public:
    static MeasureSetupPopup* create(std::function<void(std::vector<size_t>)> onStart);

protected:
    bool init(std::function<void(std::vector<size_t>)> onStart);
    void recompute();
    std::vector<size_t> selectedTargets(std::string* error) const;

    std::function<void(std::vector<size_t>)> m_onStart;
    geode::TextInput* m_range = nullptr;
    cocos2d::CCLabelBMFont* m_summary = nullptr;
    bool m_onlyUnmeasured = false;
};

/// Detail view for one input (with manual window entry).
class InputDetailPopup : public geode::Popup {
public:
    static InputDetailPopup* create(size_t recordIndex);

protected:
    bool init(size_t recordIndex);
    size_t m_index = 0;
    geode::TextInput* m_manual = nullptr;
};

/// Small text-entry popup.
class ValuePromptPopup : public geode::Popup {
public:
    static ValuePromptPopup* create(std::string const& title, std::string const& text, std::string const& initial,
                                    std::function<void(std::string const&)> onOk);

protected:
    bool init(std::string const& title, std::string const& text, std::string const& initial,
              std::function<void(std::string const&)> onOk);
    geode::TextInput* m_input = nullptr;
    std::function<void(std::string const&)> m_onOk;
};

// Tab factories (implemented in their own files)
Tab* createDashboardTab(AnalyzerPopup* popup, cocos2d::CCSize size);
Tab* createInputsTab(AnalyzerPopup* popup, cocos2d::CCSize size);
Tab* createGraphsTab(AnalyzerPopup* popup, cocos2d::CCSize size);
Tab* createPrecisionTab(AnalyzerPopup* popup, cocos2d::CCSize size);
Tab* createSettingsTab(AnalyzerPopup* popup, cocos2d::CCSize size);

/// Common text for the current macro/source, used in several tabs.
std::string sourceSummary(Analysis const& a);
std::string windowText(InputRecord const& r, Settings const& s);

} // namespace fwa::ui
