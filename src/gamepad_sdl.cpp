// The game controller device: SDL2's GameController API (see sdl_min.h for the declarations).
// Hot-plugging comes through SDL's CONTROLLERDEVICEADDED / REMOVED events; controllers already
// plugged in when the subsystem starts arrive as ADDED events too.
#include <cctype>
#include <algorithm>
#include <cmath>

#include "gamepad.h"
#include "sdl_min.h"

namespace pad {

namespace {
struct Pad {
    SDL_GameController* gc = nullptr;
    int32_t id = -1;  // joystick instance id (what the events carry)
    std::string name;
    uint32_t latched = 0;  // presses since the last read (a tap shorter than a frame still counts)
};
}  // namespace

struct Device::Impl {
    std::vector<Pad> pads;
    int active = 0;  // the one last used
    int find(int32_t id) const {
        for (int i = 0; i < (int)pads.size(); i++)
            if (pads[i].id == id) return i;
        return -1;
    }
};

bool Device::start() {
    if (started_) return true;
    if (SDL_InitSubSystem(SDL_INIT_GAMECONTROLLER) != 0) return false;
    impl_ = new Impl();
    started_ = true;
    return true;
}

int Device::count() const { return impl_ ? (int)impl_->pads.size() : 0; }

bool Device::handleEvent(const SDL_Event& e, std::string& toast) {
    if (!impl_) return e.type >= SDL_CONTROLLERAXISMOTION && e.type <= SDL_CONTROLLERDEVICEREMAPPED;
    Impl& d = *impl_;
    switch (e.type) {
        case SDL_CONTROLLERDEVICEADDED: {
            SDL_GameController* gc = SDL_GameControllerOpen(evCtrlWhich(e));
            if (!gc) return true;
            const int32_t id = SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(gc));
            if (d.find(id) >= 0) {  // (already open)
                SDL_GameControllerClose(gc);
                return true;
            }
            Pad p;
            p.gc = gc;
            p.id = id;
            const char* nm = SDL_GameControllerName(gc);
            p.name = nm ? nm : "CONTROLLER";
            d.pads.push_back(p);
            d.active = (int)d.pads.size() - 1;
            name_ = p.name;
            ui.style = styleFromName(name_);
            std::string up;
            for (char ch : name_) up += (char)std::toupper((unsigned char)ch);
            toast = "CONTROLLER CONNECTED: " + up;
            return true;
        }
        case SDL_CONTROLLERDEVICEREMOVED: {
            const int i = d.find(evCtrlWhich(e));
            if (i < 0) return true;
            SDL_GameControllerClose(d.pads[i].gc);
            d.pads.erase(d.pads.begin() + i);
            d.active = std::min(std::max(d.active - (d.active > i ? 1 : 0), 0), std::max(0, (int)d.pads.size() - 1));
            toast = "CONTROLLER DISCONNECTED";
            if (d.pads.empty()) name_.clear(), ui.last = false, rumbling_ = false;
            else name_ = d.pads[d.active].name, ui.style = styleFromName(name_);
            return true;
        }
        case SDL_CONTROLLERBUTTONDOWN:
        case SDL_CONTROLLERBUTTONUP: {
            const int i = d.find(evCtrlWhich(e));
            if (i >= 0 && e.type == SDL_CONTROLLERBUTTONDOWN && evCtrlButton(e) < B_COUNT) {
                d.pads[i].latched |= 1u << evCtrlButton(e);
                d.active = i;
                ui.style = styleFromName(d.pads[i].name);
            }
            return true;
        }
        case SDL_CONTROLLERAXISMOTION: {
            const int i = d.find(evCtrlWhich(e));
            if (i >= 0 && std::abs(evCtrlAxisValue(e)) > 12000 && i != d.active) {
                d.active = i;
                ui.style = styleFromName(d.pads[i].name);
            }
            return true;
        }
        case SDL_CONTROLLERDEVICEREMAPPED: return true;
    }
    return false;
}

Raw Device::read(bool focused) {
    Raw r;
    if (!impl_ || impl_->pads.empty()) return r;
    Pad& p = impl_->pads[std::min(std::max(impl_->active, 0), (int)impl_->pads.size() - 1)];
    const uint32_t latched = p.latched;
    for (Pad& q : impl_->pads) q.latched = 0;
    if (!focused) return r;
    auto ax = [&](int a) { return std::max(-1.0f, SDL_GameControllerGetAxis(p.gc, a) / 32767.0f); };
    r.lx = ax(SDL_CONTROLLER_AXIS_LEFTX), r.ly = ax(SDL_CONTROLLER_AXIS_LEFTY);
    r.rx = ax(SDL_CONTROLLER_AXIS_RIGHTX), r.ry = ax(SDL_CONTROLLER_AXIS_RIGHTY);
    r.lt = std::max(0.0f, ax(SDL_CONTROLLER_AXIS_TRIGGERLEFT)), r.rt = std::max(0.0f, ax(SDL_CONTROLLER_AXIS_TRIGGERRIGHT));
    for (int b = 0; b < B_COUNT; b++)
        if (SDL_GameControllerGetButton(p.gc, b)) r.buttons |= 1u << b;
    r.buttons |= latched;
    return r;
}

void Device::rumble(float low, float high) {
    if (!impl_ || impl_->pads.empty()) return;
    const bool on = low > 0.01f || high > 0.01f;
    if (!on && !rumbling_) return;  // (nothing to stop: never touch a pad we haven't rumbled)
    const Pad& p = impl_->pads[std::min(std::max(impl_->active, 0), (int)impl_->pads.size() - 1)];
    auto u16 = [](float v) { return (uint16_t)(std::min(1.0f, std::max(0.0f, v)) * 65535.0f); };
    SDL_GameControllerRumble(p.gc, u16(low), u16(high), on ? 160 : 0);  // (unsupported pads just say no)
    rumbling_ = on;
}

void Device::close() {
    if (!impl_) return;
    if (rumbling_) rumble(0, 0);
    for (Pad& p : impl_->pads) SDL_GameControllerClose(p.gc);
    delete impl_;
    impl_ = nullptr;
    started_ = false;
}

}  // namespace pad
