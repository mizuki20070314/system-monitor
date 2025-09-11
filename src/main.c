#pragma comment(lib, "winhttp.lib")
#define UNICODE
#define _UNICODE
#include <windows.h>
#include <windowsx.h>
#include <winhttp.h>
#include <stdio.h>
#include <wchar.h>
#include <time.h>
#include <stdlib.h>
#include <string.h>

#define HISTORY 200  // 履歴点数

// API サーバー設定
#define SERVER L"34.83.170.223"
#define SERVER_PORT 5000
#define GET_RESOURCE L"/resource?user="
#define POST_RESOURCE L"/update"

// グローバル変数
int cpuHistory[HISTORY] = {0};
int memHistory[HISTORY] = {0};
HFONT hFont;
BOOL darkMode = FALSE;
BOOL loggingActive = FALSE;  // ログ記録の実行状態

// モード切替用
enum MonitorMode { MODE_LOCAL, MODE_HOST, MODE_CLIENT };
enum MonitorMode currentMode = MODE_LOCAL;

// ユーザー名（起動時に入力）
wchar_t g_username[64] = L"";

// CPU 時間保持構造体
typedef struct {
    ULONGLONG idle, kernel, user;
} CPU_TIMES;
CPU_TIMES lastTimes = {0};

//------------------------------------------------------------------------------
// モーダルな名前入力ウィンドウ用（リソース不要）
// コントロールID
#define IDC_NAMEEDIT   1001
#define IDC_OKBUTTON   1002
#define IDC_CANCELBUTTON 1003

// モーダル状態管理用グローバル
BOOL g_modalDone = FALSE;
BOOL g_modalResult = FALSE; // TRUE: OK, FALSE: Cancel

LRESULT CALLBACK NameInputWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    static HWND hEdit;
    switch(msg) {
        case WM_CREATE: {
            CreateWindowEx(0, L"STATIC", L"名前を入力してください", 
                           WS_VISIBLE | WS_CHILD, 10, 10, 280, 20, hWnd, NULL, 
                           ((LPCREATESTRUCT)lParam)->hInstance, NULL);
            hEdit = CreateWindowEx(WS_EX_CLIENTEDGE, L"EDIT", L"",
                           WS_VISIBLE | WS_CHILD | ES_AUTOHSCROLL, 
                           10, 40, 280, 25, hWnd, (HMENU)IDC_NAMEEDIT,
                           ((LPCREATESTRUCT)lParam)->hInstance, NULL);
            CreateWindowEx(0, L"BUTTON", L"OK", 
                           WS_VISIBLE | WS_CHILD | BS_DEFPUSHBUTTON, 
                           60, 80, 80, 25, hWnd, (HMENU)IDC_OKBUTTON,
                           ((LPCREATESTRUCT)lParam)->hInstance, NULL);
            CreateWindowEx(0, L"BUTTON", L"Cancel", 
                           WS_VISIBLE | WS_CHILD, 160, 80, 80, 25, hWnd, 
                           (HMENU)IDC_CANCELBUTTON, ((LPCREATESTRUCT)lParam)->hInstance, NULL);
        } break;
        case WM_COMMAND:
            if(LOWORD(wParam) == IDC_OKBUTTON) {
                GetWindowTextW(hEdit, g_username, 64);
                g_modalResult = TRUE;
                DestroyWindow(hWnd);
            } else if(LOWORD(wParam) == IDC_CANCELBUTTON) {
                wcscpy(g_username, L"default");
                g_modalResult = FALSE;
                DestroyWindow(hWnd);
            }
            break;
        case WM_CLOSE:
            DestroyWindow(hWnd);
            break;
        default:
            return DefWindowProc(hWnd, msg, wParam, lParam);
    }
    return 0;
}

// モーダルな名前入力ウィンドウを表示する関数
BOOL runModalNameInput(HINSTANCE hInst) {
    WNDCLASSEX wc = {0};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = NameInputWndProc;
    wc.hInstance = hInst;
    wc.lpszClassName = L"NameInputWindow";
    RegisterClassEx(&wc);

    HWND hWnd = CreateWindowEx(0, wc.lpszClassName, L"名前入力",
                               WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU,
                               CW_USEDEFAULT, CW_USEDEFAULT, 300, 150,
                               NULL, NULL, hInst, NULL);
    if(!hWnd)
        return FALSE;
    ShowWindow(hWnd, SW_SHOW);
    UpdateWindow(hWnd);

    MSG msg;
    // モーダルループ: ウィンドウが存在している間、PeekMessage でメッセージ処理
    while(IsWindow(hWnd)) {
         while(PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
             TranslateMessage(&msg);
             DispatchMessage(&msg);
         }
         Sleep(10);
    }
    return g_modalResult;
}

//------------------------------------------------------------------------------
// HTTP GET リクエストを実施し、レスポンスを responseBuffer に出力
BOOL HttpGet(const wchar_t* server, int port, const wchar_t* resource, char* responseBuffer, DWORD bufferSize) {
    BOOL bResult = FALSE;
    HINTERNET hSession = WinHttpOpen(L"SysMonGraphClient/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                     WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if(hSession) {
        HINTERNET hConnect = WinHttpConnect(hSession, server, port, 0);
        if(hConnect) {
            HINTERNET hRequest = WinHttpOpenRequest(hConnect, L"GET", resource,
                                                    NULL, WINHTTP_NO_REFERER,
                                                    WINHTTP_DEFAULT_ACCEPT_TYPES, 0);
            if(hRequest) {
                if(WinHttpSendRequest(hRequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                                      WINHTTP_NO_REQUEST_DATA, 0, 0, 0)) {
                    if(WinHttpReceiveResponse(hRequest, NULL)) {
                        DWORD dwSize = 0, dwDownloaded = 0;
                        responseBuffer[0] = '\0';
                        do {
                            if(!WinHttpQueryDataAvailable(hRequest, &dwSize))
                                break;
                            if(dwSize == 0)
                                break;
                            char* buffer = (char*)malloc(dwSize+1);
                            if(buffer) {
                                ZeroMemory(buffer, dwSize+1);
                                if(WinHttpReadData(hRequest, (LPVOID)buffer, dwSize, &dwDownloaded)) {
                                    strncat(responseBuffer, buffer, bufferSize - strlen(responseBuffer) - 1);
                                    bResult = TRUE;
                                }
                                free(buffer);
                            }
                        } while(dwSize > 0);
                    }
                }
                WinHttpCloseHandle(hRequest);
            }
            WinHttpCloseHandle(hConnect);
        }
        WinHttpCloseHandle(hSession);
    }
    return bResult;
}

// HTTP POST リクエストを実施（data は送信するマルチバイト文字列）
BOOL HttpPost(const wchar_t* server, int port, const wchar_t* resource, const char* data) {
    BOOL bResult = FALSE;
    HINTERNET hSession = WinHttpOpen(L"SysMonGraphClient/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                     WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if(hSession) {
        HINTERNET hConnect = WinHttpConnect(hSession, server, port, 0);
        if(hConnect) {
            HINTERNET hRequest = WinHttpOpenRequest(hConnect, L"POST", resource,
                                                    NULL, WINHTTP_NO_REFERER,
                                                    WINHTTP_DEFAULT_ACCEPT_TYPES, 0);
            if(hRequest) {
                DWORD dataLength = (DWORD)strlen(data);
                if(WinHttpSendRequest(hRequest, L"Content-Type: text/plain\r\n", -1L,
                                      (LPVOID)data, dataLength, dataLength, 0)) {
                    if(WinHttpReceiveResponse(hRequest, NULL))
                        bResult = TRUE;
                }
                WinHttpCloseHandle(hRequest);
            }
            WinHttpCloseHandle(hConnect);
        }
        WinHttpCloseHandle(hSession);
    }
    return bResult;
}

//------------------------------------------------------------------------------
// CPU 使用率取得
int GetCPUUsage() {
    FILETIME idle, kernel, user;
    if (!GetSystemTimes(&idle, &kernel, &user))
        return -1;
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

//------------------------------------------------------------------------------
// メモリ使用率取得
int GetMemoryUsage() {
    MEMORYSTATUSEX mem = {0};
    mem.dwLength = sizeof(mem);
    GlobalMemoryStatusEx(&mem);
    return (int)mem.dwMemoryLoad;
}

void AddHistory(int *history, int value) {
    for (int i = 0; i < HISTORY - 1; i++) {
        history[i] = history[i + 1];
    }
    history[HISTORY - 1] = value;
}

//------------------------------------------------------------------------------
// メインウィンドウ用ウィンドウプロシージャ（既存コード）
LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    static int width = 600, height = 400;
    switch (msg) {
        case WM_CREATE:
            hFont = CreateFontW(18, 0, 0, 0, FW_NORMAL, 0, 0, 0, ANSI_CHARSET,
                                OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY,
                                DEFAULT_PITCH, L"Segoe UI");
            SetTimer(hwnd, 1, 1000, NULL); // 1秒ごと更新
            break;
        case WM_SIZE:
            width = LOWORD(lParam);
            height = HIWORD(lParam);
            break;
        case WM_KEYDOWN:
            if (wParam == 'D') { // ダークモード切替
                darkMode = !darkMode;
                InvalidateRect(hwnd, NULL, TRUE);
            } else if (wParam == 'L') { // ログ記録ON/OFF
                loggingActive = !loggingActive;
                if (loggingActive) {
                    SetTimer(hwnd, 3, 1000, NULL);
                } else {
                    KillTimer(hwnd, 3);
                }
            } else if (wParam == 'R') { // ローカルモード
                currentMode = MODE_LOCAL;
                InvalidateRect(hwnd, NULL, TRUE);
            } else if (wParam == 'H') { // ホストモード
                currentMode = MODE_HOST;
                InvalidateRect(hwnd, NULL, TRUE);
            } else if (wParam == 'C') { // クライアントモード
                currentMode = MODE_CLIENT;
                InvalidateRect(hwnd, NULL, TRUE);
            }
            break;
        case WM_TIMER: {
                if (wParam == 1) {
                    int cpu = 0, mem = 0;
                    switch (currentMode) {
                        case MODE_LOCAL:
                        case MODE_HOST:
                            cpu = GetCPUUsage();
                            mem = GetMemoryUsage();
                            break;
                        case MODE_CLIENT: {
                                char response[256] = "";
                                wchar_t resourceWithUser[256];
                                swprintf(resourceWithUser, 256, L"/resource?user=%ls", g_username);
                                if (HttpGet(SERVER, SERVER_PORT, resourceWithUser, response, sizeof(response))) {
                                    double usedGB;
                                    sscanf(response, "%d,%d,%lf", &cpu, &mem, &usedGB);
                                    // 必要に応じて usedGB を利用する
                                }
                            }
                            break;
                    }
                    AddHistory(cpuHistory, cpu);
                    AddHistory(memHistory, mem);
                    InvalidateRect(hwnd, NULL, TRUE);
                    if (currentMode == MODE_HOST) {
                        char postData[128];
                        char usernameA[64] = {0};
                        wcstombs(usernameA, g_username, sizeof(usernameA));
                        
                        // 使用中メモリ(GB)の計算
                        MEMORYSTATUSEX memInfo = {0};
                        memInfo.dwLength = sizeof(memInfo);
                        GlobalMemoryStatusEx(&memInfo);
                        double usedGB = (memInfo.ullTotalPhys - memInfo.ullAvailPhys) / (1024.0 * 1024.0 * 1024.0);
                        
                        // CSV形式：ユーザー名、CPU使用率、メモリ使用率（%）、使用中メモリ(GB)
                        sprintf(postData, "%s,%d,%d,%.2f", usernameA, cpu, mem, usedGB);
                        HttpPost(SERVER, SERVER_PORT, POST_RESOURCE, postData);
                    }
                }
                else if (wParam == 3) {
                    FILE* fp = fopen("log.csv", "a");
                    if (fp != NULL) {
                        time_t now = time(NULL);
                        struct tm local;
                        localtime_r(&now, &local);
                        char timeStr[64];
                        strftime(timeStr, sizeof(timeStr), "%Y-%m-%d %H:%M:%S", &local);
                        int cpu = GetCPUUsage();
                        int mem = GetMemoryUsage();
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
            HBRUSH bg = CreateSolidBrush(darkMode ? RGB(20,20,20) : RGB(245,245,245));
            FillRect(hdc, &rect, bg);
            DeleteObject(bg);
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
            if (cpuHistory[HISTORY-1] < memHistory[HISTORY-1]) {
                POINT memPoints[HISTORY+2];
                memPoints[0].x = margin;
                memPoints[0].y = margin+graphH;
                for (int i = 0; i < HISTORY; i++) {
                    int x = margin + i * graphW / HISTORY;
                    int y = margin + graphH - memHistory[i] * graphH / 100;
                    memPoints[i+1].x = x;
                    memPoints[i+1].y = y;
                }
                memPoints[HISTORY+1].x = margin + graphW;
                memPoints[HISTORY+1].y = margin+graphH;
                HPEN hNullPen = (HPEN)GetStockObject(NULL_PEN);
                HPEN oldPen = SelectObject(hdc, hNullPen);
                HBRUSH memBrush = CreateSolidBrush(darkMode ? RGB(80,120,80) : RGB(200,255,200));
                HBRUSH oldBrush = SelectObject(hdc, memBrush);
                Polygon(hdc, memPoints, HISTORY+2);
                SelectObject(hdc, oldPen);
                SelectObject(hdc, oldBrush);
                DeleteObject(memBrush);
                POINT cpuPoints[HISTORY+2];
                cpuPoints[0].x = margin;
                cpuPoints[0].y = margin+graphH;
                for (int i = 0; i < HISTORY; i++) {
                    int x = margin + i * graphW / HISTORY;
                    int y = margin + graphH - cpuHistory[i] * graphH / 100;
                    cpuPoints[i+1].x = x;
                    cpuPoints[i+1].y = y;
                }
                cpuPoints[HISTORY+1].x = margin + graphW;
                cpuPoints[HISTORY+1].y = margin+graphH;
                hNullPen = (HPEN)GetStockObject(NULL_PEN);
                oldPen = SelectObject(hdc, hNullPen);
                HBRUSH cpuBrush = CreateSolidBrush(darkMode ? RGB(80,100,150) : RGB(200,220,255));
                oldBrush = SelectObject(hdc, cpuBrush);
                Polygon(hdc, cpuPoints, HISTORY+2);
                SelectObject(hdc, oldPen);
                SelectObject(hdc, oldBrush);
                DeleteObject(cpuBrush);
            } else {
                POINT cpuPoints[HISTORY+2];
                cpuPoints[0].x = margin;
                cpuPoints[0].y = margin+graphH;
                for (int i = 0; i < HISTORY; i++) {
                    int x = margin + i * graphW / HISTORY;
                    int y = margin + graphH - cpuHistory[i] * graphH / 100;
                    cpuPoints[i+1].x = x;
                    cpuPoints[i+1].y = y;
                }
                cpuPoints[HISTORY+1].x = margin + graphW;
                cpuPoints[HISTORY+1].y = margin+graphH;
                HPEN hNullPen = (HPEN)GetStockObject(NULL_PEN);
                HPEN oldPen = SelectObject(hdc, hNullPen);
                HBRUSH cpuBrush = CreateSolidBrush(darkMode ? RGB(80,100,150) : RGB(200,220,255));
                HBRUSH oldBrush = SelectObject(hdc, cpuBrush);
                Polygon(hdc, cpuPoints, HISTORY+2);
                SelectObject(hdc, oldPen);
                SelectObject(hdc, oldBrush);
                DeleteObject(cpuBrush);
                POINT memPoints[HISTORY+2];
                memPoints[0].x = margin;
                memPoints[0].y = margin+graphH;
                for (int i = 0; i < HISTORY; i++) {
                    int x = margin + i * graphW / HISTORY;
                    int y = margin + graphH - memHistory[i] * graphH / 100;
                    memPoints[i+1].x = x;
                    memPoints[i+1].y = y;
                }
                memPoints[HISTORY+1].x = margin + graphW;
                memPoints[HISTORY+1].y = margin+graphH;
                hNullPen = (HPEN)GetStockObject(NULL_PEN);
                oldPen = SelectObject(hdc, hNullPen);
                HBRUSH memBrush = CreateSolidBrush(darkMode ? RGB(80,120,80) : RGB(200,255,200));
                oldBrush = SelectObject(hdc, memBrush);
                Polygon(hdc, memPoints, HISTORY+2);
                SelectObject(hdc, oldPen);
                SelectObject(hdc, oldBrush);
                DeleteObject(memBrush);
            }
            {   // CPU 折れ線描画
                HPEN cpuPen = CreatePen(PS_SOLID, 2, RGB(50,150,250));
                HPEN oldPen = SelectObject(hdc, cpuPen);
                MoveToEx(hdc, margin, margin+graphH-cpuHistory[0]*graphH/100, NULL);
                for (int i = 1; i < HISTORY; i++) {
                    int x = margin + i * graphW / HISTORY;
                    int y = margin+graphH-cpuHistory[i]*graphH/100;
                    LineTo(hdc, x, y);
                }
                SelectObject(hdc, oldPen);
                DeleteObject(cpuPen);
            }
            {   // メモリ折れ線描画
                HPEN memPen = CreatePen(PS_SOLID, 2, RGB(100,200,100));
                HPEN oldPen = SelectObject(hdc, memPen);
                MoveToEx(hdc, margin, margin+graphH-memHistory[0]*graphH/100, NULL);
                for (int i = 1; i < HISTORY; i++) {
                    int x = margin + i * graphW / HISTORY;
                    int y = margin+graphH-memHistory[i]*graphH/100;
                    LineTo(hdc, x, y);
                }
                SelectObject(hdc, oldPen);
                DeleteObject(memPen);
            }
            {   // ラベルと状態情報描画
                SetTextColor(hdc, darkMode ? RGB(255,255,255) : RGB(0,0,0));
                SetBkMode(hdc, TRANSPARENT);
                wchar_t buf[128];
                swprintf(buf, 128,
                    L"CPU: %d%%   MEM: %d%%  ［Ｄ: ダークモード］［Ｌ: ログのＯＮ・ＯＦＦ］［Ｒ: このマシン　Ｈ: ホスト　Ｃ: クライアント］  User: %ls",
                    cpuHistory[HISTORY-1], memHistory[HISTORY-1], g_username);
                TextOutW(hdc, margin, 10, buf, wcslen(buf));
            }
            {   // 画面下部 モード・ログ状態表示
                wchar_t modeStr[64];
                switch (currentMode) {
                    case MODE_LOCAL:
                        wcscpy(modeStr, L"MODE: LOCAL");
                        break;
                    case MODE_HOST:
                        wcscpy(modeStr, L"MODE: HOST");
                        break;
                    case MODE_CLIENT:
                        wcscpy(modeStr, L"MODE: CLIENT");
                        break;
                }
                TextOutW(hdc, width-margin-150, height-30, modeStr, wcslen(modeStr));
                const wchar_t *logStatus = loggingActive ? L"LOG: ON" : L"LOG: OFF";
                COLORREF logColor = loggingActive ? RGB(0,200,0) : RGB(200,0,0);
                COLORREF prevColor = SetTextColor(hdc, logColor);
                TextOutW(hdc, margin, height-30, logStatus, wcslen(logStatus));
                SetTextColor(hdc, prevColor);
            }
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
            EndPaint(hwnd, &ps);
        }
        break;
        case WM_DESTROY:
            DeleteObject(hFont);
            KillTimer(hwnd, 1);
            KillTimer(hwnd, 3);
            PostQuitMessage(0);
            break;
    }
    return DefWindowProc(hwnd, msg, wParam, lParam);
}

//------------------------------------------------------------------------------
// WinMain
int WINAPI WinMain(HINSTANCE hInst, HINSTANCE hPrev, LPSTR lpCmd, int nShow) {
    // まず、モーダルな名前入力ウィンドウを表示する
    if(!runModalNameInput(hInst)) {
        // Cancel時は default を設定
        wcscpy(g_username, L"default");
    }
    
    // メインウィンドウ用クラスを登録
    HICON hIcon = (HICON)LoadImage(NULL, L"ICON.ico", IMAGE_ICON, 32, 32, LR_LOADFROMFILE | LR_DEFAULTCOLOR);
    if (!hIcon) {
        MessageBox(NULL, L"アイコンの読み込みに失敗しました", L"エラー", MB_OK);
    }
    WNDCLASSEX wc = {0};
    wc.cbSize = sizeof(WNDCLASSEX);
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInst;
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW+1);
    wc.lpszClassName = L"SysMonGraph";
    wc.hIcon = hIcon;
    wc.hIconSm = hIcon;
    RegisterClassEx(&wc);
    HWND hwnd = CreateWindowEx(
      0,
      L"SysMonGraph",
      L"システムモニター (折れ線+ダークモード+ログ+ホスト/クライアント)",
      WS_OVERLAPPEDWINDOW,
      200, 200, 1000, 700,
      NULL, NULL, hInst, NULL);
    ShowWindow(hwnd, nShow);
    UpdateWindow(hwnd);
    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    return msg.wParam;
}
