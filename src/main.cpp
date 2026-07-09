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

void Initialize() {
    if (MH_Initialize() != MH_OK)
        return;

    constexpr auto signature = hat::compile_signature<
        "48 8B C4 48 89 58 ? 55 56 57 41 54 41 55 41 56 41 57 "
        "48 8D A8 ? ? ? ? 48 81 EC ? ? ? ? 0F 29 70 ? 0F 29 78 ? "
        "44 0F 29 40 ? 44 0F 29 48 ? 44 0F 29 90 ? ? ? ? "
        "44 0F 29 98 ? ? ? ? 44 0F 29 A0 ? ? ? ? "
        "48 8B 05 ? ? ? ? 48 33 C4 48 89 85 ? ? ? ? 4D 8B F0"
    >();

    auto scanResult = hat::find_pattern(signature, ".text");
    if (scanResult.get() == nullptr) {
        return;
    }

    void* target = scanResult.get();

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