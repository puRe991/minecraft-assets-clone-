// app/main.cpp
//
// The runnable Windows application: it wires the engine modules together into
// a playable first-person voxel world. A Win32 window presents a software
// framebuffer (StretchDIBits); input drives the physics PlayerController;
// TerrainGenerator + World stream chunks around the player; blocks can be
// broken and placed; a day/night cycle animates the light; and the world is
// autosaved via WorldSaver. Self-contained: only Win32 + GDI, no libraries.
#include <windows.h>

#include <cmath>
#include <memory>
#include <string>

#include "Renderer.hpp"
#include "vg/physics/PlayerController.hpp"
#include "vg/save/WorldSaver.hpp"
#include "vg/world/TerrainGenerator.hpp"

using namespace vg;

static const int RW = 480, RH = 270;     // internal render resolution
static const int WW = 960, WH = 540;     // window size

static uint32_t g_fb[RW * RH];
static int g_keys[256];
static int g_running = 1, g_focused = 1;
static int g_clientW = WW, g_clientH = WH;
static double g_pitch = -0.1;
static int g_break = 0, g_place = 0;
static world::BlockId g_selected = world::blocks::Cobblestone;
static HWND g_hwnd;

static LRESULT CALLBACK WndProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    switch (m) {
        case WM_DESTROY: g_running = 0; PostQuitMessage(0); return 0;
        case WM_SIZE: g_clientW = LOWORD(l); g_clientH = HIWORD(l); return 0;
        case WM_SETFOCUS: g_focused = 1; return 0;
        case WM_KILLFOCUS: g_focused = 0; return 0;
        case WM_KEYDOWN:
            if (w < 256) g_keys[w] = 1;
            if (w == VK_ESCAPE) { g_running = 0; PostQuitMessage(0); }
            if (w >= '1' && w <= '9') {
                const world::BlockId bar[9] = {
                    world::blocks::Cobblestone, world::blocks::OakPlanks, world::blocks::Dirt,
                    world::blocks::Sand, world::blocks::OakLog, world::blocks::Glass,
                    world::blocks::Stone, world::blocks::OakLeaves, world::blocks::Torch};
                g_selected = bar[w - '1'];
            }
            return 0;
        case WM_KEYUP: if (w < 256) g_keys[w] = 0; return 0;
        case WM_LBUTTONDOWN: g_break = 1; return 0;
        case WM_RBUTTONDOWN: g_place = 1; return 0;
    }
    return DefWindowProc(h, m, w, l);
}

static void centerMouse() {
    RECT rc; GetClientRect(g_hwnd, &rc);
    POINT p{(rc.right - rc.left) / 2, (rc.bottom - rc.top) / 2};
    ClientToScreen(g_hwnd, &p);
    SetCursorPos(p.x, p.y);
}

int WINAPI WinMain(HINSTANCE hInst, HINSTANCE, LPSTR, int nShow) {
    WNDCLASS wc = {};
    wc.lpfnWndProc = WndProc; wc.hInstance = hInst;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.lpszClassName = "VoxelGameWnd";
    RegisterClass(&wc);
    RECT r{0, 0, WW, WH};
    AdjustWindowRect(&r, WS_OVERLAPPEDWINDOW, FALSE);
    g_hwnd = CreateWindow("VoxelGameWnd", "VoxelGame (modular engine) - 32/64-bit",
                          WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
                          r.right - r.left, r.bottom - r.top, NULL, NULL, hInst, NULL);
    ShowWindow(g_hwnd, nShow); UpdateWindow(g_hwnd);

    // --- engine set-up ---
    world::BlockRegistry reg; registerDefaultBlocks(reg);
    unsigned seed = (unsigned)GetTickCount();
    auto gen = std::make_shared<world::TerrainGenerator>(world::TerrainConfig{seed, 40});
    world::World world(gen);

    save::WorldSaver saver("voxel_save");
    save::AutoSaver autosave(30.0);

    physics::PlayerController player(world, reg);
    // spawn on the surface at the origin
    world.updateStreaming(0, 0, 6);
    int surf = gen->columnAt(0, 0).height;
    player.setPosition({0.5, (double)surf + 2.0, 0.5});
    double yaw = 0.0, tod = 0.30;

    BITMAPINFO bmi = {};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = RW; bmi.bmiHeader.biHeight = -RH;
    bmi.bmiHeader.biPlanes = 1; bmi.bmiHeader.biBitCount = 32; bmi.bmiHeader.biCompression = BI_RGB;

    LARGE_INTEGER freq, prev, now;
    QueryPerformanceFrequency(&freq); QueryPerformanceCounter(&prev);
    HDC hdc = GetDC(g_hwnd);
    int mouseInit = 0;

    while (g_running) {
        MSG msg;
        while (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) { TranslateMessage(&msg); DispatchMessage(&msg); }
        if (!g_running) break;

        QueryPerformanceCounter(&now);
        double dt = (double)(now.QuadPart - prev.QuadPart) / freq.QuadPart;
        prev = now; if (dt > 0.1) dt = 0.1;

        // mouse look
        if (g_focused) {
            RECT rc; GetClientRect(g_hwnd, &rc);
            int cx = (rc.right - rc.left) / 2, cy = (rc.bottom - rc.top) / 2;
            POINT p; GetCursorPos(&p); ScreenToClient(g_hwnd, &p);
            if (mouseInit) {
                yaw -= (p.x - cx) * 0.0025;
                g_pitch -= (p.y - cy) * 0.0025;
                if (g_pitch > 1.55) g_pitch = 1.55;
                if (g_pitch < -1.55) g_pitch = -1.55;
            }
            centerMouse(); mouseInit = 1; ShowCursor(FALSE);
        } else { mouseInit = 0; ShowCursor(TRUE); }

        // input -> physics
        physics::PlayerInput in;
        in.forward = g_keys['W']; in.back = g_keys['S'];
        in.left = g_keys['A']; in.right = g_keys['D'];
        in.jump = g_keys[VK_SPACE]; in.crouch = g_keys[VK_SHIFT];
        in.sprint = g_keys[VK_CONTROL]; in.yaw = yaw;
        player.update(in, dt);

        // stream chunks around the player
        world.updateStreaming((int)std::floor(player.position().x),
                              (int)std::floor(player.position().z), 6);

        // camera + block interaction ray
        app::Camera cam;
        cam.pos = player.eye(); cam.yaw = yaw; cam.pitch = g_pitch;
        double cp = std::cos(g_pitch), sp = std::sin(g_pitch), cyw = std::cos(yaw), syw = std::sin(yaw);
        math::Vec3d dir{cp * syw, sp, cp * cyw};
        if (g_break || g_place) {
            int hx, hy, hz, nx, ny, nz; double d; world::BlockId blk;
            if (app::raycast(world, reg, cam.pos, dir, 6.0, hx, hy, hz, nx, ny, nz, d, blk)) {
                if (g_break) world.setBlock(hx, hy, hz, world::blocks::Air);
                else if (world.getBlock(hx + nx, hy + ny, hz + nz) == world::blocks::Air)
                    world.setBlock(hx + nx, hy + ny, hz + nz, g_selected);
            }
            g_break = g_place = 0;
        }

        // day/night
        tod += dt / 240.0; if (tod >= 1.0) tod -= 1.0;
        double daylight = 0.5 - 0.5 * std::cos(2 * 3.14159265 * tod);

        // render + present
        app::renderWorld(g_fb, RW, RH, world, reg, cam, daylight, 96.0);
        int cxp = RW / 2, cyp = RH / 2;                       // crosshair
        for (int i = -4; i <= 4; ++i) { g_fb[cyp * RW + cxp + i] ^= 0x00ffffff; g_fb[(cyp + i) * RW + cxp] ^= 0x00ffffff; }
        StretchDIBits(hdc, 0, 0, g_clientW, g_clientH, 0, 0, RW, RH, g_fb, &bmi, DIB_RGB_COLORS, SRCCOPY);

        if (autosave.tick(dt)) saver.saveDirty(world);
    }

    saver.saveDirty(world);
    saver.saveMeta({seed, player.position().x, player.position().y, player.position().z});
    ReleaseDC(g_hwnd, hdc);
    return 0;
}
