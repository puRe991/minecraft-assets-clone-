// vg/entity/Pathfinder.cpp — see Pathfinder.hpp for the contract.
#include "vg/entity/Pathfinder.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <queue>
#include <unordered_map>

namespace vg::entity {

bool Pathfinder::standable(const Vec3i& p) const {
    // feet + head passable, solid ground directly below
    return passable(p) && passable({p.x, p.y + 1, p.z}) && solid({p.x, p.y - 1, p.z});
}

namespace {
int manhattan(const Vec3i& a, const Vec3i& b) {
    return std::abs(a.x - b.x) + std::abs(a.y - b.y) + std::abs(a.z - b.z);
}
struct Open { int f; Vec3i pos; };
struct OpenCmp { bool operator()(const Open& a, const Open& b) const { return a.f > b.f; } };
}  // namespace

std::vector<Vec3i> Pathfinder::findPath(const Vec3i& start, const Vec3i& goal) const {
    if (!standable(start) || !standable(goal)) return {};
    if (start == goal) return {start};

    std::priority_queue<Open, std::vector<Open>, OpenCmp> open;
    std::unordered_map<Vec3i, Vec3i> came;
    std::unordered_map<Vec3i, int> gScore;

    gScore[start] = 0;
    open.push({manhattan(start, goal), start});

    const int dirs[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
    int expanded = 0;

    while (!open.empty()) {
        Vec3i cur = open.top().pos; open.pop();
        if (cur == goal) {
            std::vector<Vec3i> path{goal};
            while (path.back() != start) path.push_back(came[path.back()]);
            std::reverse(path.begin(), path.end());
            return path;
        }
        if (++expanded > cfg_.maxNodes) break;
        int gCur = gScore[cur];

        // Build the set of reachable neighbours (walk / step up / drop down).
        std::vector<std::pair<Vec3i, int>> neigh;  // (cell, step cost*10)
        for (auto& d : dirs) {
            Vec3i fwd{cur.x + d[0], cur.y, cur.z + d[1]};
            if (standable(fwd)) {
                neigh.push_back({fwd, 10});
            } else if (solid(fwd)) {
                // step up one block if there's headroom and a stand above
                Vec3i up{fwd.x, fwd.y + 1, fwd.z};
                if (passable({cur.x, cur.y + 2, cur.z}) && standable(up))
                    neigh.push_back({up, 15});
            } else {
                // feet cell open but no floor: look for a landing below
                for (int k = 1; k <= cfg_.maxDrop; ++k) {
                    Vec3i dn{fwd.x, fwd.y - k, fwd.z};
                    if (standable(dn)) { neigh.push_back({dn, 10 + 2 * k}); break; }
                    if (solid(dn)) break;   // hit ground that isn't standable
                }
            }
        }

        for (auto& [np, cost] : neigh) {
            int tentative = gCur + cost;
            auto it = gScore.find(np);
            if (it == gScore.end() || tentative < it->second) {
                gScore[np] = tentative;
                came[np] = cur;
                open.push({tentative + manhattan(np, goal) * 10, np});
            }
        }
    }
    return {};   // no path
}

}  // namespace vg::entity
