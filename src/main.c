#define UNICODE
#define _UNICODE
#include <windows.h>
#include <windowsx.h>
#include <stdio.h>
#include <wchar.h>
#include <time.h>  // 追加

#define HISTORY 200  // 履歴点数

int cpuHistory[HISTORY] = {0};
int memHistory[HISTORY] = {0};
HFONT hFont;
BOOL darkMode = FALSE;
BOOL loggingActive = FALSE;  // ログ記録の実行状態

typedef struct {
    ULONGLONG idle, kernel, user;
} CPU_TIMES;

CPU_TIMES lastTimes = {0};

// CPU使用率取得
int GetCPUUsage() {
    FILETIME idle, kernel, user;
    if (!GetSystemTimes(&idle, &kernel, &user)) return -1;

    ULONGLONG idleNow = ((ULONGLONG)idle.dwLowDateTime | ((ULONGLONG)idle.dwHighDateTime << 32));
    ULONGLONG kernelNow = ((ULONGLONG)kernel.dwLowDateTime | ((ULONGLONG)kernel.dwHighDateTime << 32));
    ULONGLONG userNow = ((ULONGLONG)user.dwLowDateTime | ((ULONGLONG)user.dwHighDateTime << 32));

    ULONGLONG idleDiff = idleNow - lastTimes.idle;
    ULONGLONG kernelDiff = kernelNow - lastTimes.kernel;
    ULONGLONG userDiff = userNow - lastTimes.user;

    lastTimes.idle = idleNow;
    lastTimes.kernel = kernelNow;
    lastTimes.user = userNow;

    ULONGLONG total = kernelDiff + userDiff;
    if (total == 0) return 0;
    return (int)(100 - (idleDiff * 100 / total));
}

// メモリ使用率取得
int GetMemoryUsage() {
    MEMORYSTATUSEX mem = {0};
    mem.dwLength = sizeof(mem);
    GlobalMemoryStatusEx(&mem);
    return (int)mem.dwMemoryLoad;
}

void AddHistory(int *history, int value) {
    for (int i = 0; i < HISTORY-1; i++) {
        history[i] = history[i+1];
    }
    history[HISTORY-1] = value;
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    static int width = 600, height = 400;
    switch(msg) {
        case WM_CREATE:
            hFont = CreateFontW(18, 0, 0, 0, FW_NORMAL, 0, 0, 0, ANSI_CHARSET,
                                   OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY,
                                   DEFAULT_PITCH, L"Segoe UI");
            // 既存の計測タイマー（ID:1）
            SetTimer(hwnd, 1, 1000, NULL); // 1秒ごと更新
            break;

        case WM_SIZE:
            width = LOWORD(lParam);
            height = HIWORD(lParam);
            break;

        case WM_KEYDOWN:
            if (wParam == 'D') { // Dキーでダークモード切替
                darkMode = !darkMode;
                InvalidateRect(hwnd, NULL, TRUE);
            } else if (wParam == 'L') { // Lキーでログ記録の開始/停止をトグル
                loggingActive = !loggingActive;
                if (loggingActive) {
                    SetTimer(hwnd, 2, 1000, NULL); // タイマー ID 2 で開始
                } else {
                    KillTimer(hwnd, 2);
                }
            }
            break;

        case WM_TIMER: {
                if (wParam == 1) {
                    int cpu = GetCPUUsage();
                    int mem = GetMemoryUsage();
                    AddHistory(cpuHistory, cpu);
                    AddHistory(memHistory, mem);
                    InvalidateRect(hwnd, NULL, TRUE);
                } else if (wParam == 2) {
                    // ログ記録タイマー：現在の時刻、CPU、メモリ使用率をファイルに追記
                    FILE* fp = fopen("log.csv", "a");
                    if (fp != NULL) {
                        time_t now = time(NULL);
                        struct tm local;
                        localtime_r(&now, &local);
                        char timeStr[64];
                        strftime(timeStr, sizeof(timeStr), "%Y-%m-%d %H:%M:%S", &local);  // 時刻をフォーマット
                        int cpu = GetCPUUsage();
                        int mem = GetMemoryUsage();
                        // CSV形式：時刻,CPU使用率,メモリ使用率
                        fprintf(fp, "%s,%d%%,%d%%\n", timeStr, cpu, mem);
                        fclose(fp);
                    }
                }
            }
            break;

        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hwnd, &ps);
            RECT rect;
            GetClientRect(hwnd, &rect);
            SelectObject(hdc, hFont);

            int margin = 50;
            int graphW = width - margin * 2;
            int graphH = height - margin * 2;

            // 背景
            HBRUSH bg = CreateSolidBrush(darkMode ? RGB(20,20,20) : RGB(245,245,245));
            FillRect(hdc, &rect, bg);
            DeleteObject(bg);

            // グリッド
            HPEN gridPen = CreatePen(PS_DOT, 1, darkMode ? RGB(80,80,80) : RGB(200,200,200));
            SelectObject(hdc, gridPen);
            for (int i = 0; i <= 5; i++) {
                int y = margin + i * graphH / 5;
                MoveToEx(hdc, margin, y, NULL);
                LineTo(hdc, margin + graphW, y);
            }
            for (int i = 0; i <= 10; i++) {
                int x = margin + i * graphW / 10;
                MoveToEx(hdc, x, margin, NULL);
                LineTo(hdc, x, margin + graphH);
            }
            DeleteObject(gridPen);

            // 最新値によって描画順序を決める
            BOOL drawCPUOnTop = (cpuHistory[HISTORY-1] < memHistory[HISTORY-1]);

            if (drawCPUOnTop) {
                // まずメモリ塗りつぶしを描画
                {
                    POINT memPoints[HISTORY + 2];
                    memPoints[0].x = margin;
                    memPoints[0].y = margin + graphH;  // 左下
                    for (int i = 0; i < HISTORY; i++) {
                        int x = margin + i * graphW / HISTORY;
                        int y = margin + graphH - memHistory[i] * graphH / 100;
                        memPoints[i + 1].x = x;
                        memPoints[i + 1].y = y;
                    }
                    memPoints[HISTORY + 1].x = margin + graphW;
                    memPoints[HISTORY + 1].y = margin + graphH;  // 右下

                    HPEN hNullPen = (HPEN)GetStockObject(NULL_PEN);
                    HPEN oldPen = SelectObject(hdc, hNullPen);
                    HBRUSH memBrush;
                    if (darkMode)
                        memBrush = CreateSolidBrush(RGB(80,120,80));   // darkMode: 少し暗い緑
                    else
                        memBrush = CreateSolidBrush(RGB(200,255,200));   // lightMode: 薄い緑
                    HBRUSH oldBrush = SelectObject(hdc, memBrush);
                    Polygon(hdc, memPoints, HISTORY + 2);
                    SelectObject(hdc, oldPen);
                    SelectObject(hdc, oldBrush);
                    DeleteObject(memBrush);
                }
                // 次に、CPU塗りつぶし
                {
                    POINT cpuPoints[HISTORY + 2];
                    cpuPoints[0].x = margin;
                    cpuPoints[0].y = margin + graphH;  // 左下
                    for (int i = 0; i < HISTORY; i++) {
                        int x = margin + i * graphW / HISTORY;
                        int y = margin + graphH - cpuHistory[i] * graphH / 100;
                        cpuPoints[i + 1].x = x;
                        cpuPoints[i + 1].y = y;
                    }
                    cpuPoints[HISTORY + 1].x = margin + graphW;
                    cpuPoints[HISTORY + 1].y = margin + graphH;  // 右下

                    HPEN hNullPen = (HPEN)GetStockObject(NULL_PEN);
                    HPEN oldPen = SelectObject(hdc, hNullPen);
                    HBRUSH cpuBrush;
                    if (darkMode)
                        cpuBrush = CreateSolidBrush(RGB(80,100,150));  // darkMode: 少し暗い青
                    else
                        cpuBrush = CreateSolidBrush(RGB(200,220,255));   // lightMode: 薄い青
                    HBRUSH oldBrush = SelectObject(hdc, cpuBrush);
                    Polygon(hdc, cpuPoints, HISTORY + 2);
                    SelectObject(hdc, oldPen);
                    SelectObject(hdc, oldBrush);
                    DeleteObject(cpuBrush);
                }
            } else {
                // 通常の順序（CPU塗りつぶし→メモリ塗りつぶし）
                {
                    POINT cpuPoints[HISTORY + 2];
                    cpuPoints[0].x = margin;
                    cpuPoints[0].y = margin + graphH;
                    for (int i = 0; i < HISTORY; i++) {
                        int x = margin + i * graphW / HISTORY;
                        int y = margin + graphH - cpuHistory[i] * graphH / 100;
                        cpuPoints[i + 1].x = x;
                        cpuPoints[i + 1].y = y;
                    }
                    cpuPoints[HISTORY + 1].x = margin + graphW;
                    cpuPoints[HISTORY + 1].y = margin + graphH;

                    HPEN hNullPen = (HPEN)GetStockObject(NULL_PEN);
                    HPEN oldPen = SelectObject(hdc, hNullPen);
                    HBRUSH cpuBrush;
                    if (darkMode)
                        cpuBrush = CreateSolidBrush(RGB(80,100,150));
                    else
                        cpuBrush = CreateSolidBrush(RGB(200,220,255));
                    HBRUSH oldBrush = SelectObject(hdc, cpuBrush);
                    Polygon(hdc, cpuPoints, HISTORY + 2);
                    SelectObject(hdc, oldPen);
                    SelectObject(hdc, oldBrush);
                    DeleteObject(cpuBrush);
                }
                {
                    POINT memPoints[HISTORY + 2];
                    memPoints[0].x = margin;
                    memPoints[0].y = margin + graphH;
                    for (int i = 0; i < HISTORY; i++) {
                        int x = margin + i * graphW / HISTORY;
                        int y = margin + graphH - memHistory[i] * graphH / 100;
                        memPoints[i + 1].x = x;
                        memPoints[i + 1].y = y;
                    }
                    memPoints[HISTORY + 1].x = margin + graphW;
                    memPoints[HISTORY + 1].y = margin + graphH;

                    HPEN hNullPen = (HPEN)GetStockObject(NULL_PEN);
                    HPEN oldPen = SelectObject(hdc, hNullPen);
                    HBRUSH memBrush;
                    if (darkMode)
                        memBrush = CreateSolidBrush(RGB(80,120,80));
                    else
                        memBrush = CreateSolidBrush(RGB(200,255,200));
                    HBRUSH oldBrush = SelectObject(hdc, memBrush);
                    Polygon(hdc, memPoints, HISTORY + 2);
                    SelectObject(hdc, oldPen);
                    SelectObject(hdc, oldBrush);
                    DeleteObject(memBrush);
                }
            }

            // CPU折れ線（青）
            {
                HPEN cpuPen = CreatePen(PS_SOLID, 2, RGB(50,150,250));
                HPEN oldPen = SelectObject(hdc, cpuPen);
                MoveToEx(hdc, margin, margin + graphH - cpuHistory[0] * graphH / 100, NULL);
                for (int i = 1; i < HISTORY; i++) {
                    int x = margin + i * graphW / HISTORY;
                    int y = margin + graphH - cpuHistory[i] * graphH / 100;
                    LineTo(hdc, x, y);
                }
                SelectObject(hdc, oldPen);
                DeleteObject(cpuPen);
            }

            // メモリ折れ線（緑）
            {
                HPEN memPen = CreatePen(PS_SOLID, 2, RGB(100,200,100));
                HPEN oldPen = SelectObject(hdc, memPen);
                MoveToEx(hdc, margin, margin + graphH - memHistory[0] * graphH / 100, NULL);
                for (int i = 1; i < HISTORY; i++) {
                    int x = margin + i * graphW / HISTORY;
                    int y = margin + graphH - memHistory[i] * graphH / 100;
                    LineTo(hdc, x, y);
                }
                SelectObject(hdc, oldPen);
                DeleteObject(memPen);
            }

            // ラベル（小さいフォント）および最新値などの描画
            SetTextColor(hdc, darkMode ? RGB(255,255,255) : RGB(0,0,0));
            SetBkMode(hdc, TRANSPARENT);
            {
                wchar_t buf[64];
                swprintf(buf, 64, L"CPU: %d%%   MEM: %d%%  ［Ｄキーでダークモード切替］［ＬキーでログのＯＮ・ＯＦＦ］",
                         cpuHistory[HISTORY - 1], memHistory[HISTORY - 1]);
                TextOutW(hdc, margin, 10, buf, wcslen(buf));
            }

            // グラフ右端に最新のCPU数値を大きめフォントで表示（CPU % と Memory GB）
            {
                HFONT hBigFont = CreateFontW(24, 0, 0, 0, FW_BOLD, 0, 0, 0, ANSI_CHARSET,
                                              OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY,
                                              DEFAULT_PITCH, L"Segoe UI");
                HFONT oldFont = SelectObject(hdc, hBigFont);
                wchar_t latestText[32];
                wsprintfW(latestText, L"CPU %d%%", cpuHistory[HISTORY - 1]);

                SIZE textSize;
                GetTextExtentPoint32W(hdc, latestText, wcslen(latestText), &textSize);
                int xPos = margin + graphW - textSize.cx - 75;
                TextOutW(hdc, xPos, margin, latestText, wcslen(latestText));

                // メモリ使用量を GB 表示
                {
                    MEMORYSTATUSEX memInfo = {0};
                    memInfo.dwLength = sizeof(memInfo);
                    GlobalMemoryStatusEx(&memInfo);
                    double usedGB = (memInfo.ullTotalPhys - memInfo.ullAvailPhys) / (1024.0 * 1024.0 * 1024.0);
                    wchar_t memText[32];
                    swprintf(memText, 32, L"Memory %.1fGB", usedGB);

                    SIZE memTextSize;
                    GetTextExtentPoint32W(hdc, memText, wcslen(memText), &memTextSize);
                    int yPos = margin + textSize.cy + 5;
                    TextOutW(hdc, xPos, yPos, memText, wcslen(memText));
                }
                SelectObject(hdc, oldFont);
                DeleteObject(hBigFont);
            }

            // ログ状態の簡単な表示（画面下部に表示）
            {
                const wchar_t *logStatus = loggingActive ? L"LOG: ON" : L"LOG: OFF";
                COLORREF logColor = loggingActive ? RGB(0,200,0) : RGB(200,0,0);
                COLORREF prevColor = SetTextColor(hdc, logColor);
                // 画面下部の余白分（必要に応じて位置調整）
                TextOutW(hdc, margin, height - 30, logStatus, wcslen(logStatus));
                SetTextColor(hdc, prevColor);
            }

            EndPaint(hwnd, &ps);
        }
        break;

        case WM_DESTROY:
            DeleteObject(hFont);
            KillTimer(hwnd, 1);
            // ログ記録中なら停止
            KillTimer(hwnd, 2);
            PostQuitMessage(0);
            break;
    }
    return DefWindowProc(hwnd, msg, wParam, lParam);
}

int WINAPI WinMain(HINSTANCE hInst, HINSTANCE hPrev, LPSTR lpCmd, int nShow) {
    // 外部ファイルからアイコンを読み込む（32x32 のサイズ）
    HICON hIcon = (HICON)LoadImage(NULL, L"ICON.ico", IMAGE_ICON, 32, 32, LR_LOADFROMFILE | LR_DEFAULTCOLOR);
    if (!hIcon) {
        MessageBox(NULL, L"アイコンの読み込みに失敗しました", L"エラー", MB_OK);
    }
    
    WNDCLASSEX wc = {0};
    wc.cbSize        = sizeof(WNDCLASSEX);
    wc.lpfnWndProc   = WndProc;
    wc.hInstance     = hInst;
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW+1);
    wc.lpszClassName = L"SysMonGraph";
    wc.hIcon         = hIcon;
    wc.hIconSm       = hIcon;
    RegisterClassEx(&wc);

    HWND hwnd = CreateWindowEx(
      0,
      L"SysMonGraph",
      L"システムモニター (折れ線+ダークモード+ログ)",
      WS_OVERLAPPEDWINDOW,
      200, 200, 800, 500,
      NULL, NULL, hInst, NULL);

    ShowWindow(hwnd, nShow);
    UpdateWindow(hwnd);

    MSG msg;
    while(GetMessage(&msg, NULL, 0, 0)) {
      TranslateMessage(&msg);
      DispatchMessage(&msg);
    }
    return msg.wParam;
}
