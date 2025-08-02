#define UNICODE
#define _UNICODE

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <windows.h>
#include <string>
#include <vector>
#include <thread>
#include <chrono>
#include "digit_templates.h"

const int REGION_X = 1800;   // Adjust based on your screen/resolution
const int REGION_Y = 20;
const int REGION_W = 80;
const int REGION_H = 20;

enum CursorShape { CIRCLE, SQUARE, CROSSHAIR };

//Default vaules
int CURSOR_RADIUS = 10;
COLORREF cursorColor = RGB(0, 255, 0);
CursorShape cursorShape = CIRCLE;

//reads config file
void LoadCursorConfig(const char* filename) {
    FILE* file = fopen(filename, "r");
    if (!file) return;
    char line[256];
    while (fgets(line, sizeof(line), file)) {
        if (strstr(line, "\"CURSOR_RADIUS\"")) {
            char* colon = strchr(line, ':');
            if (colon) CURSOR_RADIUS = atoi(colon + 1);
        }
        if (strstr(line, "\"CURSOR_SHAPE\"")) {
            if (strstr(line, "square")) cursorShape = SQUARE;
            else if (strstr(line, "crosshair")) cursorShape = CROSSHAIR;
            else cursorShape = CIRCLE;
        }
    }
    fclose(file);
}

HWND hOverlay;
bool running = true;
//COLORREF cursorColor = RGB(255, 255, 255);

COLORREF GetColorForFPS(int fps) {
    if (fps >= 60) return RGB(0, 255, 0);
    else if (fps >= 30) return RGB(255, 255, 0);
    else return RGB(255, 0, 0);
}

// Extract monochrome bitmap from screen region
std::vector<bool> CaptureFPSRegion() {
    HDC hdcScreen = GetDC(NULL);
    HDC hdcMem = CreateCompatibleDC(hdcScreen);
    HBITMAP hBitmap = CreateCompatibleBitmap(hdcScreen, REGION_W, REGION_H);
    SelectObject(hdcMem, hBitmap);
    BitBlt(hdcMem, 0, 0, REGION_W, REGION_H, hdcScreen, REGION_X, REGION_Y, SRCCOPY);

    BITMAPINFO bmi = {};
    bmi.bmiHeader.biSize = sizeof(bmi.bmiHeader);
    bmi.bmiHeader.biWidth = REGION_W;
    bmi.bmiHeader.biHeight = -REGION_H;  // top-down
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 24;
    bmi.bmiHeader.biCompression = BI_RGB;

    std::vector<unsigned char> pixels(REGION_W * REGION_H * 3);
    GetDIBits(hdcMem, hBitmap, 0, REGION_H, pixels.data(), &bmi, DIB_RGB_COLORS);

    std::vector<bool> mono;
    for (int i = 0; i < REGION_W * REGION_H; ++i) {
        int r = pixels[i * 3 + 2];
        int g = pixels[i * 3 + 1];
        int b = pixels[i * 3 + 0];
        mono.push_back((r + g + b) / 3 > 128 ? 0 : 1);
    }

    DeleteObject(hBitmap);
    DeleteDC(hdcMem);
    ReleaseDC(NULL, hdcScreen);
    return mono;
}

// Match a single digit at x,y position
int MatchDigitAt(const std::vector<bool>& region, int x, int y) {
    for (int digit = 0; digit <= 9; ++digit) {
        bool match = true;
        const char* pattern = DIGITS[digit];
        for (int row = 0; row < DIGIT_HEIGHT; ++row) {
            for (int col = 0; col < DIGIT_WIDTH; ++col) {
                int idx = (y + row) * REGION_W + (x + col);
                if (idx >= region.size()) continue;
                int bit = (pattern[row * DIGIT_WIDTH + col] == '1');
                if (region[idx] != bit) {
                    match = false;
                    break;
                }
            }
            if (!match) break;
        }
        if (match) return digit;
    }
    return -1;
}

// Parse all digits left to right
int ParseFPSFromRegion(const std::vector<bool>& region) {
    std::string digits;
    for (int x = 0; x < REGION_W - DIGIT_WIDTH; ++x) {
        int d = MatchDigitAt(region, x, 0);
        if (d != -1) {
            digits += '0' + d;
            x += DIGIT_WIDTH - 1; // skip forward
        }
    }
    return digits.empty() ? -1 : std::stoi(digits);
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);

        RECT rc;
        GetClientRect(hwnd, &rc);
        int cx = (rc.right - rc.left) / 2;
        int cy = (rc.bottom - rc.top) / 2;

        HBRUSH brush = CreateSolidBrush(cursorColor);
        HPEN pen = CreatePen(PS_SOLID, 2, cursorColor);
        SelectObject(hdc, brush);
        SelectObject(hdc, pen);

        switch (cursorShape) {
        case CIRCLE:
            Ellipse(hdc, cx - CURSOR_RADIUS, cy - CURSOR_RADIUS, cx + CURSOR_RADIUS, cy + CURSOR_RADIUS);
            break;
        case SQUARE:
            Rectangle(hdc, cx - CURSOR_RADIUS, cy - CURSOR_RADIUS, cx + CURSOR_RADIUS, cy + CURSOR_RADIUS);
            break;
        case CROSSHAIR:
            MoveToEx(hdc, cx - CURSOR_RADIUS, cy, NULL);
            LineTo(hdc, cx + CURSOR_RADIUS, cy);
            MoveToEx(hdc, cx, cy - CURSOR_RADIUS, NULL);
            LineTo(hdc, cx, cy + CURSOR_RADIUS);
            break;
        }

        DeleteObject(brush);
        DeleteObject(pen);
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_DESTROY:
        running = false;
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProc(hwnd, msg, wParam, lParam);
}

/*void UpdateLoop() {
    while (running) {
        auto mono = CaptureFPSRegion();
        int fps = ParseFPSFromRegion(mono);
        if (fps != -1) {
            cursorColor = GetColorForFPS(fps);
            OutputDebugStringA(("FPS: " + std::to_string(fps) + "\n").c_str());
        } else {
            cursorColor = RGB(128, 128, 128);  // unknown
        }
        InvalidateRect(hOverlay, NULL, TRUE);
        Sleep(1000);
    }
	return 0;
}*/

DWORD WINAPI UpdateLoop(LPVOID) {
    while (running) {
        auto mono = CaptureFPSRegion();
        int fps = ParseFPSFromRegion(mono);
        if (fps != -1) {
            cursorColor = GetColorForFPS(fps);
            OutputDebugStringA(("FPS: " + std::to_string(fps) + "\n").c_str());
        } else {
            cursorColor = RGB(128, 128, 128);  // unknown
        }
        InvalidateRect(hOverlay, NULL, TRUE);
        Sleep(1000);
    }
    return 0;
}

/*DWORD WINAPI UpdateLoopThread(LPVOID) {
    UpdateLoop();
    return 0;
}*/


int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int) {
    LoadCursorConfig("config.json");

    WNDCLASSW wc = {};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = L"OverlayCursor";
    wc.hbrBackground = (HBRUSH)GetStockObject(NULL_BRUSH);
    RegisterClassW(&wc);

    hOverlay = CreateWindowExW(
        WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOPMOST | WS_EX_TOOLWINDOW,
        L"OverlayCursor", L"",
        WS_POPUP,
        0, 0, GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN),
        NULL, NULL, hInstance, NULL
    );

    if (!hOverlay) {
        MessageBoxA(NULL, "Failed to create overlay window", "Error", MB_OK);
        return -1;
    }

    SetLayeredWindowAttributes(hOverlay, RGB(0, 0, 0), 0, LWA_COLORKEY);
    ShowWindow(hOverlay, SW_SHOW);

    CreateThread(NULL, 0, UpdateLoop, NULL, 0, NULL);

    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    return 0;
}