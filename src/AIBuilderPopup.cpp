#include "AIBuilderPopup.hpp"
#include "AIClient.hpp"
#include "Builder.hpp"

#include <Geode/binding/ButtonSprite.hpp>
#include <Geode/binding/CCMenuItemToggler.hpp>
#include <Geode/binding/EditorUI.hpp>
#include <Geode/ui/GeodeUI.hpp>
#include <Geode/ui/Notification.hpp>

using namespace geode::prelude;
using namespace ai_builder;

namespace {

// Remember the last description and mode between popups.
std::string s_lastPrompt;
bool s_lastReplace = false;
size_t s_exampleIndex = 0;

constexpr std::array EXAMPLES = {
    "a medieval castle with two towers, battlements and a gate",
    "pixel art of a red mushroom with white spots, 16x16",
    "a short fair cube section with spikes, a jump pad and an orb",
    "turn this into a glowing neon version with outline blocks",
    "add hanging chains and rods under this platform",
    "a ship corridor 30 blocks long with saws on the ceiling and floor",
    "a big rocket ship made of blocks and slopes",
    "mirror this structure and place the copy right next to it",
};

} // namespace

AIBuilderPopup* AIBuilderPopup::create(EditorUI* ui) {
    auto ret = new AIBuilderPopup();
    if (ret->init(ui)) {
        ret->autorelease();
        return ret;
    }
    delete ret;
    return nullptr;
}

bool AIBuilderPopup::init(EditorUI* ui) {
    if (!Popup::init(400.f, 250.f)) return false;
    m_editorUI = ui;
    m_selection = captureSelection(ui);

    this->setTitle("AI Builder");

    // What is selected
    std::string selText = m_selection.objects.empty()
        ? "Nothing selected - will build a new design near the camera"
        : fmt::format("Selected structure: {} object{} ({}x{} blocks)",
            m_selection.objects.size(), m_selection.objects.size() == 1 ? "" : "s",
            m_selection.widthCells + 1, m_selection.heightCells + 1);
    auto selLabel = CCLabelBMFont::create(selText.c_str(), "goldFont.fnt");
    selLabel->limitLabelWidth(360.f, .55f, .2f);
    m_mainLayer->addChildAtPosition(selLabel, Anchor::Top, ccp(0, -48));

    // Description input
    m_input = TextInput::create(360.f, "Describe your design...", "chatFont.fnt");
    m_input->setMaxCharCount(600);
    m_input->setCommonFilter(CommonFilter::Any);
    m_input->setLabel("Description");
    if (!s_lastPrompt.empty()) m_input->setString(s_lastPrompt);
    m_mainLayer->addChildAtPosition(m_input, Anchor::Center, ccp(0, 10));

    // Example prompts button
    auto exampleSpr = ButtonSprite::create("Example", "bigFont.fnt", "GJ_button_05.png", .6f);
    exampleSpr->setScale(.55f);
    auto exampleBtn = CCMenuItemSpriteExtra::create(exampleSpr, this, menu_selector(AIBuilderPopup::onExample));
    m_buttonMenu->addChildAtPosition(exampleBtn, Anchor::Center, ccp(145, -22));

    // Replace toggle (only meaningful with a selection)
    m_replaceToggle = CCMenuItemToggler::createWithStandardSprites(this, nullptr, .6f);
    m_replaceToggle->toggle(s_lastReplace);
    m_buttonMenu->addChildAtPosition(m_replaceToggle, Anchor::Center, ccp(-165, -22));
    m_replaceLabel = CCLabelBMFont::create("Replace selection (otherwise add to it)", "bigFont.fnt");
    m_replaceLabel->setScale(.33f);
    m_replaceLabel->setAnchorPoint(ccp(0, .5f));
    m_mainLayer->addChildAtPosition(m_replaceLabel, Anchor::Center, ccp(-150, -22));
    if (m_selection.objects.empty()) {
        m_replaceToggle->setVisible(false);
        m_replaceLabel->setVisible(false);
    }

    // Status line
    m_statusLabel = CCLabelBMFont::create("", "chatFont.fnt");
    m_statusLabel->setScale(.7f);
    m_mainLayer->addChildAtPosition(m_statusLabel, Anchor::Bottom, ccp(0, 62));

    m_spinner = LoadingSpinner::create(22.f);
    m_spinner->setVisible(false);
    m_mainLayer->addChildAtPosition(m_spinner, Anchor::Bottom, ccp(120, 30));

    // Buttons
    auto genSpr = ButtonSprite::create("Generate", "goldFont.fnt", "GJ_button_01.png", .8f);
    m_generateBtn = CCMenuItemSpriteExtra::create(genSpr, this, menu_selector(AIBuilderPopup::onGenerate));
    m_buttonMenu->addChildAtPosition(m_generateBtn, Anchor::Bottom, ccp(0, 30));

    auto settingsSpr = CCSprite::createWithSpriteFrameName("GJ_optionsBtn_001.png");
    settingsSpr->setScale(.55f);
    auto settingsBtn = CCMenuItemSpriteExtra::create(settingsSpr, this, menu_selector(AIBuilderPopup::onSettings));
    m_buttonMenu->addChildAtPosition(settingsBtn, Anchor::BottomRight, ccp(-25, 25));

    auto problem = checkSettings();
    if (!problem.empty()) {
        this->setStatus("Set your API key first (gear button)", {255, 200, 80});
    }
    return true;
}

void AIBuilderPopup::setStatus(std::string const& status, ccColor3B color) {
    m_statusLabel->setString(status.c_str());
    m_statusLabel->setColor(color);
    m_statusLabel->limitLabelWidth(370.f, .7f, .3f);
}

void AIBuilderPopup::setBusy(bool busy, std::string const& status) {
    m_busy = busy;
    m_spinner->setVisible(busy);
    m_generateBtn->setEnabled(!busy);
    m_generateBtn->setOpacity(busy ? 120 : 255);
    this->setStatus(status, busy ? ccColor3B{150, 220, 255} : ccColor3B{255, 255, 255});
}

void AIBuilderPopup::onExample(CCObject*) {
    if (m_busy) return;
    m_input->setString(EXAMPLES[s_exampleIndex % EXAMPLES.size()]);
    s_exampleIndex++;
}

void AIBuilderPopup::onSettings(CCObject*) {
    openSettingsPopup(Mod::get());
}

void AIBuilderPopup::onGenerate(CCObject*) {
    if (m_busy) return;

    std::string description = m_input->getString();
    if (description.find_first_not_of(" \t") == std::string::npos) {
        this->setStatus("Type a description first!", {255, 120, 120});
        return;
    }
    auto problem = checkSettings();
    if (!problem.empty()) {
        FLAlertLayer::create("AI Builder", problem, "OK")->show();
        return;
    }

    s_lastPrompt = description;
    s_lastReplace = m_replaceToggle->isToggled();

    auto mode = (m_replaceToggle->isToggled() && !m_selection.objects.empty())
        ? BuildMode::Replace : BuildMode::Add;

    this->setBusy(true, "AI is designing... (this can take up to a minute)");

    auto system = buildSystemPrompt();
    auto user = buildUserMessage(description, m_selection, mode);
    log::debug("AI Builder user message:\n{}", user);

    m_task.spawn(sendDesignRequest(system, user), [this](web::WebResponse res) {
        this->handleResponse(std::move(res));
    });
}

void AIBuilderPopup::handleResponse(web::WebResponse res) {
    auto text = extractResponseText(res);
    if (!text) {
        this->setBusy(false, "");
        this->setStatus("Failed - see details", {255, 120, 120});
        FLAlertLayer::create("AI Builder Error", text.unwrapErr(), "OK")->show();
        return;
    }
    log::debug("AI Builder raw response:\n{}", text.unwrap());

    auto design = parseDesign(text.unwrap(), m_selection.ids);
    if (!design) {
        this->setBusy(false, "");
        this->setStatus("Failed - see details", {255, 120, 120});
        FLAlertLayer::create("AI Builder Error", design.unwrapErr(), "OK")->show();
        return;
    }

    auto mode = (m_replaceToggle->isToggled() && !m_selection.objects.empty())
        ? BuildMode::Replace : BuildMode::Add;
    int placed = applyDesign(m_editorUI, m_selection, design.unwrap(), mode);

    if (placed == 0) {
        this->setBusy(false, "");
        this->setStatus("Nothing could be placed. Try again.", {255, 120, 120});
        return;
    }

    std::string msg = fmt::format("Built {} objects! (Ctrl+Z to undo)", placed);
    if (design.unwrap().droppedObjects > 0) {
        msg += fmt::format(" - skipped {} invalid", design.unwrap().droppedObjects);
    }
    Notification::create(msg, NotificationIcon::Success, 3.f)->show();
    if (!design.unwrap().notes.empty()) {
        log::info("AI Builder: {}", design.unwrap().notes);
    }

    // Close on the next frame, not from inside the request callback that this
    // popup's own TaskHolder is currently running.
    m_busy = false;
    Loader::get()->queueInMainThread([self = Ref(this)] {
        self->Popup::onClose(nullptr);
    });
}

void AIBuilderPopup::onClose(CCObject* sender) {
    if (m_busy) {
        // Closing cancels the in-flight request (TaskHolder cancels on destruction).
        m_task.cancel();
        m_busy = false;
    }
    Popup::onClose(sender);
}
