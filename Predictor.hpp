#pragma once
#include <vector>

namespace pca {

enum class Mode : int { Cube, Ship, Ball, Ufo, Wave, Robot, Spider };

struct Rect { float x0, y0, x1, y1; };
struct Solid { Rect r; bool hazard; };

enum class PortalKind { Mode, Gravity, Speed, Size };
struct Portal { Rect r; PortalKind kind; int value; };

// Estado simulado del jugador. vy está en "local up" (positivo = hacia arriba según la gravedad).
struct Sim {
    float x = 0, y = 0, vy = 0;
    bool up = false;       // gravedad invertida
    Mode mode = Mode::Cube;
    int speedIdx = 1;      // 0..4 = slow, normal, fast, faster, fastest
    bool mini = false;
    bool hold = false;
    bool onGround = false;
    int boost = 0;         // robot
};

struct Tap { float t; float x; bool press; };

struct Plan {
    bool valid = false;
    bool survives = false;
    float surviveTime = 0.f;
    float horizon = 0.55f;
    bool holdNow = false;
    std::vector<Tap> taps;
};

class Predictor {
public:
    float floorY = 90.f;
    float ceilHeight = 300.f;   // altura del techo en modos con techo

    void setLevel(std::vector<Solid> solids, std::vector<Portal> portals);
    bool step(Sim& s, bool hold, float dt) const;      // false = muere
    Plan plan(Sim const& start, float horizon = 0.55f) const;

private:
    std::vector<Solid> m_solids;     // ordenados por x0
    std::vector<Portal> m_portals;   // ordenados por x0
    float m_maxW = 0.f, m_maxPW = 0.f;

    float half(Sim const& s) const;
    float ceilFor(Mode m) const;
    float rayUp(float x, float ly, float h, float ceil) const;
};

} // namespace pca
