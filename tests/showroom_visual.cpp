// Headless checks that a vehicle's drawing template can be freed (the showroom rebuilds from an owned spec on every pick and
// deletes the old one, so a reused spec address must not find the old template) and that a template still being made refuses.
#include <chrono>
#include <cstdio>
#include <new>
#include <thread>

#include "../src/carvisual.h"
#include "../src/lights.h"
#include "harness.h"

namespace {
int failures = 0;
void check(const char* name, bool ok, const char* detail = "") {
    std::printf("%s %s %s\n", ok ? "PASS" : "FAIL", name, detail);
    failures += !ok;
}
double nowMs() {
    using namespace std::chrono;
    return duration<double, std::milli>(steady_clock::now().time_since_epoch()).count();
}
const V3 kColor(0.4, 0.4, 0.4);
}  // namespace

int main() {
    blockOnTemplates = true;
    const auto& cat = vehicleCatalog();

    // evict frees: the forget callback sees the template once, and the slot is gone.
    {
        auto* a = new VehicleSpec(cat[0]);
        Car car;
        car.build(*a, V3(), 0, kColor);
        const VisualTemplate* t = &visualTemplate(car);
        int calls = 0;
        const VisualTemplate* seen = nullptr;
        auto f = [&](const VisualTemplate& v) { calls++, seen = &v; };
        check("evict frees: evictVisual is true", evictVisual(*a, f));
        check("evict frees: forget called once with the template", calls == 1 && seen == t);
        check("evict frees: visualReady is false afterwards", !visualReady(*a));
        check("evict of a spec never seen is true", evictVisual(*a, f) && calls == 1);
        forgetLampLayout(a);
        forgetLampLayout(nullptr);  // (nothing cached: a no-op)
        delete a;
    }

    // A spec rebuilt at the same address gets its own template, not the freed one's.
    {
        void* mem = ::operator new(sizeof(VehicleSpec));
        auto* a = ::new (mem) VehicleSpec(cat[0]);
        size_t verts0 = 0;
        {
            Car car;
            car.build(*a, V3(), 0, kColor);
            verts0 = visualTemplate(car).rvStatic.size();
        }
        check("address reuse: first evict is true", evictVisual(*a, [](const VisualTemplate&) {}));
        a->~VehicleSpec();
        a = ::new (mem) VehicleSpec(cat[std::min<size_t>(2, cat.size() - 1)]);
        Car car;
        car.build(*a, V3(), 0, kColor);
        const VisualTemplate& t1 = visualTemplate(car);
        // (not compared by address: the allocator may well hand the freed template's memory to the new one)
        check("no stale template on address reuse: its vertex count differs", t1.rvStatic.size() != verts0);
        evictVisual(*a, [](const VisualTemplate&) {});
        a->~VehicleSpec();
        ::operator delete(mem);
    }

    // A slot whose build is queued but not done refuses, and leaves the cache as it was.
    {
        blockOnTemplates = false;
        auto* b = new VehicleSpec(cat[std::min<size_t>(1, cat.size() - 1)]);
        prebuildVisual(*b, kColor);
        int calls = 0;
        auto f = [&](const VisualTemplate&) { calls++; };
        const bool readyBefore = visualReady(*b);
        const bool evicted = evictVisual(*b, f);
        check("pending refuses (or the build won the race)", !evicted || readyBefore);
        check("a refused evict forgets nothing", evicted || calls == 0);
        if (!evicted) {
            const double w0 = nowMs();
            while (!visualReady(*b) && nowMs() - w0 < 60000) std::this_thread::sleep_for(std::chrono::milliseconds(1));
            check("... the template becomes ready", visualReady(*b));
            check("... and then evicts", evictVisual(*b, f) && calls == 1);
        }
        delete b;
    }

    std::printf("%s\n", failures ? "FAILED" : "all passed");
    return failures ? 1 : 0;
}
