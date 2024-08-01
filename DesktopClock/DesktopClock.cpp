// DesktopClock.cpp : Defines the entry point for the application.
//
#pragma warning(disable : 4996)

#include "framework.h"
#include "DesktopClock.h"

#include <exception>
#include <cstdio>
#include <tuple>
#include <string>
#include <codecvt>
#include <locale>
#include <atomic>
#include <chrono>
#include <thread>
#include <iomanip>

constexpr auto MAX_LOADSTRING = 100;

// Global Variables:
HINSTANCE hInst;                      // current instance
WCHAR szTitle[MAX_LOADSTRING];        // The title bar text
WCHAR szWindowClass[MAX_LOADSTRING];  // the main window class name

// Forward declarations of functions included in this code module:
ATOM MyRegisterClass(HINSTANCE hInstance);
BOOL InitInstance(HINSTANCE, int);
LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);

// Direct2D globals
static ComPtr<ID2D1Factory> g_pD2DFactory;
static ComPtr<IDWriteFactory> g_pDWriteFactory;
static ComPtr<ID2D1HwndRenderTarget> g_pD2DRenderTarget;
static ComPtr<IDWriteTextFormat> g_pTimeFont;
static ComPtr<IDWriteTextFormat> g_pDateFont;

namespace {
    class com_exception : public std::exception {
    public:
        com_exception(HRESULT hr) noexcept : result(hr) {}

        const char* what() const noexcept override {
            static char s_str[64] = {};
            sprintf_s(s_str, "Failure with HRESULT of %08X", static_cast<unsigned int>(result));
            return s_str;
        }

    private:
        HRESULT result;
    };

    void ThrowIfFailed(HRESULT hr) {
        if (FAILED(hr)) {
            throw com_exception(hr);
        }
    }

    void ANSIToWide(const std::string& value, std::wstring& converted) {
        std::wstring_convert<std::codecvt_utf8_utf16<wchar_t>> converter;
        converted = converter.from_bytes(value);
    }
}  // namespace

namespace Clock {
    static std::atomic<bool> g_aRunning(true);

    static void ClockThread(HWND hwnd) {
        while (g_aRunning) {
            std::this_thread::sleep_for(std::chrono::seconds(1));
            InvalidateRect(hwnd, nullptr, false);  // Trigger a redraw
        }
    }

    static std::wstring GetCurrentDay() {
        auto now            = std::chrono::system_clock::now();
        std::time_t nowTime = std::chrono::system_clock::to_time_t(now);
        std::tm* nowTm      = std::localtime(&nowTime);

        std::wstring weekDay;
        switch (nowTm->tm_wday) {
            case 0:
                return L"SUNDAY";
            case 1:
                return L"MONDAY";
            case 2:
                return L"TUESDAY";
            case 3:
                return L"WEDNESDAY";
            case 4:
                return L"THURSDAY";
            case 5:
                return L"FRIDAY";
            case 6:
                return L"SATURDAY";
            default:
                return L"";
        }
    }

    static std::wstring GetTime() {
        SYSTEMTIME time;
        GetLocalTime(&time);

        int hour            = time.wHour;
        const wchar_t* ampm = L"AM";

        if (hour == 0)
            hour = 12;
        else if (hour == 12)
            ampm = L"PM";
        else if (hour > 12) {
            hour -= 12;
            ampm = L"PM";
        }

        wchar_t timeStr[100];
        swprintf_s(timeStr, L"%02d:%02d:%02d %s", hour, time.wMinute, time.wSecond, ampm);
        return timeStr;
    }

    static std::wstring GetCurrentDate() {
        auto now            = std::chrono::system_clock::now();
        std::time_t nowTime = std::chrono::system_clock::to_time_t(now);
        std::tm* nowTm      = std::localtime(&nowTime);
        char dateStr[100];
        std::strftime(dateStr, sizeof(dateStr), "%B %d, %Y", nowTm);

        std::wstring out;
        ANSIToWide(dateStr, out);
        // Convert text to all uppercase letters
        std::transform(out.begin(), out.end(), out.begin(), [](unsigned char t) {
            return std::toupper(t);
        });

        return out;
    }
}  // namespace Clock

static void InitializeD2D(HWND hwnd) {
    ThrowIfFailed(
      D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, IID_PPV_ARGS(&g_pD2DFactory)));

    RECT rc;
    GetClientRect(hwnd, &rc);
    ThrowIfFailed(g_pD2DFactory->CreateHwndRenderTarget(
      D2D1::RenderTargetProperties(),
      D2D1::HwndRenderTargetProperties(hwnd, D2D1::SizeU(rc.right, rc.bottom)),
      &g_pD2DRenderTarget));

    ThrowIfFailed(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED,
                                      __uuidof(IDWriteFactory),
                                      (IUnknown**)&g_pDWriteFactory));

    // Create fonts
    ThrowIfFailed(g_pDWriteFactory->CreateTextFormat(L"Chakra Petch",
                                                     nullptr,
                                                     DWRITE_FONT_WEIGHT_BLACK,
                                                     DWRITE_FONT_STYLE_NORMAL,
                                                     DWRITE_FONT_STRETCH_NORMAL,
                                                     200.f,
                                                     L"en-us",
                                                     &g_pTimeFont));
    g_pTimeFont->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
    g_pTimeFont->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

    ThrowIfFailed(g_pDWriteFactory->CreateTextFormat(L"Chakra Petch",
                                                     nullptr,
                                                     DWRITE_FONT_WEIGHT_LIGHT,
                                                     DWRITE_FONT_STYLE_NORMAL,
                                                     DWRITE_FONT_STRETCH_NORMAL,
                                                     48.f,
                                                     L"en-us",
                                                     &g_pDateFont));
    g_pDateFont->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
    g_pDateFont->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
}

static void Render(HWND hwnd) {
    if (g_pD2DRenderTarget) {
        const auto rt = g_pD2DRenderTarget.Get();
        rt->BeginDraw();
        rt->Clear(D2D1::ColorF(0.f, 0.f, 0.f, 1.f));

        RECT rc;
        GetClientRect(hwnd, &rc);

        ID2D1SolidColorBrush* timeBrush = nullptr;
        ThrowIfFailed(rt->CreateSolidColorBrush(D2D1::ColorF(1.f, 1.f, 1.f, 1.f), &timeBrush));
        ID2D1SolidColorBrush* dateBrush = nullptr;
        ThrowIfFailed(rt->CreateSolidColorBrush(D2D1::ColorF(0.4f, 0.4f, 0.4f, 1.f), &dateBrush));

        auto day  = Clock::GetCurrentDay();
        auto time = Clock::GetTime();
        auto date = Clock::GetCurrentDate();

        rt->DrawTextW(day.c_str(),
                      wcslen(day.c_str()),
                      g_pDateFont.Get(),
                      {0.f, 0.f, (float)rc.right, (float)rc.bottom - 300},
                      dateBrush);
        rt->DrawTextW(time.c_str(),
                      wcslen(time.c_str()),
                      g_pTimeFont.Get(),
                      {0.f, 0.f, (float)rc.right, (float)rc.bottom},
                      timeBrush);
        rt->DrawTextW(date.c_str(),
                      wcslen(date.c_str()),
                      g_pDateFont.Get(),
                      {0.f, 0.f, (float)rc.right, (float)rc.bottom + 300},
                      dateBrush);

        ThrowIfFailed(rt->EndDraw());
    }
}

static void ShutdownD2D() {
    if (g_pD2DRenderTarget) {
        g_pD2DRenderTarget.Reset();
    }

    if (g_pDWriteFactory) {
        g_pDWriteFactory.Reset();
    }

    if (g_pD2DFactory) {
        g_pD2DFactory.Reset();
    }
}

int APIENTRY wWinMain(_In_ HINSTANCE hInstance,
                      _In_opt_ HINSTANCE hPrevInstance,
                      _In_ LPWSTR lpCmdLine,
                      _In_ int nCmdShow) {
    UNREFERENCED_PARAMETER(hPrevInstance);
    UNREFERENCED_PARAMETER(lpCmdLine);

    ThrowIfFailed(CoInitializeEx(nullptr, COINIT_MULTITHREADED));

    // Initialize global strings
    LoadStringW(hInstance, IDS_APP_TITLE, szTitle, MAX_LOADSTRING);
    LoadStringW(hInstance, IDC_DESKTOPCLOCK, szWindowClass, MAX_LOADSTRING);
    MyRegisterClass(hInstance);

    // Perform application initialization:
    if (!InitInstance(hInstance, nCmdShow)) {
        return FALSE;
    }

    HACCEL hAccelTable = LoadAccelerators(hInstance, MAKEINTRESOURCE(IDC_DESKTOPCLOCK));

    MSG msg;

    // Main message loop:
    while (GetMessage(&msg, nullptr, 0, 0)) {
        if (!TranslateAccelerator(msg.hwnd, hAccelTable, &msg)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
    }

    Clock::g_aRunning = false;

    CoUninitialize();

    return (int)msg.wParam;
}

ATOM MyRegisterClass(HINSTANCE hInstance) {
    WNDCLASSEXW wcex;

    wcex.cbSize = sizeof(WNDCLASSEX);

    wcex.style         = CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS;
    wcex.lpfnWndProc   = WndProc;
    wcex.cbClsExtra    = 0;
    wcex.cbWndExtra    = 0;
    wcex.hInstance     = hInstance;
    wcex.hIcon         = LoadIcon(hInstance, MAKEINTRESOURCE(IDI_DESKTOPCLOCK));
    wcex.hCursor       = LoadCursor(nullptr, IDC_SIZEALL);
    wcex.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wcex.lpszMenuName  = nullptr;
    wcex.lpszClassName = szWindowClass;
    wcex.hIconSm       = LoadIcon(wcex.hInstance, MAKEINTRESOURCE(IDI_SMALL));

    return RegisterClassExW(&wcex);
}

BOOL InitInstance(HINSTANCE hInstance, int nCmdShow) {
    hInst = hInstance;  // Store instance handle in our global variable

    HWND hWnd = CreateWindowExW(WS_EX_TOPMOST,
                                szWindowClass,
                                L"Desktop Clock",
                                WS_POPUP,
                                CW_USEDEFAULT,
                                CW_USEDEFAULT,
                                1280,
                                720,
                                nullptr,
                                nullptr,
                                hInstance,
                                nullptr);

    if (!hWnd) {
        return FALSE;
    }

    InitializeD2D(hWnd);

    ShowWindow(hWnd, SW_SHOWMAXIMIZED);
    UpdateWindow(hWnd);

    // Start the clock thread
    std::thread timer(Clock::ClockThread, hWnd);
    timer.detach();

    return TRUE;
}

LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam) {
    static bool isMaximized = true;

    switch (message) {
        case WM_PAINT: {
            Render(hWnd);
            ValidateRect(hWnd, nullptr);
        } break;
        case WM_DESTROY:
            ShutdownD2D();
            PostQuitMessage(0);
            break;
        case WM_SIZE: {
            if (g_pD2DRenderTarget) {
                RECT rc;
                GetClientRect(hWnd, &rc);
                D2D1_SIZE_U size = {rc.right, rc.bottom};
                g_pD2DRenderTarget->Resize(size);
            }

            if (wParam == SIZE_MAXIMIZED) {
                isMaximized = true;
            } else if (wParam == SIZE_RESTORED) {
                isMaximized = false;
            }
        } break;
        case WM_LBUTTONDOWN: {
            ReleaseCapture();
            SendMessage(hWnd, WM_NCLBUTTONDOWN, HTCAPTION, 0);
        } break;
        case WM_LBUTTONDBLCLK: {
            if (isMaximized) {
                ShowWindow(hWnd, SW_RESTORE);
            } else {
                ShowWindow(hWnd, SW_MAXIMIZE);
            }
            isMaximized = !isMaximized;
        } break;
        case WM_NCHITTEST: {
            LRESULT hit = DefWindowProc(hWnd, message, wParam, lParam);
            if (hit == HTCLIENT) {
                POINT pt;
                pt.x = GET_X_LPARAM(lParam);
                pt.y = GET_Y_LPARAM(lParam);
                ScreenToClient(hWnd, &pt);

                RECT rc;
                GetClientRect(hWnd, &rc);
                if (pt.y < rc.top + 10) {
                    return HTCAPTION;
                }
            }
            return hit;
        } break;
        default:
            return DefWindowProc(hWnd, message, wParam, lParam);
    }
    return 0;
}