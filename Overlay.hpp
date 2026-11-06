#pragma once
#include <Geode/Geode.hpp>
#include "Predictor.hpp"

class TapOverlay : public cocos2d::CCNode {
public:
    static TapOverlay* create();
    bool init() override;
    void update(float dt) override;

    void addTap(bool press);
    void setPlan(pca::Plan const& p) { m_plan = p; }
    void setCounts(int attempt, int total) { m_attempt = attempt; m_total = total; }

private:
    struct Ping { float age; bool press; };

    cocos2d::CCDrawNode* m_draw = nullptr;
    cocos2d::CCLabelBMFont* m_label = nullptr;
    std::vector<Ping> m_pings;
    pca::Plan m_plan;
    int m_attempt = 0, m_total = 0;
};
