#include <Geode/Geode.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <Geode/modify/GJBaseGameLayer.hpp>
#include <cmath>
#include "Predictor.hpp"
#include "Overlay.hpp"

using namespace geode::prelude;

namespace {

struct State {
    pca::Predictor predictor;
    PlayLayer* owner = nullptr;
    TapOverlay* overlay = nullptr;
    pca::Plan plan;
    int tick = 0;
    int tapsAttempt = 0;
    int tapsTotal = 0;
    bool holding = false;     // estado real del botón (manual o bot)
    bool botHolding = false;
    bool botUsed = false;
    bool injecting = false;
};
State st;

bool cfg(char const* k) { return Mod::get()->getSettingValue<bool>(k); }

int speedIndex(float v) {
    static constexpr float t[5] = {0.7f, 0.9f, 1.1f, 1.3f, 1.6f};
    int best = 1; float bd = 1e9f;
    for (int i = 0; i < 5; ++i) if (std::abs(t[i] - v) < bd) { bd = std::abs(t[i] - v); best = i; }
    return best;
}

bool portalInfo(int id, pca::PortalKind& k, int& v) {
    using P = pca::PortalKind; using M = pca::Mode;
    switch (id) {
        case 12:   k = P::Mode; v = (int)M::Cube;   return true;
        case 13:   k = P::Mode; v = (int)M::Ship;   return true;
        case 47:   k = P::Mode; v = (int)M::Ball;   return true;
        case 111:  k = P::Mode; v = (int)M::Ufo;    return true;
        case 660:  k = P::Mode; v = (int)M::Wave;   return true;
        case 745:  k = P::Mode; v = (int)M::Robot;  return true;
        case 1331: k = P::Mode; v = (int)M::Spider; return true;
        case 10:   k = P::Gravity; v = 0; return true;
        case 11:   k = P::Gravity; v = 1; return true;
        case 99:   k = P::Size; v = 0; return true;
        case 101:  k = P::Size; v = 1; return true;
        case 200:  k = P::Speed; v = 0; return true;
        case 201:  k = P::Speed; v = 1; return true;
        case 202:  k = P::Speed; v = 2; return true;
        case 203:  k = P::Speed; v = 3; return true;
        case 1334: k = P::Speed; v = 4; return true;
        default: return false;
    }
}

void buildForLevel(PlayLayer* pl) {
    st = State{};
    st.owner = pl;

    std::vector<pca::Solid> solids;
    std::vector<pca::Portal> portals;
    for (auto obj : CCArrayExt<GameObject*>(pl->m_objects)) {
        if (!obj) continue;
        auto r = obj->getObjectRect();
        pca::Rect pr{r.origin.x, r.origin.y, r.origin.x + r.size.width, r.origin.y + r.size.height};
        pca::PortalKind k; int v;
        if (portalInfo(obj->m_objectID, k, v)) { portals.push_back({pr, k, v}); continue; }
        if (obj->m_objectType == GameObjectType::Hazard) solids.push_back({pr, true});
        else if (obj->m_objectType == GameObjectType::Solid) solids.push_back({pr, false});
    }
    st.predictor.setLevel(std::move(solids), std::move(portals));

    st.overlay = TapOverlay::create();
    if (st.overlay && pl->m_uiLayer) pl->m_uiLayer->addChild(st.overlay, 100);
}

pca::Sim captureSim(PlayerObject* p) {
    pca::Sim s;
    s.x = p->getPositionX();
    s.y = p->getPositionY();
    s.vy = static_cast<float>(p->m_yVelocity);
    s.up = p->m_isUpsideDown;
    s.mini = p->m_vehicleSize < 1.f;
    s.speedIdx = speedIndex(p->m_playerSpeed);
    s.onGround = p->m_isOnGround;
    s.hold = st.holding;
    using M = pca::Mode;
    s.mode = p->m_isShip ? M::Ship : p->m_isBird ? M::Ufo : p->m_isBall ? M::Ball :
             p->m_isDart ? M::Wave : p->m_isRobot ? M::Robot : p->m_isSpider ? M::Spider : M::Cube;
    return s;
}

void tick(PlayLayer* pl, GJBaseGameLayer* base) {
    if (!st.owner || st.owner != pl || !pl->m_player1 || pl->m_player1->m_isDead) return;
    if (!cfg("smart-detection")) return;

    // Replanificar ~60 veces/s (el juego corre a 240 ticks/s)
    if (st.tick++ % 4 == 0) {
        st.plan = st.predictor.plan(captureSim(pl->m_player1));
        if (st.overlay) st.overlay->setPlan(st.plan);
    }

    if (cfg("auto-click") && st.plan.valid) {
        bool want = st.plan.holdNow;
        if (want != st.botHolding) {
            st.injecting = true;
            if (want) base->pushButton(PlayerButton::Jump, false);
            else      base->releaseButton(PlayerButton::Jump, false);
            st.injecting = false;
            st.botHolding = want;
            st.botUsed = true;
        }
    }
}

} // namespace

class $modify(PCABase, GJBaseGameLayer) {
    void processCommands(float dt) {
        auto pl = PlayLayer::get();
        if (pl && static_cast<GJBaseGameLayer*>(pl) == this && cfg("enabled")) tick(pl, this);
        GJBaseGameLayer::processCommands(dt);
    }

    void pushButton(PlayerButton b, bool second) {
        GJBaseGameLayer::pushButton(b, second);
        if (b == PlayerButton::Jump && !second && st.owner && cfg("enabled")) {
            st.holding = true;
            ++st.tapsAttempt; ++st.tapsTotal;
            if (st.overlay) { st.overlay->addTap(true); st.overlay->setCounts(st.tapsAttempt, st.tapsTotal); }
        }
    }

    void releaseButton(PlayerButton b, bool second) {
        GJBaseGameLayer::releaseButton(b, second);
        if (b == PlayerButton::Jump && !second && st.owner && cfg("enabled")) {
            st.holding = false;
            if (st.overlay) st.overlay->addTap(false);
        }
    }
};

class $modify(PCAPlayLayer, PlayLayer) {
    void setupHasCompleted() {
        PlayLayer::setupHasCompleted();
        buildForLevel(this);
    }

    void resetLevel() {
        PlayLayer::resetLevel();
        st.holding = st.botHolding = false;
        st.plan = {};
        st.tick = 0;
        st.tapsAttempt = 0;
        if (st.overlay) st.overlay->setCounts(0, st.tapsTotal);
    }

    void levelComplete() {
        // Con el bot activo la partida se marca como test: no se guarda progreso ni récords.
        if (st.botUsed && cfg("safe-mode")) m_isTestMode = true;
        PlayLayer::levelComplete();
    }

    void onQuit() {
        st.owner = nullptr;
        st.overlay = nullptr;
        PlayLayer::onQuit();
    }
};
