// Vehicle Parker -- Ctrl+F10, as plugins\mt_plugin_VehicleParker.dll.
//
// Despawns every vehicle you own that is not a camper (class Campy_C or a
// subclass of it), through the game's own ServerDespawnVehicle -- the vehicle
// list's Despawn button -- so vehicles go back to your list, nothing is deleted.
//
// Ported from capitalist_economy\src\dllmain.cpp unchanged in behaviour; it no
// longer hooks ProcessEvent itself but asks the loader to run it on the game
// thread. Remove the DLL to turn it off.

#include <string>
#include <vector>
#include "../include/mt_plugin.hpp"

using namespace mt;

static bool classIs(uintptr_t cls, const char* prefix) {
    for (int i = 0; cls && i < 32; i++, cls = superOf(cls))
        if (fname(cls + L().obj_name).rfind(prefix, 0) == 0) return true;
    return false;
}

static void doDespawn(void*) {
    uintptr_t pcCls = api->find_object("Class", "PlayerController", "/Script/Engine");
    uintptr_t vehCls = api->find_object("Class", "MTVehicle", "/Script/MotorTown");
    uintptr_t funcCls = api->find_object("Class", "Function", "/Script/CoreUObject");
    uintptr_t pc = 0;
    for (int i = 0, n = api->num_objects(); i < n && !pc; i++) {
        uintptr_t o = api->object(i);
        if (o && !isCDO(o) && api->is_a(classOf(o), pcCls)) pc = o;
    }
    if (!pc) { MT_LOG("despawn: no player controller"); return; }
    uintptr_t fn = 0;
    for (int i = 0, n = api->num_objects(); i < n && !fn; i++) {
        uintptr_t o = api->object(i);
        if (o && classOf(o) == funcCls && fname(o + L().obj_name) == "ServerDespawnVehicle"
            && api->is_a(classOf(pc), outerOf(o))) fn = o;
    }
    std::string pcName = fname(classOf(pc) + L().obj_name);
    if (!fn) { MT_LOG("despawn: ServerDespawnVehicle not found on %s", pcName.c_str()); return; }

    // the parameter list, read from the function itself
    struct Param { std::string name, type; int off; };
    std::vector<Param> params;
    for (uintptr_t f = get<uintptr_t>(fn + L().struct_child_props); f; f = get<uintptr_t>(f + L().field_next))
        params.push_back({ fname(f + L().field_name), name(get<uint32_t>(get<uintptr_t>(f + 8))),
                           get<int>(f + L().field_offset) });
    int psize = get<int>(fn + L().struct_child_props + 8);
    std::string sig; for (auto& q : params) sig += q.name + ":" + q.type + " ";
    MT_LOG("despawn: %s::ServerDespawnVehicle( %s) size %d", pcName.c_str(), sig.c_str(), psize);
    for (auto& q : params)
        if (q.type != "ObjectProperty" && q.type != "Int64Property" && q.type != "BoolProperty") {
            MT_LOG("despawn: ABORT, parameter %s is %s -- not something I know how to fill", q.name.c_str(), q.type.c_str());
            return;
        }

    int oPS = off(classOf(pc), "PlayerState");
    uintptr_t myPS = oPS >= 0 ? get<uintptr_t>(pc + oPS) : 0;
    int oOwnerPS = off(vehCls, "Net_OwnerPlayerState"), oVid = off(vehCls, "Net_VehicleId");
    if (!myPS || oOwnerPS < 0 || oVid < 0) { MT_LOG("despawn: ABORT, player state %p owner+%x id+%x", (void*)myPS, oOwnerPS, oVid); return; }

    std::vector<uintptr_t> mine;
    int campers = 0, others = 0;
    for (int i = 0, n = api->num_objects(); i < n; i++) {
        uintptr_t o = api->object(i);
        if (!o || isCDO(o) || !api->is_a(classOf(o), vehCls)) continue;
        if (get<uintptr_t>(o + oOwnerPS) != myPS) { others++; continue; }
        if (classIs(classOf(o), "Campy")) {
            campers++;
            MT_LOG("despawn: keep  %-28s id %lld (camper)", fname(classOf(o) + L().obj_name).c_str(), get<int64_t>(o + oVid));
            continue;
        }
        mine.push_back(o);
    }
    MT_LOG("despawn: %zu of yours to despawn, %d campers kept, %d vehicles not yours untouched", mine.size(), campers, others);
    std::vector<uint8_t> buf(psize > 0 ? psize : 0);
    for (uintptr_t v : mine) {
        std::fill(buf.begin(), buf.end(), 0);
        int64_t vid = get<int64_t>(v + oVid);
        for (auto& q : params) {
            if (q.off < 0 || q.off + 8 > psize) continue;
            if (q.type == "ObjectProperty") memcpy(buf.data() + q.off, &v, 8);
            else if (q.type == "Int64Property" && q.name.find("Id") != std::string::npos) memcpy(buf.data() + q.off, &vid, 8);
            // anything else (Cost) stays 0: despawning through the plugin is free
        }
        MT_LOG("despawn: -> %-28s id %lld", fname(classOf(v) + L().obj_name).c_str(), vid);
        api->process_event(pc, fn, buf.data());
    }
    MT_LOG("despawn: done");
}

static DWORD WINAPI hotkeys(LPVOID) {
    bool down = false, announced = false;
    for (;;) {
        Sleep(50);
        if (!announced && api->game_thread_ready()) { MT_LOG("despawn: Ctrl+F10 ready"); announced = true; }
        bool now = (GetAsyncKeyState(VK_CONTROL) & 0x8000) && (GetAsyncKeyState(VK_F10) & 0x8000);
        // Like before: a press counts only once the game-thread hook is in.
        if (now && !down && api->game_thread_ready()) {
            MT_LOG("despawn: Ctrl+F10 pressed");
            api->run_on_game_thread(doDespawn, nullptr);
        }
        down = now;
    }
}

extern "C" __declspec(dllexport) int PluginInit(const MtApi* a) {
    if (!attach(a)) return 1;
    if (iniDisabled(L"vehicle_parker.ini")) return 0;
    CloseHandle(CreateThread(nullptr, 0, hotkeys, nullptr, 0, nullptr));
    return 0;
}
