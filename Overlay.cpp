#include "Overlay.hpp"
#include <cmath>

using namespace geode::prelude;

static ccColor4F col(ccColor3B c, float a) { return {c.r / 255.f, c.g / 255.f, c.b / 255.f, a}; }

static void ring(CCDrawNode* n, CCPoint c, float r, ccColor4F color) {
    constexpr int N = 24;
    CCPoint v[N];
    for (int i = 0; i < N; ++i) {
        float a = 6.2831853f * i / N;
        v[i] = ccp(c.x + std::cos(a) * r, c.y + std::sin(a) * r);
    }
    n->drawPolygon(v, N, ccColor4F{0, 0, 0, 0}, 1.5f, color);
}

TapOverlay* TapOverlay::create() {
    auto ret = new TapOverlay();
    if (ret && ret->init()) { ret->autorelease(); return ret; }
    CC_SAFE_DELETE(ret);
    return nullptr;
}

bool TapOverlay::init() {
    if (!CCNode::init()) return false;
    m_draw = CCDrawNode::create();
    this->addChild(m_draw);
    m_label = CCLabelBMFont::create("Taps: 0", "bigFont.fnt");
    m_label->setScale(0.4f);
    this->addChild(m_label);
    this->scheduleUpdate();
    return true;
}

void TapOverlay::addTap(bool press) {
    m_pings.push_back({0.f, press});
    if (m_pings.size() > 64) m_pings.erase(m_pings.begin());
}

void TapOverlay::update(float dt) {
    auto mod = Mod::get();
    bool enabled = mod->getSettingValue<bool>("enabled");
    this->setVisible(enabled);
    if (!enabled) return;

    float const op = static_cast<float>(mod->getSettingValue<double>("overlay-opacity"));
    auto cPress = mod->getSettingValue<ccColor3B>("color-press");
    auto cRel   = mod->getSettingValue<ccColor3B>("color-release");
    auto cPred  = mod->getSettingValue<ccColor3B>("color-predicted");
    bool showTaps = mod->getSettingValue<bool>("show-taps");
    bool showPred = mod->getSettingValue<bool>("show-predicted");
    bool anim     = mod->getSettingValue<bool>("tap-animation");
    bool counter  = mod->getSettingValue<bool>("click-counter");
    auto posName  = mod->getSettingValue<std::string>("overlay-position");

    auto win = CCDirector::get()->getWinSize();
    constexpr float W = 280.f;
    CCPoint p;
    if (posName == "Top Center")         p = {win.width / 2 - W / 2, win.height - 40};
    else if (posName == "Bottom Center") p = {win.width / 2 - W / 2, 30};
    else if (posName == "Top Left")      p = {20, win.height - 40};
    else if (posName == "Top Right")     p = {win.width - W - 20, win.height - 40};
    else if (posName == "Bottom Left")   p = {20, 30};
    else                                 p = {win.width - W - 20, 30};

    m_draw->clear();

    // Línea de tiempo + marcador "ahora"
    m_draw->drawSegment(p, ccp(p.x + W, p.y), 2.f, ccColor4F{1, 1, 1, 0.25f * op});
    m_draw->drawSegment(ccp(p.x, p.y - 12), ccp(p.x, p.y + 12), 1.5f, ccColor4F{1, 1, 1, op});

    // Taps pasados (derivan a la izquierda y se desvanecen)
    for (auto& ping : m_pings) ping.age += dt;
    std::erase_if(m_pings, [](Ping const& g) { return g.age > 1.6f; });
    if (showTaps) {
        for (auto const& g : m_pings) {
            float t = g.age / 1.6f;
            auto c = g.press ? cPress : cRel;
            CCPoint c0 = ccp(p.x - t * 70.f, p.y);
            m_draw->drawDot(c0, g.press ? 7.f : 4.f, col(c, (1.f - t) * op));
            if (anim && g.age < 0.35f) {
                float k = g.age / 0.35f;
                ring(m_draw, ccp(p.x, p.y), 7.f + k * 18.f, col(c, (1.f - k) * op));
            }
        }
    }

    // Taps previstos
    if (showPred && m_plan.valid) {
        float pressT = -1.f;
        for (auto const& t : m_plan.taps) {
            float x = p.x + t.t / m_plan.horizon * W;
            if (t.press) {
                pressT = x;
                m_draw->drawDot(ccp(x, p.y), 6.f, col(cPred, op));
            } else {
                ring(m_draw, ccp(x, p.y), 5.f, col(cPred, op));
                if (pressT >= 0.f) m_draw->drawSegment(ccp(pressT, p.y), ccp(x, p.y), 3.f, col(cPred, 0.35f * op));
                pressT = -1.f;
            }
        }
        if (pressT >= 0.f) m_draw->drawSegment(ccp(pressT, p.y), ccp(p.x + W, p.y), 3.f, col(cPred, 0.35f * op));
        if (!m_plan.survives)
            m_draw->drawDot(ccp(p.x + m_plan.surviveTime / m_plan.horizon * W, p.y + 14.f), 4.f, ccColor4F{1, 0.2f, 0.2f, op});
    }

    m_label->setVisible(counter);
    if (counter) {
        m_label->setString(fmt::format("Taps: {}  (total {})", m_attempt, m_total).c_str());
        m_label->setPosition(ccp(p.x + W / 2, p.y + 24.f));
        m_label->setOpacity(static_cast<GLubyte>(255.f * op));
    }
}
