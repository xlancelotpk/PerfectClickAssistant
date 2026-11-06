#include "Predictor.hpp"
#include <algorithm>
#include <climits>
#include <cmath>

using namespace pca;

// ---- Constantes de física (unidades GD por frame @60Hz). Ajustables si ves desvíos. ----
static constexpr float kSpeed[5] = {251.16f, 311.58f, 387.42f, 468.0f, 576.0f}; // unidades/s
static constexpr float kGrav     = 0.958199f;
static constexpr float kJump     = 11.180032f;

static inline bool overlap(Rect const& a, Rect const& b) {
    return a.x0 < b.x1 && a.x1 > b.x0 && a.y0 < b.y1 && a.y1 > b.y0;
}
static inline Rect toLocal(Rect const& r, float d) {
    return d > 0 ? r : Rect{r.x0, -r.y1, r.x1, -r.y0};
}

void Predictor::setLevel(std::vector<Solid> solids, std::vector<Portal> portals) {
    m_solids = std::move(solids);
    m_portals = std::move(portals);
    std::sort(m_solids.begin(), m_solids.end(), [](Solid const& a, Solid const& b) { return a.r.x0 < b.r.x0; });
    std::sort(m_portals.begin(), m_portals.end(), [](Portal const& a, Portal const& b) { return a.r.x0 < b.r.x0; });
    m_maxW = m_maxPW = 0.f;
    for (auto& s : m_solids) m_maxW = std::max(m_maxW, s.r.x1 - s.r.x0);
    for (auto& p : m_portals) m_maxPW = std::max(m_maxPW, p.r.x1 - p.r.x0);
}

float Predictor::half(Sim const& s) const {
    float h = s.mode == Mode::Wave ? 7.f : 15.f;
    return s.mini ? h * 0.6f : h;
}

float Predictor::ceilFor(Mode m) const {
    return (m == Mode::Cube || m == Mode::Robot) ? 1e6f : floorY + ceilHeight;
}

float Predictor::rayUp(float x, float ly, float h, float ceil) const {
    float best = ceil;
    auto it = std::lower_bound(m_solids.begin(), m_solids.end(), x - h - m_maxW,
        [](Solid const& a, float v) { return a.r.x0 < v; });
    for (; it != m_solids.end() && it->r.x0 <= x + h; ++it) {
        if (it->hazard) continue;
        if (it->r.x1 < x - h) continue;
        best = std::min(best, it->r.y0 >= ly + h - 1.f ? it->r.y0 : best);
    }
    return best;
}

bool Predictor::step(Sim& s, bool hold, float dt) const {
    float const f = dt * 60.f;
    bool const edge = hold && !s.hold;
    bool const wasGround = s.onGround;
    s.onGround = false;

    float d = s.up ? -1.f : 1.f;
    float const h = half(s);
    float ly = d * s.y;
    float prevLy = ly;
    float fl, ce;
    auto bounds = [&] {
        float c = ceilFor(s.mode);
        fl = d > 0 ? floorY : -c;
        ce = d > 0 ? c : -floorY;
    };
    bounds();

    float const wavePerFrame = kSpeed[s.speedIdx] / 60.f;
    switch (s.mode) {
        case Mode::Cube:
            if (hold && wasGround) s.vy = kJump;
            s.vy = std::max(s.vy - kGrav * f, -15.f);
            break;
        case Mode::Ship:
            s.vy += (hold ? 0.8f : -0.8f) * kGrav * f;
            s.vy = std::clamp(s.vy, -8.f, 8.f);
            break;
        case Mode::Ball:
            if (edge && wasGround) {
                s.up = !s.up; d = -d; ly = -ly; prevLy = ly; s.vy = 0.f; bounds();
            }
            s.vy = std::max(s.vy - 0.6f * kGrav * f, -15.f);
            break;
        case Mode::Ufo:
            if (edge) s.vy = 7.f;
            s.vy = std::max(s.vy - 0.5f * kGrav * f, -8.f);
            break;
        case Mode::Wave:
            s.vy = hold ? wavePerFrame : -wavePerFrame;
            break;
        case Mode::Robot:
            if (edge && wasGround) { s.vy = 10.f; s.boost = 9; }
            else if (hold && s.boost > 0) { s.vy = std::max(s.vy, 10.f); --s.boost; }
            else if (!hold) s.boost = 0;
            s.vy = std::max(s.vy - kGrav * f, -15.f);
            break;
        case Mode::Spider:
            if (edge && wasGround) {
                float target = rayUp(s.x, ly, h, ce);
                ly = target - h;
                s.up = !s.up; d = -d; ly = -ly; prevLy = ly; s.vy = 0.f; bounds();
                s.onGround = true;
            } else {
                s.vy = std::max(s.vy - kGrav * f, -15.f);
            }
            break;
    }

    ly += s.vy * f;
    s.x += kSpeed[s.speedIdx] * dt;

    // ---- Colisiones ----
    bool dead = false;
    auto it = std::lower_bound(m_solids.begin(), m_solids.end(), s.x - h - m_maxW,
        [](Solid const& a, float v) { return a.r.x0 < v; });
    for (; it != m_solids.end() && it->r.x0 <= s.x + h && !dead; ++it) {
        Rect r = toLocal(it->r, d);
        Rect p{s.x - h, ly - h, s.x + h, ly + h};
        if (!overlap(p, r)) continue;
        if (it->hazard || s.mode == Mode::Wave) { dead = true; break; }
        constexpr float tol = 10.f;
        if (prevLy - h >= r.y1 - tol && s.vy <= 0.f) {
            ly = r.y1 + h; s.vy = 0.f; s.onGround = true;
        } else if (prevLy + h <= r.y0 + tol && s.vy >= 0.f) {
            ly = r.y0 - h; s.vy = std::min(s.vy, 0.f);
        } else {
            dead = true;
        }
    }
    if (dead) return false;

    if (ly - h < fl) { ly = fl + h; if (s.vy < 0.f) s.vy = 0.f; s.onGround = true; }
    if (ly + h > ce) { ly = ce - h; if (s.vy > 0.f) s.vy = 0.f; }

    s.y = d * ly;
    s.hold = hold;

    // ---- Portales (valores explícitos => idempotentes) ----
    Rect wp{s.x - h, s.y - h, s.x + h, s.y + h};
    auto pit = std::lower_bound(m_portals.begin(), m_portals.end(), s.x - h - m_maxPW,
        [](Portal const& a, float v) { return a.r.x0 < v; });
    for (; pit != m_portals.end() && pit->r.x0 <= s.x + h; ++pit) {
        if (!overlap(wp, pit->r)) continue;
        switch (pit->kind) {
            case PortalKind::Mode:    s.mode = static_cast<Mode>(pit->value); break;
            case PortalKind::Gravity: s.up = pit->value == 1; break;
            case PortalKind::Speed:   s.speedIdx = pit->value; break;
            case PortalKind::Size:    s.mini = pit->value == 1; break;
        }
    }
    return true;
}

Plan Predictor::plan(Sim const& start, float horizon) const {
    constexpr float dt = 1.f / 120.f;
    int const N = static_cast<int>(horizon / dt);
    constexpr int NONE = INT_MAX;

    struct Best { int cost = INT_MAX, k1 = NONE, k2 = NONE, tie = -1; bool s0 = false; float t = -1.f; bool survive = false; } best;

    auto value = [](bool s0, int k1, int k2, int i) {
        bool v = s0;
        if (i >= k1) v = !v;
        if (i >= k2) v = !v;
        return v;
    };

    auto eval = [&](bool s0, int k1, int k2) {
        Sim s = start;
        int i = 0;
        for (; i < N; ++i)
            if (!step(s, value(s0, k1, k2, i), dt)) break;
        bool survive = (i == N);
        float t = i * dt;
        int cost = (s0 != start.hold ? 1 : 0) + (k1 != NONE) + (k2 != NONE);
        int tie = k1 == NONE ? INT_MAX : k1;   // preferimos acciones más tardías (más margen de reacción)
        bool better = false;
        if (survive && !best.survive) better = true;
        else if (survive && best.survive) better = cost < best.cost || (cost == best.cost && tie > best.tie);
        else if (!survive && !best.survive) better = t > best.t || (t == best.t && cost < best.cost);
        if (better) best = {cost, k1, k2, tie, s0, t, survive};
    };

    for (int s0i = 0; s0i < 2; ++s0i) {
        bool s0 = s0i == 0 ? start.hold : !start.hold;
        eval(s0, NONE, NONE);
        for (int k1 = 0; k1 <= 40; k1 += 2) {
            eval(s0, k1, NONE);
            for (int k2 = k1 + 2; k2 <= N; k2 += 2) eval(s0, k1, k2);
        }
    }

    Plan p;
    p.valid = true;
    p.survives = best.survive;
    p.surviveTime = best.t;
    p.horizon = horizon;
    p.holdNow = value(best.s0, best.k1, best.k2, 0);

    float const spd = kSpeed[start.speedIdx];
    bool cur = start.hold;
    for (int i = 0; i < N; ++i) {
        bool v = value(best.s0, best.k1, best.k2, i);
        if (v != cur) { p.taps.push_back({i * dt, start.x + spd * i * dt, v}); cur = v; }
    }
    return p;
}
