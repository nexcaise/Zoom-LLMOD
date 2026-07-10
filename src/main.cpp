#include <Windows.h>
#include <cstdint>
#include <atomic>
#include <thread>
#include <cmath>

#include <libhat/scanner.hpp>
#include <libhat/signature.hpp>

#include <MinHook.h>

#pragma pack(push, 1)
struct LevelRendererPlayer {
    uint8_t padding_x[0xFF0];
    float fov_x;
    uint8_t padding_y[0x1004 - 0xFF0 - 4];
    float fov_y;
};

struct LevelRenderer {
    uint8_t padding[0x478];
    LevelRendererPlayer* player;
};
#pragma pack(pop)

using RenderLevelFn = void(__fastcall*)(LevelRenderer*, void*, void*);

static std::atomic<RenderLevelFn> g_originalRenderLevel{ nullptr };
static float g_zoomModifier = 1.0f;
constexpr float TARGET_ZOOM = 10.0f;

void __fastcall DetourRenderLevel(LevelRenderer* levelRenderer, void* screenContext, void* unk) {
    auto original = g_originalRenderLevel.load(std::memory_order_acquire);
    if (original) {
        original(levelRenderer, screenContext, unk);
    }

    if (levelRenderer && levelRenderer->player) {
        bool isCPressed = (GetAsyncKeyState('C') & 0x8000) != 0;
        float target = isCPressed ? TARGET_ZOOM : 1.0f;

        g_zoomModifier += (target - g_zoomModifier) * 0.1f;

        levelRenderer->player->fov_x *= g_zoomModifier;
        levelRenderer->player->fov_y *= g_zoomModifier;
    }
}

uintptr_t scanfunc(std::string pattern) {
	hat::result res = hat::parse_signature(pattern);
	if (!res.has_value()) return 0;
	hat::scan_result addr = hat::find_pattern(res.value(), ".text");
	if (!addr.has_result()) return 0;

	return (uintptr_t)addr.get();
}

void Initialize() {
    if (MH_Initialize() != MH_OK)
        return;

    void* target = (void*)scanfunc("E8 ? ? ? ? 45 31 E4 48 83 BE");

    void* original = nullptr;
    if (MH_CreateHook(target, &DetourRenderLevel, &original) == MH_OK) {
        g_originalRenderLevel.store(reinterpret_cast<RenderLevelFn>(original), std::memory_order_release);
        MH_EnableHook(target);
    }
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID reserved) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(hModule);
        std::thread(Initialize).detach();
    }
    return TRUE;
}