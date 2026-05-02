#include <EditorP2P/ui/ActivityLogLayer.hpp>
#include <EditorP2P/core/ActivityLog.hpp>
#include <Geode/binding/ButtonSprite.hpp>
#include <Geode/binding/CCMenuItemSpriteExtra.hpp>
#include <Geode/Geode.hpp>
#include <Geode/utils/general.hpp>

#include <sstream>

using namespace geode::prelude;

namespace ep2p {

    ActivityLogLayer* ActivityLogLayer::create() {
        auto* obj = new ActivityLogLayer();
        if (obj && obj->init(400.f, 280.f) && obj->setup()) {
            obj->autorelease();
            return obj;
        }
        CC_SAFE_DELETE(obj);
        return nullptr;
    }

    bool ActivityLogLayer::setup() {
        this->setTitle("Activity Log");

        auto size = this->m_mainLayer->getContentSize();
        m_list = CCNode::create();
        m_list->setPosition({20.f, size.height - 48.f});
        this->m_mainLayer->addChild(m_list);

        auto* menu = CCMenu::create();
        menu->setPosition({size.width / 2.f, 24.f});
        this->m_mainLayer->addChild(menu);

        auto* copyBtn = CCMenuItemSpriteExtra::create(
            ButtonSprite::create("Copy", 70, 0, 0.44f, true),
            this,
            menu_selector(ActivityLogLayer::onCopy)
        );
        copyBtn->setPosition({-42.f, 0.f});
        menu->addChild(copyBtn);

        auto* clearBtn = CCMenuItemSpriteExtra::create(
            ButtonSprite::create("Clear", 70, 0, 0.44f, true),
            this,
            menu_selector(ActivityLogLayer::onClear)
        );
        clearBtn->setPosition({42.f, 0.f});
        menu->addChild(clearBtn);

        refresh();
        return true;
    }

    void ActivityLogLayer::refresh() {
        if (!m_list) return;
        m_list->removeAllChildren();

        const auto& events = ActivityLog::get().events();
        if (events.empty()) {
            auto* label = CCLabelBMFont::create("No activity yet.", "bigFont.fnt");
            label->setScale(0.34f);
            label->setAnchorPoint({0.f, 0.5f});
            label->setPosition({0.f, -20.f});
            m_list->addChild(label);
            return;
        }

        constexpr size_t maxRows = 9;
        size_t start = events.size() > maxRows ? events.size() - maxRows : 0;
        float y = 0.f;
        for (size_t i = start; i < events.size(); ++i) {
            const auto& event = events[i];
            auto seconds = static_cast<unsigned long long>(event.timestamp / 1000);
            auto text = std::to_string(seconds) + "s P" +
                        std::to_string(event.actorId) + " " + event.detail;

            auto* label = CCLabelBMFont::create(text.c_str(), "bigFont.fnt");
            label->setScale(0.24f);
            label->setWidth(350.f);
            label->setAnchorPoint({0.f, 0.5f});
            label->setPosition({0.f, y});
            m_list->addChild(label);
            y -= 22.f;
        }
    }

    void ActivityLogLayer::onClear(CCObject*) {
        ActivityLog::get().clear();
        refresh();
    }

    void ActivityLogLayer::onCopy(CCObject*) {
        geode::utils::clipboard::write(buildLogText());
    }

    std::string ActivityLogLayer::buildLogText() const {
        std::ostringstream out;
        for (const auto& event : ActivityLog::get().events()) {
            out << event.timestamp
                << "|P" << event.actorId
                << "|obj=" << event.objectId
                << "|" << event.detail
                << "\n";
        }
        return out.str();
    }

} // namespace ep2p
