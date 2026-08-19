// ============================================================================
//  ValMods - petit gestionnaire manuel de mods Valheim (Win32 natif, C++)
//  - vue en cartes (pas un tableau) : icone visible, nom, petits details
//    (categorie / derniere verif / etat DLL / etat Thunderstore / note),
//    et les boutons d'action directement sur chaque ligne
//  - bouton "Watch"   : ouvre le lien du mod        "Hist." : historique
//  - bouton "Check+"  : ouvre le lien + horodate     "OK"   : horodate sans ouvrir
//  - bouton "TS"      : verifie la derniere version sur Thunderstore
//  - bouton "DL"      : telecharge le zip (demande ou l'enregistrer)
//  - bouton "Modif."  : modifie le mod               "..."  : copier lien /
//    localiser le DLL / ouvrir son dossier / supprimer
//  - tri via un menu deroulant + un bouton croissant/decroissant
//  - acces rapide aux dossiers plugins / config / sauvegardes / telechargements
//  - onglet Sauvegardes : liste des mondes et des personnages + backup
//
//  Aucune dependance externe : GDI+ (icones) et Common Dialogs (parcourir)
//  sont livres avec Windows. Donnees stockees a cote de l'exe :
//      valmods.json
// ============================================================================

#define _CRT_SECURE_NO_WARNINGS
#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX            // gdiplus.h utilise std::min/max ; les macros
                             // min/max de windows.h les masqueraient sinon.
#define WINVER 0x0600        // sans ca, des macros comme SS_ENDELLIPSIS
                             // restent gardees derriere #if(WINVER >= ...)
                             // dans winuser.h et ne se declarent pas.
#define _WIN32_WINNT 0x0600

#include <windows.h>
#include <commctrl.h>
#include <commdlg.h>         // GetOpenFileNameW (parcourir un DLL / une icone)
#include <shlobj.h>
#include <shellapi.h>
#include <winver.h>          // GetFileVersionInfoW / VerQueryValueW (version DLL)
#include <winhttp.h>          // appels HTTPS vers l'API Thunderstore (verif de version)
#include <objidl.h>
#include <gdiplus.h>          // decode PNG/JPG/BMP/ICO/GIF pour les icones de mod
#include <string>
#include <vector>
#include <map>
#include <algorithm>
#include <sstream>
#include <cstdlib>
#include <ctime>

#include "minijson.h"

#ifdef _MSC_VER
#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "gdiplus.lib")
#pragma comment(lib, "version.lib")
#pragma comment(lib, "comdlg32.lib")
#pragma comment(lib, "winhttp.lib")
#pragma comment(linker, "\"/manifestdependency:type='win32' \
name='Microsoft.Windows.Common-Controls' version='6.0.0.0' \
processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")
#endif

// ---------------------------------------------------------------- identifiants
#define IDC_TAB        1000
#define IDC_BADD       1002
#define IDC_SORTCOMBO  1018
#define IDC_SORTDIR    1019
#define IDC_WORLDS     1010
#define IDC_CHARS      1011
#define IDC_LBL1       1012
#define IDC_LBL2       1013
#define IDC_BREFRESH   1014
#define IDC_BOPENSAVE  1015
#define IDC_BBACKUP    1016
#define IDC_BOPENBK    1017

// actions sur une ligne (carte) de mod : bouton dynamique = RA_BASE +
// index_ligne * RA_COUNT + action. LOWORD(wParam) de WM_COMMAND est un
// entier 16 bits (0-65535), donc jusqu'a (65535-RA_BASE)/RA_COUNT lignes
// possibles - tres largement suffisant pour une liste de mods reelle.
#define RA_BASE  5000
enum { RA_WATCH = 0, RA_HIST = 1, RA_CHECK = 2, RA_MARK = 3, RA_TS = 4,
       RA_DL = 5, RA_EDIT = 6, RA_MORE = 7, RA_COUNT = 8 };

// Un bouton de carte declenche son action via un message DIFFERE
// (PostMessage), jamais executee directement dans le gestionnaire de
// WM_COMMAND : l'action (ex: Verifie) peut appeler RefillMods(), qui detruit
// et recree TOUTES les cartes - y compris le bouton en train d'etre clique,
// dont le propre traitement de clic n'a pas fini de se derouler dans la pile
// d'appels (WM_COMMAND est relaye de facon synchrone depuis le panneau de
// cartes). Detruire une fenetre pendant qu'elle traite encore son propre
// evenement est un piege classique en Win32. Poster differe l'execution
// reelle a la prochaine iteration de la boucle de messages, une fois que le
// clic du bouton s'est completement termine et que la pile s'est deroulee.
#define WM_APP_ROWACTION (WM_APP + 1)
// actions du menu "..." (pas encodees dans l'id d'un bouton - la cible est
// g_ctxMenuModIndex, pose au moment d'ouvrir le menu)
enum { RA_MENU_COPY = 100, RA_MENU_LOCATE_DLL = 101, RA_MENU_OPEN_DLLDIR = 102, RA_MENU_DELETE = 103 };

#define IDM_OPENDATA   2001
#define IDM_EXIT       2002
#define IDM_PLUGINS    2003
#define IDM_CONFIG     2004
#define IDM_SAVES      2005
#define IDM_GAMEDIR    2006
#define IDM_SETDIR     2007
#define IDM_ABOUT      2008
#define IDM_BEPINEX    2009
#define IDM_DOWNLOADS  2010

// menu "..." (actions moins frequentes) sur une carte de mod - les actions
// courantes (Watch/Historique/Check/Verifie/TS/Telecharger/Modifier) sont
// deja des boutons directs sur la carte, inutile de les dupliquer ici.
#define IDM_CTX_LOCATE_DLL  2104
#define IDM_CTX_OPEN_DLLDIR 2105
#define IDM_CTX_COPY        2106
#define IDM_CTX_DELETE      2108

// boutons propres a la fenetre d'edition d'un mod
#define IDC_E_BROWSEDLL   3001
#define IDC_E_BROWSEICON  3002
#define IDC_E_CLEARICON   3003
#define IDC_E_AUTOFILL    3004

#define VALMODS_VERSION "1.2.0"

// "colonnes" au sens tri uniquement desormais (il n'y a plus de tableau) :
// meme enum reutilise par ModLess et par le menu deroulant de tri.
enum { COL_NAME = 0, COL_CAT = 1, COL_LASTCHECK = 2, COL_AGE = 3,
       COL_DLL = 4, COL_TSVER = 5, COL_URL = 6, COL_NOTE = 7 };

// ---------------------------------------------------------------- donnees
struct Mod {
    std::wstring name, cat, url, changelogUrl, dllPath, iconPath, tsVersion, description, last, note;
};

static HINSTANCE g_hInst = NULL;
static HWND  g_hMain = NULL, g_hTab = NULL;
static HWND  g_hCardsHost = NULL;       // panneau scrollable "cartes" (onglet Mods)
static HWND  g_hWorlds = NULL, g_hChars = NULL;
static HWND  g_hTooltip = NULL;
static HFONT g_font = NULL, g_fontBold = NULL;
static std::vector<Mod> g_mods;
static std::wstring g_valheimDir;
static int  g_sortCol = 0;
static bool g_sortAsc = true;
static bool g_lastListIsWorld = true;   // pour le bouton backup
static int  g_ctxMenuModIndex = -1;     // cible du menu "..." (voir ShowRowOverflowMenu)

static ULONG_PTR g_gdiplusToken = 0;
static HICON g_defaultIcon = NULL;
static std::map<std::wstring, HICON> g_iconCache;   // chemin icone -> icone chargee (GDI+)

// controles crees dynamiquement pour chaque carte de mod (detruits et
// recrees a chaque RefillMods). "stretch" = la largeur est recalculee a
// chaque redimensionnement/scroll pour occuper le panneau (nom/details/
// note/separateur) ; sinon la largeur reste fixe (icone, boutons).
struct CardChild { HWND hwnd; int x, y, w, h; bool stretch; };
static std::vector<CardChild> g_cardChildren;
static int g_cardTotalHeight = 0;
static int g_scrollPos = 0;

// la taille doit rester >= au nombre d'elements pousses dans WM_CREATE.
static HWND g_pageMods[16];  static int g_nMods = 0;
static HWND g_pageSaves[8];  static int g_nSaves = 0;

// ---------------------------------------------------------------- utilitaires
static std::string W2U(const std::wstring& w) {
    if (w.empty()) return std::string();
    int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), NULL, 0, NULL, NULL);
    std::string s((size_t)n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), &s[0], n, NULL, NULL);
    return s;
}
static std::wstring U2W(const std::string& s) {
    if (s.empty()) return std::wstring();
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), NULL, 0);
    std::wstring w((size_t)n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), &w[0], n);
    return w;
}
static std::wstring Trim(const std::wstring& s) {
    size_t a = s.find_first_not_of(L" \t\r\n");
    if (a == std::wstring::npos) return L"";
    size_t b = s.find_last_not_of(L" \t\r\n");
    return s.substr(a, b - a + 1);
}
static std::wstring Clean(const std::wstring& s) {   // pas de tab / retour ligne
    std::wstring r = s;
    for (size_t i = 0; i < r.size(); ++i)
        if (r[i] == L'\t' || r[i] == L'\r' || r[i] == L'\n') r[i] = L' ';
    return Trim(r);
}
static bool DirExists(const std::wstring& p) {
    if (p.empty()) return false;
    DWORD a = GetFileAttributesW(p.c_str());
    return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY);
}
static bool FileExists(const std::wstring& p) {
    if (p.empty()) return false;
    DWORD a = GetFileAttributesW(p.c_str());
    return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
}
static std::wstring ExeDir() {
    wchar_t buf[MAX_PATH];
    GetModuleFileNameW(NULL, buf, MAX_PATH);
    std::wstring s = buf;
    size_t p = s.find_last_of(L'\\');
    return (p == std::wstring::npos) ? L"." : s.substr(0, p);
}
static std::wstring GetTextOf(HWND h) {
    int n = GetWindowTextLengthW(h);
    if (n <= 0) return L"";
    std::wstring s((size_t)n + 1, L'\0');
    GetWindowTextW(h, &s[0], n + 1);
    s.resize((size_t)n);
    return s;
}
static void Info(HWND h, const wchar_t* msg) {
    MessageBoxW(h, msg, L"ValMods", MB_OK | MB_ICONINFORMATION);
}
static void MakeDirs(const std::wstring& path) {
    std::wstring cur;
    for (size_t i = 0; i < path.size(); ++i) {
        cur += path[i];
        if (path[i] == L'\\' && cur.size() > 3) CreateDirectoryW(cur.c_str(), NULL);
    }
    CreateDirectoryW(path.c_str(), NULL);
}
static void OpenFolder(HWND h, const std::wstring& path, const wchar_t* label) {
    if (!DirExists(path)) {
        std::wstring m = L"Dossier introuvable :\n";
        m += path.empty() ? L"(non defini)" : path;
        m += L"\n\n(";
        m += label;
        m += L")\n\nVerifie le chemin du jeu dans Parametres.";
        MessageBoxW(h, m.c_str(), L"ValMods", MB_OK | MB_ICONWARNING);
        return;
    }
    ShellExecuteW(NULL, L"open", path.c_str(), NULL, NULL, SW_SHOWNORMAL);
}
static void RevealFile(const std::wstring& file) {
    std::wstring args = L"/select,\"" + file + L"\"";
    ShellExecuteW(NULL, L"open", L"explorer.exe", args.c_str(), NULL, SW_SHOWNORMAL);
}
static void CopyToClipboard(HWND h, const std::wstring& txt) {
    if (!OpenClipboard(h)) return;
    EmptyClipboard();
    size_t bytes = (txt.size() + 1) * sizeof(wchar_t);
    HGLOBAL g = GlobalAlloc(GMEM_MOVEABLE, bytes);
    if (g) {
        void* p = GlobalLock(g);
        memcpy(p, txt.c_str(), bytes);
        GlobalUnlock(g);
        SetClipboardData(CF_UNICODETEXT, g);
    }
    CloseClipboard();
}
static std::wstring ClipboardText() {
    std::wstring r;
    if (!OpenClipboard(NULL)) return r;
    HANDLE h = GetClipboardData(CF_UNICODETEXT);
    if (h) {
        wchar_t* p = (wchar_t*)GlobalLock(h);
        if (p) { r = p; GlobalUnlock(h); }
    }
    CloseClipboard();
    return Clean(r);
}

// ---------------------------------------------------------------- icones (GDI+)
// Chaque mod peut avoir un petit logo, charge depuis un fichier local
// (PNG/JPG/BMP/ICO/GIF) via GDI+. GDI+ est livre avec Windows depuis XP :
// aucune dependance ajoutee, et il decode bien plus de formats que LoadImage
// (qui ne sait lire nativement que BMP/ICO/CUR).
//
// On NE PASSE PAS par Gdiplus::Bitmap::GetHICON() : cette fonction est connue
// pour produire des icones noires ou totalement invisibles des qu'un canal
// alpha est utilise, sur pas mal de configurations Windows (probleme GDI+
// documente, pas specifique a ce code). A la place, AlphaIconBuilder dessine
// dans un DIB section 32bpp premultiplie qu'on connait a l'avance, puis
// construit l'icone a la main via CreateIconIndirect - la methode fiable
// pour une icone avec vraie transparence.
struct AlphaIconBuilder {
    HBITMAP hbm;
    void* bits;
    Gdiplus::Bitmap* gdiBmp;
    int size;
    explicit AlphaIconBuilder(int sz) : hbm(NULL), bits(NULL), gdiBmp(NULL), size(sz) {
        BITMAPV5HEADER bi; memset(&bi, 0, sizeof(bi));
        bi.bV5Size = sizeof(BITMAPV5HEADER);
        bi.bV5Width = size;
        bi.bV5Height = -size;            // negatif = top-down, evite tout flip manuel
        bi.bV5Planes = 1;
        bi.bV5BitCount = 32;
        bi.bV5Compression = BI_BITFIELDS;
        bi.bV5RedMask   = 0x00FF0000;
        bi.bV5GreenMask = 0x0000FF00;
        bi.bV5BlueMask  = 0x000000FF;
        bi.bV5AlphaMask = 0xFF000000;
        HDC screenDc = GetDC(NULL);
        hbm = CreateDIBSection(screenDc, (BITMAPINFO*)&bi, DIB_RGB_COLORS, &bits, NULL, 0);
        ReleaseDC(NULL, screenDc);
        if (hbm && bits) {
            memset(bits, 0, (size_t)size * (size_t)size * 4);   // transparent au depart
            // PixelFormat32bppPARGB (alpha premultipliee) : le format attendu
            // par un HBITMAP consomme ensuite comme icone a canal alpha.
            gdiBmp = new Gdiplus::Bitmap(size, size, size * 4, PixelFormat32bppPARGB, (BYTE*)bits);
        }
    }
    ~AlphaIconBuilder() {
        delete gdiBmp;
        if (hbm) DeleteObject(hbm);
    }
    bool ok() const { return hbm && bits && gdiBmp && gdiBmp->GetLastStatus() == Gdiplus::Ok; }
    // hbmColor est REUTILISE tel quel par CreateIconIndirect (qui en fait sa
    // propre copie interne) : on peut le detruire nous-memes juste apres,
    // le destructeur s'en charge normalement.
    HICON ToIcon() {
        if (!ok()) return NULL;
        HBITMAP hMask = CreateBitmap(size, size, 1, 1, NULL);
        if (!hMask) return NULL;
        ICONINFO ii; memset(&ii, 0, sizeof(ii));
        ii.fIcon = TRUE;
        ii.hbmColor = hbm;
        ii.hbmMask = hMask;
        HICON hIcon = CreateIconIndirect(&ii);
        DeleteObject(hMask);
        return hIcon;
    }
};

static HICON MakeDefaultIcon(int size) {
    AlphaIconBuilder b(size);
    if (!b.ok()) return NULL;
    Gdiplus::Graphics g(b.gdiBmp);
    g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    Gdiplus::SolidBrush fill(Gdiplus::Color(255, 100, 100, 112));
    Gdiplus::Pen pen(Gdiplus::Color(255, 60, 60, 70), 1.0f);
    float pad = 1.0f, s = (float)size;
    g.FillRectangle(&fill, pad, pad, s - 2 * pad, s - 2 * pad);
    g.DrawRectangle(&pen, pad, pad, s - 2 * pad, s - 2 * pad);
    return b.ToIcon();
}
static HICON LoadScaledIconFromFile(const std::wstring& path, int size) {
    if (path.empty() || !FileExists(path)) return NULL;
    Gdiplus::Bitmap* src = Gdiplus::Bitmap::FromFile(path.c_str(), FALSE);
    if (!src) return NULL;
    if (src->GetLastStatus() != Gdiplus::Ok || src->GetWidth() == 0 || src->GetHeight() == 0) {
        delete src;
        return NULL;
    }
    AlphaIconBuilder b(size);
    if (!b.ok()) { delete src; return NULL; }
    Gdiplus::Graphics g(b.gdiBmp);
    g.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
    g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    float sw = (float)src->GetWidth(), sh = (float)src->GetHeight();
    float scale = (sw > sh) ? (float)size / sw : (float)size / sh;   // conserve le ratio
    float dw = sw * scale, dh = sh * scale;
    float dx = ((float)size - dw) / 2.0f, dy = ((float)size - dh) / 2.0f;
    g.DrawImage(src, dx, dy, dw, dh);
    delete src;
    return b.ToIcon();
}
// Icone (40x40, taille des cartes) pour ce chemin, chargee et mise en cache
// au premier appel ; un chemin vide ou illisible retombe sur l'icone par
// defaut PARTAGEE (g_defaultIcon) SANS la mettre en cache, pour ne jamais
// risquer de detruire deux fois le meme HICON au nettoyage final (voir
// wWinMain) - seuls les chargements reussis, chacun un HICON distinct,
// entrent dans g_iconCache. Le cache n'est pas invalide si le fichier
// change sur disque a chemin egal - re-parcourir le fichier dans l'editeur
// force un rechargement (nouveau HICON, ancien jamais libere avant la fin
// de l'appli : negligeable pour le nombre d'icones qu'un usage normal genere).
static HICON GetOrLoadHIcon(const std::wstring& path) {
    if (path.empty()) return g_defaultIcon;
    std::map<std::wstring, HICON>::iterator it = g_iconCache.find(path);
    if (it != g_iconCache.end()) return it->second;
    HICON hi = LoadScaledIconFromFile(path, 40);
    if (!hi) return g_defaultIcon;
    g_iconCache[path] = hi;
    return hi;
}

// --- lecture / ecriture de fichiers via l'API Win32 (chemins Unicode surs) ---
static bool ReadAllBytes(const std::wstring& path, std::string& out) {
    out.clear();
    HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, NULL,
                           OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) return false;
    LARGE_INTEGER sz;
    if (!GetFileSizeEx(h, &sz) || sz.QuadPart > (LONGLONG)32 * 1024 * 1024) {
        CloseHandle(h); return false;
    }
    out.resize((size_t)sz.QuadPart);
    DWORD got = 0;
    bool ok = true;
    if (sz.QuadPart > 0)
        ok = (ReadFile(h, &out[0], (DWORD)sz.QuadPart, &got, NULL) != 0) && got == sz.QuadPart;
    CloseHandle(h);
    if (!ok) out.clear();
    return ok;
}
static bool WriteAllBytes(const std::wstring& path, const std::string& data) {
    HANDLE h = CreateFileW(path.c_str(), GENERIC_WRITE, 0, NULL,
                           CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) return false;
    DWORD wrote = 0;
    bool ok = data.empty() ||
              ((WriteFile(h, data.data(), (DWORD)data.size(), &wrote, NULL) != 0)
               && wrote == data.size());
    CloseHandle(h);
    return ok;
}

// ---------------------------------------------------------------- dates
static std::wstring NowStamp() {
    time_t t = time(NULL);
    struct tm* lt = localtime(&t);
    wchar_t buf[32];
    wsprintfW(buf, L"%04d-%02d-%02d %02d:%02d",
        lt->tm_year + 1900, lt->tm_mon + 1, lt->tm_mday, lt->tm_hour, lt->tm_min);
    return buf;
}
// -1 = jamais verifie
static int DaysSince(const std::wstring& stamp) {
    if (stamp.size() < 10) return -1;
    int y = 0, mo = 0, d = 0, h = 0, mi = 0;
    if (swscanf(stamp.c_str(), L"%d-%d-%d %d:%d", &y, &mo, &d, &h, &mi) < 3) return -1;
    struct tm a; memset(&a, 0, sizeof(a));
    a.tm_year = y - 1900; a.tm_mon = mo - 1; a.tm_mday = d;
    a.tm_hour = h; a.tm_min = mi; a.tm_isdst = -1;
    time_t t0 = mktime(&a);
    if (t0 == (time_t)-1) return -1;
    double diff = difftime(time(NULL), t0);
    if (diff < 0) diff = 0;
    return (int)(diff / 86400.0);
}
static std::wstring DaysText(const std::wstring& stamp) {
    int d = DaysSince(stamp);
    if (d < 0) return L"-";
    wchar_t b[24]; wsprintfW(b, L"%d j", d);
    return b;
}
static std::wstring FileTimeText(const FILETIME& ft) {
    FILETIME lf; SYSTEMTIME st;
    FileTimeToLocalFileTime(&ft, &lf);
    FileTimeToSystemTime(&lf, &st);
    wchar_t b[40];
    wsprintfW(b, L"%04d-%02d-%02d %02d:%02d", st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute);
    return b;
}

// ---------------------------------------------------------------- chemins jeu
static std::wstring SavesRoot() {
    wchar_t p[MAX_PATH];
    if (SUCCEEDED(SHGetFolderPathW(NULL, CSIDL_PROFILE, NULL, 0, p)))
        return std::wstring(p) + L"\\AppData\\LocalLow\\IronGate\\Valheim";
    return L"";
}
static std::wstring WorldsDir() {
    std::wstring r = SavesRoot();
    if (r.empty()) return L"";
    if (DirExists(r + L"\\worlds_local")) return r + L"\\worlds_local";
    return r + L"\\worlds";
}
static std::wstring CharsDir() {
    std::wstring r = SavesRoot();
    if (r.empty()) return L"";
    if (DirExists(r + L"\\characters_local")) return r + L"\\characters_local";
    return r + L"\\characters";
}
static std::wstring PluginsDir() { return g_valheimDir.empty() ? L"" : g_valheimDir + L"\\BepInEx\\plugins"; }
static std::wstring ConfigDir()  { return g_valheimDir.empty() ? L"" : g_valheimDir + L"\\BepInEx\\config"; }
static std::wstring BepInExDir() { return g_valheimDir.empty() ? L"" : g_valheimDir + L"\\BepInEx"; }
static std::wstring BackupRoot() { return ExeDir() + L"\\backups"; }
static std::wstring DownloadsRoot() { return ExeDir() + L"\\downloads"; }

static std::wstring RegSteamPath() {
    HKEY k;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Valve\\Steam", 0, KEY_READ, &k) != ERROR_SUCCESS)
        return L"";
    wchar_t buf[MAX_PATH]; DWORD sz = sizeof(buf), type = 0;
    std::wstring res;
    if (RegQueryValueExW(k, L"SteamPath", NULL, &type, (LPBYTE)buf, &sz) == ERROR_SUCCESS && type == REG_SZ)
        res = buf;
    RegCloseKey(k);
    for (size_t i = 0; i < res.size(); ++i) if (res[i] == L'/') res[i] = L'\\';
    return res;
}
// lit libraryfolders.vdf pour trouver les bibliotheques sur d'autres disques
static void SteamLibraries(std::vector<std::wstring>& out) {
    std::wstring steam = RegSteamPath();
    if (!steam.empty()) out.push_back(steam);
    if (steam.empty()) return;
    std::wstring vdf = steam + L"\\steamapps\\libraryfolders.vdf";
    std::string content;
    if (!ReadAllBytes(vdf, content)) return;
    std::istringstream f(content);
    std::string line;
    while (std::getline(f, line)) {
        size_t k = line.find("\"path\"");
        if (k == std::string::npos) continue;
        size_t q1 = line.find('"', k + 6);
        if (q1 == std::string::npos) continue;
        size_t q2 = line.find('"', q1 + 1);
        if (q2 == std::string::npos) continue;
        std::string raw = line.substr(q1 + 1, q2 - q1 - 1);
        std::string clean;
        for (size_t i = 0; i < raw.size(); ++i) {
            if (raw[i] == '\\' && i + 1 < raw.size() && raw[i + 1] == '\\') { clean += '\\'; ++i; }
            else clean += raw[i];
        }
        out.push_back(U2W(clean));
    }
}
static std::wstring DetectValheim() {
    std::vector<std::wstring> libs;
    SteamLibraries(libs);
    for (size_t i = 0; i < libs.size(); ++i) {
        std::wstring c = libs[i] + L"\\steamapps\\common\\Valheim";
        if (DirExists(c)) return c;
    }
    const wchar_t* fixed[] = {
        L"C:\\Program Files (x86)\\Steam\\steamapps\\common\\Valheim",
        L"C:\\Program Files\\Steam\\steamapps\\common\\Valheim",
        L"D:\\Steam\\steamapps\\common\\Valheim",
        L"D:\\SteamLibrary\\steamapps\\common\\Valheim",
        L"E:\\SteamLibrary\\steamapps\\common\\Valheim"
    };
    for (int i = 0; i < 5; ++i) if (DirExists(fixed[i])) return fixed[i];
    return L"";
}

// ---------------------------------------------------------------- persistance
// Tout tient dans un seul fichier lisible/editable a la main, a cote de l'exe :
//
//   valmods.json
//   {
//     "version": 4,
//     "valheimDir": "D:\\SteamLibrary\\steamapps\\common\\Valheim",
//     "mods": [
//       { "name": "...", "category": "...", "url": "...",
//         "changelogUrl": "...", "dllPath": "...", "iconPath": "...",
//         "tsVersion": "1.3.0", "description": "...",
//         "lastCheck": "2026-08-19 14:30", "note": "..." }
//     ]
//   }
static std::wstring DataFile()   { return ExeDir() + L"\\valmods.json"; }
static std::wstring LegacyFile() { return ExeDir() + L"\\valmods.tsv"; }

static void SaveMods() {
    std::string out;
    out += "{\n";
    out += "  \"version\": 4,\n";
    out += "  \"valheimDir\": " + mj::quote(W2U(g_valheimDir)) + ",\n";
    out += "  \"mods\": [\n";
    for (size_t i = 0; i < g_mods.size(); ++i) {
        const Mod& m = g_mods[i];
        out += "    {\n";
        out += "      \"name\":         " + mj::quote(W2U(m.name))         + ",\n";
        out += "      \"category\":     " + mj::quote(W2U(m.cat))          + ",\n";
        out += "      \"url\":          " + mj::quote(W2U(m.url))          + ",\n";
        out += "      \"changelogUrl\": " + mj::quote(W2U(m.changelogUrl)) + ",\n";
        out += "      \"dllPath\":      " + mj::quote(W2U(m.dllPath))      + ",\n";
        out += "      \"iconPath\":     " + mj::quote(W2U(m.iconPath))     + ",\n";
        out += "      \"tsVersion\":    " + mj::quote(W2U(m.tsVersion))    + ",\n";
        out += "      \"description\":  " + mj::quote(W2U(m.description))  + ",\n";
        out += "      \"lastCheck\":    " + mj::quote(W2U(m.last))         + ",\n";
        out += "      \"note\":         " + mj::quote(W2U(m.note))         + "\n";
        out += (i + 1 < g_mods.size()) ? "    },\n" : "    }\n";
    }
    out += "  ]\n";
    out += "}\n";

    // ecriture atomique : on ecrit un .tmp puis on remplace, comme ca une
    // coupure en plein enregistrement ne detruit pas la liste existante.
    std::wstring tmp = DataFile() + L".tmp";
    if (!WriteAllBytes(tmp, out)) return;
    if (!MoveFileExW(tmp.c_str(), DataFile().c_str(), MOVEFILE_REPLACE_EXISTING))
        DeleteFileW(tmp.c_str());
}
static void SaveIni() { SaveMods(); }

// Import de l'ancien format TSV (migration automatique, une seule fois).
static void ImportLegacyTsv(const std::string& raw) {
    std::istringstream f(raw);
    std::string line;
    while (std::getline(f, line)) {
        if (!line.empty() && line[line.size() - 1] == '\r') line.erase(line.size() - 1);
        if (line.empty() || line[0] == '#') continue;
        std::wstring w = U2W(line);
        std::vector<std::wstring> col;
        size_t start = 0;
        for (;;) {
            size_t p = w.find(L'\t', start);
            if (p == std::wstring::npos) { col.push_back(w.substr(start)); break; }
            col.push_back(w.substr(start, p - start));
            start = p + 1;
        }
        while (col.size() < 5) col.push_back(L"");
        Mod m;
        m.name = col[0]; m.cat = col[1]; m.url = col[2]; m.last = col[3]; m.note = col[4];
        if (!m.name.empty() || !m.url.empty()) g_mods.push_back(m);
    }
}

static void LoadData() {
    g_mods.clear();
    g_valheimDir.clear();
    std::string raw;

    if (ReadAllBytes(DataFile(), raw)) {
        mj::Value root;
        if (mj::parse(raw, root) && root.type == mj::OBJ) {
            g_valheimDir = U2W(root.s("valheimDir"));
            const mj::Value* mods = root.find("mods");
            if (mods && mods->type == mj::ARR) {
                for (size_t i = 0; i < mods->arr.size(); ++i) {
                    const mj::Value& v = mods->arr[i];
                    if (v.type != mj::OBJ) continue;
                    Mod m;
                    m.name         = Clean(U2W(v.s("name")));
                    m.cat          = Clean(U2W(v.s("category")));
                    m.url          = Clean(U2W(v.s("url")));
                    m.changelogUrl = Clean(U2W(v.s("changelogUrl")));
                    m.dllPath      = Clean(U2W(v.s("dllPath")));
                    m.iconPath     = Clean(U2W(v.s("iconPath")));
                    m.tsVersion    = Clean(U2W(v.s("tsVersion")));
                    m.description  = Clean(U2W(v.s("description")));
                    m.last         = Clean(U2W(v.s("lastCheck")));
                    m.note         = Clean(U2W(v.s("note")));
                    if (!m.name.empty() || !m.url.empty()) g_mods.push_back(m);
                }
            }
        } else {
            // fichier corrompu : on le met de cote au lieu de l'ecraser
            std::wstring bad = DataFile() + L".bad";
            DeleteFileW(bad.c_str());
            MoveFileW(DataFile().c_str(), bad.c_str());
            MessageBoxW(NULL,
                L"valmods.json est illisible (JSON invalide).\n"
                L"Il a ete renomme en valmods.json.bad et la liste repart a vide.\n"
                L"Tu peux corriger le fichier a la main puis le renommer.",
                L"ValMods", MB_OK | MB_ICONWARNING);
        }
    } else if (ReadAllBytes(LegacyFile(), raw)) {
        ImportLegacyTsv(raw);      // ancienne version -> on migre vers JSON
    }

    if (!DirExists(g_valheimDir)) g_valheimDir = DetectValheim();
    SaveMods();                    // normalise / cree le fichier au premier lancement
}

// ---------------------------------------------------------------- tri
static bool ModLess(const Mod& a, const Mod& b) {
    int r = 0;
    switch (g_sortCol) {
        case COL_NAME: r = lstrcmpiW(a.name.c_str(), b.name.c_str()); break;
        case COL_CAT:  r = lstrcmpiW(a.cat.c_str(), b.cat.c_str()); break;
        case COL_LASTCHECK:
        case COL_AGE: {
            int da = DaysSince(a.last), db = DaysSince(b.last);
            if (da < 0) da = 100000;            // jamais verifie = tout en haut
            if (db < 0) db = 100000;
            r = (da > db) ? 1 : (da < db ? -1 : 0);
            if (r == 0) r = lstrcmpiW(a.name.c_str(), b.name.c_str());
            break;
        }
        case COL_DLL: r = lstrcmpiW(a.dllPath.c_str(), b.dllPath.c_str()); break;
        case COL_TSVER: r = lstrcmpiW(a.tsVersion.c_str(), b.tsVersion.c_str()); break;
        case COL_URL: r = lstrcmpiW(a.url.c_str(), b.url.c_str()); break;
        default:      r = lstrcmpiW(a.note.c_str(), b.note.c_str()); break;
    }
    return g_sortAsc ? (r < 0) : (r > 0);
}

// ---------------------------------------------------------------- DLL lie
static std::wstring DllFileName(const std::wstring& path) {
    size_t p = path.find_last_of(L"\\/");
    return (p == std::wstring::npos) ? path : path.substr(p + 1);
}
static std::wstring GetDllVersionString(const std::wstring& path) {
    DWORD handle = 0;
    DWORD sz = GetFileVersionInfoSizeW(path.c_str(), &handle);
    if (!sz) return L"";
    std::vector<BYTE> buf(sz);
    if (!GetFileVersionInfoW(path.c_str(), 0, sz, &buf[0])) return L"";
    VS_FIXEDFILEINFO* ffi = NULL;
    UINT len = 0;
    if (!VerQueryValueW(&buf[0], L"\\", (LPVOID*)&ffi, &len) || !ffi || len == 0) return L"";
    wchar_t out[64];
    wsprintfW(out, L"%u.%u.%u.%u",
        HIWORD(ffi->dwFileVersionMS), LOWORD(ffi->dwFileVersionMS),
        HIWORD(ffi->dwFileVersionLS), LOWORD(ffi->dwFileVersionLS));
    return out;
}
// missingOut, si fourni, est mis a true si un DLL est renseigne mais introuvable
static std::wstring DllStatusText(const Mod& m, bool* missingOut) {
    if (missingOut) *missingOut = false;
    if (m.dllPath.empty()) return L"-";
    std::wstring fn = DllFileName(m.dllPath);
    if (!FileExists(m.dllPath)) {
        if (missingOut) *missingOut = true;
        return L"manquant : " + fn;
    }
    std::wstring ver = GetDllVersionString(m.dllPath);
    return ver.empty() ? fn : (fn + L" (v" + ver + L")");
}

// ---------------------------------------------------------------- Thunderstore
// Verification en ligne de la derniere version publiee, via l'API publique
// de Thunderstore (aucune cle requise, en lecture seule) :
//   GET https://thunderstore.io/api/experimental/package/{namespace}/{name}/
// Ne fonctionne QUE pour les mods dont le lien pointe vers thunderstore.io ;
// Nexus/GitHub ne sont pas interroges (pas d'equivalent aussi simple et sans
// cle). L'appel est synchrone (bloque l'interface le temps de la requete,
// quelques secondes au pire vu les timeouts ci-dessous) : c'est deliberement
// simple plutot que threade, puisqu'il est declenche par un clic explicite
// sur un seul mod, jamais automatiquement au demarrage ou sur toute la liste.

// Extrait namespace/nom d'une URL de page Thunderstore, ex:
// https://thunderstore.io/c/valheim/p/Namespace/PackageName/ -> ns=Namespace, name=PackageName
// Accepte aussi les sous-domaines par communaute (.../package/Namespace/PackageName/).
static bool ParseThunderstoreUrl(const std::wstring& url, std::wstring& ns, std::wstring& name) {
    if (url.find(L"thunderstore.io") == std::wstring::npos) return false;
    size_t marker = url.find(L"/p/");
    size_t skip = 3;
    if (marker == std::wstring::npos) { marker = url.find(L"/package/"); skip = 9; }
    if (marker == std::wstring::npos) return false;
    std::wstring rest = url.substr(marker + skip);
    size_t q = rest.find_first_of(L"?#");
    if (q != std::wstring::npos) rest = rest.substr(0, q);
    std::vector<std::wstring> parts;
    size_t start = 0;
    for (size_t i = 0; i <= rest.size(); ++i) {
        if (i == rest.size() || rest[i] == L'/') {
            if (i > start) parts.push_back(rest.substr(start, i - start));
            start = i + 1;
        }
    }
    if (parts.size() < 2) return false;
    ns = parts[0];
    name = parts[1];
    return true;
}
// Compare deux numeros de version "a.b.c[...]" composant par composant
// (numeriquement) ; un composant manquant vaut 0. Suffit pour du SemVer
// simple sans suffixe, seul format accepte par Thunderstore.
static std::vector<int> ParseVersionParts(const std::wstring& v) {
    std::vector<int> parts;
    size_t start = 0;
    for (size_t i = 0; i <= v.size(); ++i) {
        if (i == v.size() || v[i] == L'.') {
            if (i > start) parts.push_back(_wtoi(v.substr(start, i - start).c_str()));
            start = i + 1;
        }
    }
    return parts;
}
static bool VersionLess(const std::wstring& a, const std::wstring& b) {
    std::vector<int> pa = ParseVersionParts(a), pb = ParseVersionParts(b);
    size_t n = (pa.size() > pb.size()) ? pa.size() : pb.size();
    for (size_t i = 0; i < n; ++i) {
        int va = (i < pa.size()) ? pa[i] : 0;
        int vb = (i < pb.size()) ? pb[i] : 0;
        if (va != vb) return va < vb;
    }
    return false;
}

// Requete HTTPS GET minimale via WinHTTP (natif Windows, aucune dependance).
// Malgre le nom historique, ceci lit des octets bruts, pas forcement de
// l'UTF-8 : reutilise a la fois pour le JSON de l'API, les icones PNG et les
// zips de mods (voir DownloadThunderstoreZip plus bas).
// maxBytes (0 = illimite) coupe le telechargement si la reponse depasse
// cette taille - garde-fou pour un zip anormalement gros plutot que de
// remplir la RAM sans limite.
// Timeouts volontairement courts (resolution/connexion 5s, envoi/reception 8s)
// pour ne pas bloquer l'interface trop longtemps en cas de reseau absent.
static bool HttpGetBytes(const std::wstring& host, const std::wstring& path,
                         std::string& outBody, DWORD& outStatus, std::wstring& errOut,
                         size_t maxBytes = 0)
{
    outBody.clear(); outStatus = 0; errOut.clear();
    HINTERNET hSession = WinHttpOpen(L"ValMods (Windows)", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
        WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!hSession) { errOut = L"Impossible d'initialiser WinHTTP."; return false; }
    WinHttpSetTimeouts(hSession, 5000, 5000, 8000, 8000);

    HINTERNET hConnect = WinHttpConnect(hSession, host.c_str(), INTERNET_DEFAULT_HTTPS_PORT, 0);
    if (!hConnect) {
        errOut = L"Connexion impossible a " + host + L" (verifie ta connexion internet).";
        WinHttpCloseHandle(hSession);
        return false;
    }

    HINTERNET hRequest = WinHttpOpenRequest(hConnect, L"GET", path.c_str(), NULL,
        WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
    if (!hRequest) {
        errOut = L"Impossible de preparer la requete.";
        WinHttpCloseHandle(hConnect); WinHttpCloseHandle(hSession);
        return false;
    }

    BOOL ok = WinHttpSendRequest(hRequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
        WINHTTP_NO_REQUEST_DATA, 0, 0, 0);
    if (ok) ok = WinHttpReceiveResponse(hRequest, NULL);
    if (!ok) {
        errOut = L"Pas de reponse du serveur (delai depasse ou pas de connexion).";
        WinHttpCloseHandle(hRequest); WinHttpCloseHandle(hConnect); WinHttpCloseHandle(hSession);
        return false;
    }

    DWORD statusSize = sizeof(outStatus);
    WinHttpQueryHeaders(hRequest, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
        WINHTTP_HEADER_NAME_BY_INDEX, &outStatus, &statusSize, WINHTTP_NO_HEADER_INDEX);

    bool tooBig = false;
    for (;;) {
        DWORD avail = 0;
        if (!WinHttpQueryDataAvailable(hRequest, &avail) || avail == 0) break;
        std::vector<char> buf(avail);
        DWORD got = 0;
        if (!WinHttpReadData(hRequest, &buf[0], avail, &got)) break;
        outBody.append(&buf[0], got);
        if (maxBytes && outBody.size() > maxBytes) { tooBig = true; break; }
    }

    WinHttpCloseHandle(hRequest);
    WinHttpCloseHandle(hConnect);
    WinHttpCloseHandle(hSession);

    if (tooBig) {
        errOut = L"Reponse trop volumineuse (limite de securite depassee).";
        outBody.clear();
        return false;
    }
    return outStatus != 0;
}
// Decoupe une URL https://host/chemin en host + chemin, pour WinHttpConnect
// (qui veut le host separement). Retourne false si ce n'est pas du https.
static bool SplitHttpsUrl(const std::wstring& url, std::wstring& host, std::wstring& path) {
    const std::wstring prefix = L"https://";
    if (url.compare(0, prefix.size(), prefix) != 0) return false;
    size_t rest = prefix.size();
    size_t slash = url.find(L'/', rest);
    if (slash == std::wstring::npos) { host = url.substr(rest); path = L"/"; return !host.empty(); }
    host = url.substr(rest, slash - rest);
    path = url.substr(slash);
    return !host.empty();
}
// Nom de fichier sur : ne garde que [A-Za-z0-9_.-], le reste devient '_'.
static std::wstring SanitizeFileName(const std::wstring& s) {
    std::wstring out;
    for (size_t i = 0; i < s.size(); ++i) {
        wchar_t c = s[i];
        bool ok = (c >= L'a' && c <= L'z') || (c >= L'A' && c <= L'Z') ||
                  (c >= L'0' && c <= L'9') || c == L'_' || c == L'-' || c == L'.';
        out += ok ? c : L'_';
    }
    return out.empty() ? L"icon" : out;
}

// Recupere et parse le JSON du package Thunderstore correspondant a l'URL
// d'un mod. Partage par CheckThunderstoreVersion (bouton "Verif. TS") et
// FetchThunderstoreAutofill (bouton "Auto-remplir") pour ne pas dupliquer
// la logique reseau + gestion d'erreurs entre les deux.
struct TsFetchResult {
    bool ok;
    bool isThunderstore;
    std::wstring error;
    mj::Value root;
    TsFetchResult() : ok(false), isThunderstore(false) {}
};
static TsFetchResult FetchThunderstorePackage(const std::wstring& modUrl) {
    TsFetchResult r;
    std::wstring ns, name;
    if (!ParseThunderstoreUrl(modUrl, ns, name)) return r;   // isThunderstore reste false
    r.isThunderstore = true;

    std::wstring path = L"/api/experimental/package/" + ns + L"/" + name + L"/";
    std::string body; DWORD status = 0; std::wstring err;
    if (!HttpGetBytes(L"thunderstore.io", path, body, status, err)) {
        r.error = err.empty() ? L"Echec de la requete." : err;
        return r;
    }
    if (status == 404) {
        r.error = L"Mod introuvable sur Thunderstore (lien casse, mod retire, ou "
                  L"nom/namespace incorrect dans l'URL).";
        return r;
    }
    if (status != 200) {
        wchar_t b[64]; wsprintfW(b, L"Thunderstore a repondu avec le code %lu.", (unsigned long)status);
        r.error = b;
        return r;
    }
    if (!mj::parse(body, r.root) || r.root.type != mj::OBJ) {
        r.error = L"Reponse Thunderstore illisible (format inattendu).";
        return r;
    }
    r.ok = true;
    return r;
}
// L'API renvoie directement la derniere version publiee dans un objet
// "latest" (confirme sur une reponse reelle de l'API) - PAS dans un tableau
// "versions" a parcourir comme on pourrait le supposer. Renvoie NULL si
// l'objet est absent ou n'a pas de version_number exploitable (mod retire,
// reponse degradee...).
static const mj::Value* FindLatestEntry(const mj::Value& root, std::wstring& outVersion) {
    outVersion.clear();
    const mj::Value* latest = root.find("latest");
    if (!latest || latest->type != mj::OBJ) return NULL;
    outVersion = U2W(latest->s("version_number"));
    if (outVersion.empty()) return NULL;
    return latest;
}

struct TsCheckResult {
    bool ok;              // requete + parsing reussis, latestVersion exploitable
    bool isThunderstore;  // le lien du mod pointait bien vers thunderstore.io
    bool deprecated;
    std::wstring latestVersion;
    std::wstring description;
    std::wstring error;
    TsCheckResult() : ok(false), isThunderstore(false), deprecated(false) {}
};

static TsCheckResult CheckThunderstoreVersion(const std::wstring& modUrl) {
    TsCheckResult r;
    TsFetchResult f = FetchThunderstorePackage(modUrl);
    r.isThunderstore = f.isThunderstore;
    if (!f.isThunderstore) return r;
    if (!f.ok) { r.error = f.error; return r; }

    std::wstring latest;
    const mj::Value* bestEntry = FindLatestEntry(f.root, latest);
    if (!bestEntry) {
        r.error = L"Numero de version introuvable dans la reponse (mod retire ou reponse degradee).";
        return r;
    }
    r.latestVersion = latest;
    r.description = Clean(U2W(bestEntry->s("description")));

    const mj::Value* dep = f.root.find("is_deprecated");
    r.deprecated = (dep && dep->type == mj::BOOL && dep->b);
    r.ok = true;
    return r;
}

// Recupere nom / categorie / lien historique / derniere version / icone
// depuis Thunderstore pour pre-remplir l'editeur de mod (bouton
// "Auto-remplir"). L'icone est telechargee et enregistree localement dans
// icons/ a cote de l'exe (GDI+ sait ensuite la charger comme n'importe quel
// autre fichier d'icone choisi a la main).
struct TsAutofillResult {
    bool ok;
    bool isThunderstore;
    std::wstring error;
    std::wstring name, category, changelogUrl, latestVersion, localIconPath, description;
    TsAutofillResult() : ok(false), isThunderstore(false) {}
};
static TsAutofillResult FetchThunderstoreAutofill(const std::wstring& modUrl) {
    TsAutofillResult r;
    TsFetchResult f = FetchThunderstorePackage(modUrl);
    r.isThunderstore = f.isThunderstore;
    if (!f.isThunderstore) return r;
    if (!f.ok) { r.error = f.error; return r; }

    r.name = Clean(U2W(f.root.s("name")));

    // "categories" n'existe pas sur cet endpoint (confirme sur une reponse
    // reelle) - ce bloc ne trouvera donc jamais rien pour l'instant, garde
    // au cas ou Thunderstore l'ajoute un jour ; ca ne casse rien si absent.
    const mj::Value* cats = f.root.find("categories");
    if (cats && cats->type == mj::ARR) {
        for (size_t i = 0; i < cats->arr.size(); ++i) {
            if (cats->arr[i].type != mj::STR) continue;
            if (!r.category.empty()) r.category += L", ";
            r.category += U2W(cats->arr[i].str);
        }
    }

    std::wstring latest;
    const mj::Value* bestEntry = FindLatestEntry(f.root, latest);
    if (!bestEntry) {
        r.error = L"Numero de version introuvable dans la reponse (mod retire ou reponse degradee).";
        return r;
    }
    r.latestVersion = latest;
    // Clean() aplatit les retours a la ligne en espaces : la description de
    // Thunderstore est souvent un paragraphe, mais chaque carte n'en montre
    // qu'une ligne tronquee de toute facon (voir RefillMods).
    r.description = Clean(U2W(bestEntry->s("description")));

    std::wstring changelog = modUrl;
    if (!changelog.empty() && changelog[changelog.size() - 1] != L'/') changelog += L'/';
    changelog += L"changelog/";
    r.changelogUrl = changelog;

    std::wstring iconUrl = U2W(bestEntry->s("icon"));
    if (!iconUrl.empty()) {
        std::wstring host, path;
        if (SplitHttpsUrl(iconUrl, host, path)) {
            std::string bytes; DWORD status = 0; std::wstring err;
            if (HttpGetBytes(host, path, bytes, status, err) && status == 200 && !bytes.empty()) {
                std::wstring ns, nm;
                ParseThunderstoreUrl(modUrl, ns, nm);   // deja valide via f.isThunderstore
                std::wstring dir = ExeDir() + L"\\icons";
                MakeDirs(dir);
                std::wstring dest = dir + L"\\" + SanitizeFileName(ns) + L"-" + SanitizeFileName(nm) + L".png";
                if (WriteAllBytes(dest, bytes)) r.localIconPath = dest;
            }
            // l'icone est un bonus : si le telechargement echoue, on continue
            // quand meme avec le reste des champs plutot que de tout faire echouer.
        }
    }

    r.ok = true;
    return r;
}

// Telecharge le zip de la version demandee via l'URL de telechargement
// officielle de Thunderstore (celle utilisee par leur propre bouton
// "Download" sur la page du mod) :
//   https://thunderstore.io/package/download/{namespace}/{name}/{version}/
// Enregistre le zip dans downloads/ a cote de l'exe. Ne l'extrait JAMAIS
// automatiquement : ValMods reste un outil manuel, l'installation dans
// BepInEx/plugins se fait a la main.
static bool DownloadThunderstoreZip(const std::wstring& ns, const std::wstring& name,
                                    const std::wstring& version, const std::wstring& destPath,
                                    std::wstring& errOut)
{
    std::wstring path = L"/package/download/" + ns + L"/" + name + L"/" + version + L"/";
    std::string bytes; DWORD status = 0;
    const size_t maxBytes = 200u * 1024u * 1024u;   // 200 Mo, garde-fou de securite
    if (!HttpGetBytes(L"thunderstore.io", path, bytes, status, errOut, maxBytes)) {
        if (errOut.empty()) errOut = L"Echec du telechargement.";
        return false;
    }
    if (status == 404) {
        errOut = L"Version introuvable sur Thunderstore (peut-etre retiree entre temps).";
        return false;
    }
    if (status != 200) {
        wchar_t b[64]; wsprintfW(b, L"Thunderstore a repondu avec le code %lu.", (unsigned long)status);
        errOut = b;
        return false;
    }
    if (bytes.empty()) { errOut = L"Fichier telecharge vide."; return false; }
    if (!WriteAllBytes(destPath, bytes)) { errOut = L"Impossible d'ecrire le fichier sur le disque."; return false; }
    return true;
}

// Texte + categorie de couleur pour l'etat Thunderstore (integre a la ligne
// de details de chaque carte) : 0 gris (inconnu),
// 1 vert (a jour), 2 rouge (mise a jour disponible).
static std::wstring TsStatusText(const Mod& m, int* colorCategory) {
    if (colorCategory) *colorCategory = 0;
    if (m.tsVersion.empty()) return L"-";
    std::wstring installed = m.dllPath.empty() ? L"" : GetDllVersionString(m.dllPath);
    if (installed.empty()) return m.tsVersion;   // derniere version connue, mais rien a comparer
    if (VersionLess(installed, m.tsVersion)) {
        if (colorCategory) *colorCategory = 2;
        return m.tsVersion + L" (maj disponible)";
    }
    if (colorCategory) *colorCategory = 1;
    return m.tsVersion + L" (a jour)";
}

// Une seule couleur "dominante" par carte plutot que par segment : plus
// simple et plus lisible qu'une phrase multicolore. Priorite : probleme
// concret (DLL manquant / mise a jour disponible) avant l'anciennete de
// la derniere verification.
static COLORREF RowStatusColor(const Mod& m) {
    bool dllMissing = false;
    DllStatusText(m, &dllMissing);
    if (dllMissing) return RGB(200, 40, 40);
    int tsCat = 0;
    TsStatusText(m, &tsCat);
    if (tsCat == 2) return RGB(200, 40, 40);
    int d = DaysSince(m.last);
    if (d < 0) return RGB(130, 130, 130);
    if (d >= 30) return RGB(200, 40, 40);
    if (d >= 14) return RGB(190, 120, 0);
    return RGB(30, 120, 50);
}
// Ligne de "petits details" affichee sous le nom du mod dans sa carte.
static std::wstring BuildDetailsLine(const Mod& m) {
    std::wstring s;
    if (!m.cat.empty()) s += m.cat + L"  |  ";
    s += L"Verif : " + (m.last.empty() ? std::wstring(L"jamais") : DaysText(m.last));
    s += L"  |  DLL : " + DllStatusText(m, NULL);
    s += L"  |  TS : " + TsStatusText(m, NULL);
    return s;
}

// definie plus bas (pres de MkButton/AddCol), mais RefillMods l'utilise des
// ici pour les info-bulles des boutons de chaque carte : declaration
// anticipee pour eviter d'imposer un ordre de sections dans le fichier.
static void AddTip(HWND ctrl, const wchar_t* text);

// ---------------------------------------------------------------- liste mods
// Vue en "cartes" (pas un tableau) : chaque mod est une ligne haute avec une
// icone visible, le nom, une ligne de petits details, une ligne de note, et
// ses propres boutons d'action - pas de toolbar partagee, pas de selection
// au sens ListView. Le panneau (g_hCardsHost, classe "ValModsCards") gere
// son propre defilement vertical (voir CardsHostProc plus bas).
static const int CARD_H = 128;

static void ClearCards() {
    for (size_t i = 0; i < g_cardChildren.size(); ++i)
        if (g_cardChildren[i].hwnd) DestroyWindow(g_cardChildren[i].hwnd);
    g_cardChildren.clear();
}
// STATIC de carte : SS_NOPREFIX est important ici (pas dans MkLabel, qui
// n'affiche que du texte fixe ecrit par nous) car le nom/la note d'un mod
// viennent de l'utilisateur ou de Thunderstore et peuvent contenir '&', que
// Windows interpreterait sinon comme un prefixe d'acceleration clavier.
static HWND MkCardStatic(const std::wstring& txt, int x, int y, int w, int h,
                         HFONT font, COLORREF color, DWORD extraStyle, bool stretch)
{
    HWND s = CreateWindowExW(0, L"STATIC", txt.c_str(),
        WS_CHILD | WS_VISIBLE | SS_NOPREFIX | extraStyle,
        x, y, w, h, g_hCardsHost, NULL, g_hInst, NULL);
    SendMessageW(s, WM_SETFONT, (WPARAM)font, TRUE);
    if (color) SetWindowLongPtrW(s, GWLP_USERDATA, (LONG_PTR)color);
    CardChild cc; cc.hwnd = s; cc.x = x; cc.y = y; cc.w = w; cc.h = h; cc.stretch = stretch;
    g_cardChildren.push_back(cc);
    return s;
}
static HWND MkCardButton(int id, const wchar_t* txt, int x, int y, int w, int h) {
    HWND b = CreateWindowExW(0, L"BUTTON", txt,
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
        x, y, w, h, g_hCardsHost, (HMENU)(INT_PTR)id, g_hInst, NULL);
    SendMessageW(b, WM_SETFONT, (WPARAM)g_font, TRUE);
    CardChild cc; cc.hwnd = b; cc.x = x; cc.y = y; cc.w = w; cc.h = h; cc.stretch = false;
    g_cardChildren.push_back(cc);
    return b;
}
// Recalcule la largeur des controles "stretch" selon la largeur actuelle du
// panneau, et repositionne tout selon g_scrollPos. Appelee au defilement ET
// au redimensionnement (bien moins couteux que reconstruire les cartes).
static void RepositionCards() {
    if (!g_hCardsHost) return;
    RECT rc; GetClientRect(g_hCardsHost, &rc);
    int hostW = rc.right;
    for (size_t i = 0; i < g_cardChildren.size(); ++i) {
        CardChild& cc = g_cardChildren[i];
        int w = cc.w;
        if (cc.stretch) {
            w = hostW - cc.x - 10;
            if (w < 40) w = 40;
        }
        MoveWindow(cc.hwnd, cc.x, cc.y - g_scrollPos, w, cc.h, TRUE);
    }
}
static void UpdateCardsScrollInfo() {
    if (!g_hCardsHost) return;
    RECT rc; GetClientRect(g_hCardsHost, &rc);
    int pageH = rc.bottom - rc.top;
    SCROLLINFO si; memset(&si, 0, sizeof(si));
    si.cbSize = sizeof(si);
    si.fMask = SIF_RANGE | SIF_PAGE | SIF_POS;
    si.nMin = 0;
    si.nMax = (g_cardTotalHeight > 0) ? g_cardTotalHeight - 1 : 0;
    si.nPage = (UINT)((pageH > 1) ? pageH : 1);
    si.nPos = g_scrollPos;
    SetScrollInfo(g_hCardsHost, SB_VERT, &si, TRUE);
    // relit la position potentiellement replafonnee par Windows (page > range)
    GetScrollInfo(g_hCardsHost, SB_VERT, &si);
    g_scrollPos = si.nPos;
}

static void RefillMods() {
    std::stable_sort(g_mods.begin(), g_mods.end(), ModLess);
    if (g_hCardsHost) SendMessageW(g_hCardsHost, WM_SETREDRAW, FALSE, 0);
    ClearCards();

    const int iconX = 10, textX = 60;
    for (size_t i = 0; i < g_mods.size(); ++i) {
        const Mod& m = g_mods[i];
        int y = (int)i * CARD_H;

        HWND icon = CreateWindowExW(0, L"STATIC", L"",
            WS_CHILD | WS_VISIBLE | SS_ICON | SS_CENTERIMAGE,
            iconX, y + 12, 40, 40, g_hCardsHost, NULL, g_hInst, NULL);
        SendMessageW(icon, STM_SETICON, (WPARAM)GetOrLoadHIcon(m.iconPath), 0);
        { CardChild cc; cc.hwnd = icon; cc.x = iconX; cc.y = y + 12; cc.w = 40; cc.h = 40;
          cc.stretch = false; g_cardChildren.push_back(cc); }

        MkCardStatic(m.name, textX, y + 8, 400, 20,
            g_fontBold, 0, SS_LEFTNOWORDWRAP | SS_ENDELLIPSIS, true);

        // description courte (auto-remplie depuis Thunderstore, ou tapee a
        // la main dans l'editeur) : ce que fait le mod, en un coup d'oeil.
        MkCardStatic(m.description, textX, y + 28, 400, 18,
            g_font, RGB(40, 40, 40), SS_LEFTNOWORDWRAP | SS_ENDELLIPSIS, true);

        MkCardStatic(BuildDetailsLine(m), textX, y + 46, 400, 18,
            g_font, RowStatusColor(m), SS_LEFTNOWORDWRAP | SS_ENDELLIPSIS, true);

        std::wstring noteTxt = m.note.empty() ? L"" : (L"Note : " + m.note);
        MkCardStatic(noteTxt, textX, y + 64, 400, 18,
            g_font, RGB(120, 120, 120), SS_LEFTNOWORDWRAP | SS_ENDELLIPSIS, true);

        int bx = textX, by = y + 86, bh = 24, gap = 4;
        int id = RA_BASE + (int)i * RA_COUNT;
        HWND bWatch = MkCardButton(id + RA_WATCH, L"Watch",  bx, by, 58, bh); bx += 58 + gap;
        HWND bHist  = MkCardButton(id + RA_HIST,  L"Hist.",  bx, by, 48, bh); bx += 48 + gap;
        HWND bCheck = MkCardButton(id + RA_CHECK, L"Check+", bx, by, 60, bh); bx += 60 + gap;
        HWND bMark  = MkCardButton(id + RA_MARK,  L"OK",     bx, by, 38, bh); bx += 38 + gap;
        HWND bTs    = MkCardButton(id + RA_TS,    L"TS",     bx, by, 36, bh); bx += 36 + gap;
        HWND bDl    = MkCardButton(id + RA_DL,    L"DL",     bx, by, 36, bh); bx += 36 + gap;
        HWND bEdit  = MkCardButton(id + RA_EDIT,  L"Modif.", bx, by, 56, bh); bx += 56 + gap;
        HWND bMore  = MkCardButton(id + RA_MORE,  L"...",    bx, by, 32, bh); bx += 32 + gap;

        AddTip(bWatch, L"Ouvre la page du mod dans le navigateur");
        AddTip(bHist,  L"Ouvre la page d'historique / changelog du mod");
        AddTip(bCheck, L"Ouvre la page du mod ET note la date de verification du jour");
        AddTip(bMark,  L"Note la date de verification SANS ouvrir de lien - deja verifie ailleurs ?");
        AddTip(bTs,    L"Interroge Thunderstore pour connaitre la derniere version publiee");
        AddTip(bDl,    L"Telecharge le zip de la derniere version (demande ou l'enregistrer)");
        AddTip(bEdit,  L"Modifie ce mod");
        AddTip(bMore,  L"Plus d'actions : copier le lien, localiser le DLL, supprimer...");

        MkCardStatic(L"", 8, y + CARD_H - 6, 400, 2, g_font, 0, SS_ETCHEDHORZ, true);
    }

    g_cardTotalHeight = (int)g_mods.size() * CARD_H;
    UpdateCardsScrollInfo();
    RepositionCards();

    if (g_hCardsHost) {
        SendMessageW(g_hCardsHost, WM_SETREDRAW, TRUE, 0);
        RedrawWindow(g_hCardsHost, NULL, NULL,
            RDW_INVALIDATE | RDW_UPDATENOW | RDW_ALLCHILDREN);
    }
}

// ---------------------------------------------------------------- editeur mod
struct EditCtx {
    Mod m;
    bool ok;
    HWND hName, hCat, hUrl, hHist, hDesc, hDll, hIcon, hNote, hIconPreview;
    HICON previewIcon;
    EditCtx() : ok(false), hName(0), hCat(0), hUrl(0), hHist(0), hDesc(0), hDll(0), hIcon(0),
                hNote(0), hIconPreview(0), previewIcon(0) {}
};

static HWND MkLabel(HWND p, const wchar_t* t, int x, int y, int w) {
    HWND h = CreateWindowExW(0, L"STATIC", t, WS_CHILD | WS_VISIBLE,
        x, y, w, 18, p, NULL, g_hInst, NULL);
    SendMessageW(h, WM_SETFONT, (WPARAM)g_font, TRUE);
    return h;
}
static HWND MkEdit(HWND p, const std::wstring& txt, int x, int y, int w) {
    HWND h = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", txt.c_str(),
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
        x, y, w, 24, p, NULL, g_hInst, NULL);
    SendMessageW(h, WM_SETFONT, (WPARAM)g_font, TRUE);
    return h;
}
static HWND MkDlgButton(HWND p, int id, const wchar_t* txt, int x, int y, int w, int h) {
    HWND btn = CreateWindowExW(0, L"BUTTON", txt,
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
        x, y, w, h, p, (HMENU)(INT_PTR)id, g_hInst, NULL);
    SendMessageW(btn, WM_SETFONT, (WPARAM)g_font, TRUE);
    return btn;
}

// Boite "Parcourir..." standard ; renvoie 'current' inchange si l'utilisateur
// annule, pour que l'appelant n'ait pas a distinguer "annule" de "vide".
static std::wstring BrowseFile(HWND owner, const wchar_t* filter,
                               const std::wstring& current, const wchar_t* title)
{
    wchar_t buf[MAX_PATH]; buf[0] = 0;
    if (!current.empty()) {
        wcsncpy(buf, current.c_str(), MAX_PATH - 1);
        buf[MAX_PATH - 1] = 0;
    }
    std::wstring initDir;
    if (!current.empty()) {
        size_t p = current.find_last_of(L"\\/");
        if (p != std::wstring::npos) initDir = current.substr(0, p);
    }
    if (initDir.empty() || !DirExists(initDir)) {
        std::wstring pd = PluginsDir();
        initDir = DirExists(pd) ? pd : L"";
    }
    OPENFILENAMEW ofn; memset(&ofn, 0, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = owner;
    ofn.lpstrFilter = filter;
    ofn.lpstrFile = buf;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrTitle = title;
    ofn.lpstrInitialDir = initDir.empty() ? NULL : initDir.c_str();
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_HIDEREADONLY;
    if (GetOpenFileNameW(&ofn)) return std::wstring(buf);
    return current;
}
// Boite "Enregistrer sous..." (contrairement a BrowseFile, annuler renvoie
// une chaine vide plutot qu'une valeur "actuelle" - il n'y en a pas ici).
static std::wstring BrowseSaveFile(HWND owner, const wchar_t* filter,
                                   const std::wstring& initialDir,
                                   const std::wstring& suggestedName, const wchar_t* title)
{
    wchar_t buf[MAX_PATH]; buf[0] = 0;
    if (!suggestedName.empty()) {
        wcsncpy(buf, suggestedName.c_str(), MAX_PATH - 1);
        buf[MAX_PATH - 1] = 0;
    }
    OPENFILENAMEW ofn; memset(&ofn, 0, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = owner;
    ofn.lpstrFilter = filter;
    ofn.lpstrFile = buf;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrTitle = title;
    ofn.lpstrInitialDir = (!initialDir.empty() && DirExists(initialDir)) ? initialDir.c_str() : NULL;
    ofn.lpstrDefExt = L"zip";
    ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_HIDEREADONLY;
    if (GetSaveFileNameW(&ofn)) return std::wstring(buf);
    return L"";
}
static void UpdateIconPreview(EditCtx* c, const std::wstring& path) {
    if (!c || !c->hIconPreview) return;
    HICON hi = path.empty() ? NULL : LoadScaledIconFromFile(path, 32);
    SendMessageW(c->hIconPreview, STM_SETICON, (WPARAM)hi, 0);
    if (c->previewIcon) DestroyIcon(c->previewIcon);
    c->previewIcon = hi;
}

static LRESULT CALLBACK EditProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    EditCtx* c = (EditCtx*)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
    switch (msg) {
    case WM_CREATE: {
        CREATESTRUCTW* cs = (CREATESTRUCTW*)lp;
        c = (EditCtx*)cs->lpCreateParams;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)c);

        // -- colonne de gauche : champs texte (nom / categorie / liens) -----
        const int LX = 12, LW = 360;
        MkLabel(hwnd, L"Nom du mod *", LX, 10, LW);
        c->hName = MkEdit(hwnd, c->m.name, LX, 28, LW);
        MkLabel(hwnd, L"Categorie / auteur", LX, 58, LW);
        c->hCat = MkEdit(hwnd, c->m.cat, LX, 76, LW);
        MkLabel(hwnd, L"Lien de la page du mod *", LX, 106, LW);
        c->hUrl = MkEdit(hwnd, c->m.url, LX, 124, LW);
        MkDlgButton(hwnd, IDC_E_AUTOFILL, L"Auto-remplir depuis Thunderstore", LX, 150, 220, 24);
        MkLabel(hwnd, L"Lien historique / changelog (optionnel)", LX, 184, LW);
        c->hHist = MkEdit(hwnd, c->m.changelogUrl, LX, 202, LW);

        // -- colonne de droite : icone (apercu + parcourir + effacer) -------
        const int IX = LX + LW + 16;   // 388
        MkLabel(hwnd, L"Icone", IX, 10, 130);
        c->hIconPreview = CreateWindowExW(WS_EX_CLIENTEDGE, L"STATIC", L"",
            WS_CHILD | WS_VISIBLE | SS_ICON | SS_CENTERIMAGE,
            IX, 28, 40, 40, hwnd, NULL, g_hInst, NULL);
        MkDlgButton(hwnd, IDC_E_BROWSEICON, L"Parcourir...", IX, 72, 130, 24);
        MkDlgButton(hwnd, IDC_E_CLEARICON,  L"Effacer",      IX, 100, 130, 24);

        // -- description (courte, affichee sur la carte) - pleine largeur ---
        MkLabel(hwnd, L"Description courte (affichee sur la carte)", LX, 232, LW + 16 + 130);
        c->hDesc = MkEdit(hwnd, c->m.description, LX, 250, LW + 16 + 130);

        // -- DLL associe, pleine largeur -------------------------------------
        MkLabel(hwnd, L"DLL installe (pour verifier qu'il est bien present)", LX, 282, LW + 16 + 130);
        c->hDll = MkEdit(hwnd, c->m.dllPath, LX, 300, LW);
        MkDlgButton(hwnd, IDC_E_BROWSEDLL, L"Parcourir...", LX + LW + 24, 300, 124, 24);

        MkLabel(hwnd, L"Note (version installee, remarques...)", LX, 330, LW + 16 + 130);
        c->hNote = MkEdit(hwnd, c->m.note, LX, 348, LW + 16 + 130);

        HWND ok = CreateWindowExW(0, L"BUTTON", L"OK",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON,
            336, 386, 92, 28, hwnd, (HMENU)IDOK, g_hInst, NULL);
        HWND ca = CreateWindowExW(0, L"BUTTON", L"Annuler",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
            436, 386, 92, 28, hwnd, (HMENU)IDCANCEL, g_hInst, NULL);
        SendMessageW(ok, WM_SETFONT, (WPARAM)g_font, TRUE);
        SendMessageW(ca, WM_SETFONT, (WPARAM)g_font, TRUE);

        if (!c->m.iconPath.empty()) UpdateIconPreview(c, c->m.iconPath);

        SetFocus(c->hName);
        return 0;
    }
    case WM_COMMAND:
        if (LOWORD(wp) == IDC_E_AUTOFILL && c) {
            std::wstring url = Clean(GetTextOf(c->hUrl));
            if (url.empty()) {
                MessageBoxW(hwnd, L"Renseigne d'abord le lien de la page du mod.",
                    L"ValMods", MB_OK | MB_ICONINFORMATION);
                return 0;
            }
            HCURSOR oldCursor = SetCursor(LoadCursor(NULL, IDC_WAIT));
            TsAutofillResult r = FetchThunderstoreAutofill(url);
            SetCursor(oldCursor);

            if (!r.isThunderstore) {
                MessageBoxW(hwnd,
                    L"Ce lien ne pointe pas vers thunderstore.io :\n"
                    L"l'auto-remplissage ne fonctionne que pour les mods\n"
                    L"heberges sur Thunderstore.",
                    L"ValMods", MB_OK | MB_ICONWARNING);
                return 0;
            }
            if (!r.ok) {
                std::wstring m = L"Auto-remplissage impossible :\n" + r.error;
                MessageBoxW(hwnd, m.c_str(), L"ValMods", MB_OK | MB_ICONWARNING);
                return 0;
            }

            // remplace toujours les champs concernes : un clic explicite sur
            // "Auto-remplir" veut dire "je veux les donnees fraiches de Thunderstore".
            if (!r.name.empty())         SetWindowTextW(c->hName, r.name.c_str());
            if (!r.category.empty())     SetWindowTextW(c->hCat, r.category.c_str());
            if (!r.changelogUrl.empty()) SetWindowTextW(c->hHist, r.changelogUrl.c_str());
            if (!r.description.empty())  SetWindowTextW(c->hDesc, r.description.c_str());
            if (!r.localIconPath.empty()) {
                SetWindowTextW(c->hIcon, r.localIconPath.c_str());
                UpdateIconPreview(c, r.localIconPath);
            }
            c->m.tsVersion = r.latestVersion;   // porte jusqu'a l'enregistrement (pas de champ texte dedie)

            std::wstring summary = L"Champs remplis depuis Thunderstore.\n"
                L"Derniere version publiee : " + r.latestVersion;
            if (r.localIconPath.empty())
                summary += L"\n(icone non recuperee - le mod n'en a peut-etre pas)";
            Info(hwnd, summary.c_str());
            return 0;
        }
        if (LOWORD(wp) == IDC_E_BROWSEDLL && c) {
            std::wstring picked = BrowseFile(hwnd,
                L"Bibliotheques (*.dll)\0*.dll\0Tous les fichiers (*.*)\0*.*\0",
                GetTextOf(c->hDll), L"Choisir le DLL installe");
            SetWindowTextW(c->hDll, picked.c_str());
            return 0;
        }
        if (LOWORD(wp) == IDC_E_BROWSEICON && c) {
            std::wstring picked = BrowseFile(hwnd,
                L"Images (*.png;*.jpg;*.jpeg;*.bmp;*.ico;*.gif)\0*.png;*.jpg;*.jpeg;*.bmp;*.ico;*.gif\0"
                L"Tous les fichiers (*.*)\0*.*\0",
                GetTextOf(c->hIcon), L"Choisir une icone");
            SetWindowTextW(c->hIcon, picked.c_str());
            UpdateIconPreview(c, picked);
            return 0;
        }
        if (LOWORD(wp) == IDC_E_CLEARICON && c) {
            SetWindowTextW(c->hIcon, L"");
            UpdateIconPreview(c, L"");
            return 0;
        }
        if (LOWORD(wp) == IDOK && c) {
            std::wstring n = Clean(GetTextOf(c->hName));
            std::wstring u = Clean(GetTextOf(c->hUrl));
            if (n.empty() || u.empty()) {
                MessageBoxW(hwnd, L"Le nom et le lien de la page du mod sont obligatoires.",
                    L"ValMods", MB_OK | MB_ICONWARNING);
                return 0;
            }
            c->m.name = n;
            c->m.cat  = Clean(GetTextOf(c->hCat));
            c->m.url  = u;
            c->m.changelogUrl = Clean(GetTextOf(c->hHist));
            c->m.description  = Clean(GetTextOf(c->hDesc));
            c->m.dllPath      = Clean(GetTextOf(c->hDll));
            c->m.iconPath     = Clean(GetTextOf(c->hIcon));
            c->m.note = Clean(GetTextOf(c->hNote));
            c->ok = true;
            DestroyWindow(hwnd);
            return 0;
        }
        if (LOWORD(wp) == IDCANCEL) { DestroyWindow(hwnd); return 0; }
        break;
    case WM_DESTROY:
        // l'apercu d'icone est une copie GDI+ locale a cette fenetre, jamais
        // partagee avec l'image list de la liste principale : a nettoyer ici.
        if (c && c->previewIcon) { DestroyIcon(c->previewIcon); c->previewIcon = NULL; }
        return 0;
    case WM_CLOSE:
        DestroyWindow(hwnd);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

static bool ShowModEditor(HWND parent, const wchar_t* title, Mod& io) {
    static bool reg = false;
    if (!reg) {
        WNDCLASSEXW wc; memset(&wc, 0, sizeof(wc));
        wc.cbSize = sizeof(wc);
        wc.lpfnWndProc = EditProc;
        wc.hInstance = g_hInst;
        wc.hCursor = LoadCursor(NULL, IDC_ARROW);
        wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
        wc.lpszClassName = L"ValModsEditor";
        RegisterClassExW(&wc);
        reg = true;
    }
    EditCtx ctx; ctx.m = io;
    RECT r = { 0, 0, 540, 430 };
    DWORD style = WS_POPUP | WS_CAPTION | WS_SYSMENU;
    AdjustWindowRect(&r, style, FALSE);
    RECT pr; GetWindowRect(parent, &pr);
    int w = r.right - r.left, h = r.bottom - r.top;
    int x = pr.left + ((pr.right - pr.left) - w) / 2;
    int y = pr.top + ((pr.bottom - pr.top) - h) / 2;

    HWND dlg = CreateWindowExW(WS_EX_DLGMODALFRAME | WS_EX_CONTROLPARENT,
        L"ValModsEditor", title, style, x, y, w, h, parent, NULL, g_hInst, &ctx);
    if (!dlg) return false;
    EnableWindow(parent, FALSE);
    ShowWindow(dlg, SW_SHOW);
    UpdateWindow(dlg);

    MSG msg;
    while (IsWindow(dlg) && GetMessageW(&msg, NULL, 0, 0)) {
        if (!IsDialogMessageW(dlg, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }
    EnableWindow(parent, TRUE);
    SetForegroundWindow(parent);
    if (ctx.ok) io = ctx.m;
    return ctx.ok;
}

// ---------------------------------------------------------------- sauvegardes
struct SaveEntry { std::wstring name, file; };
static std::vector<SaveEntry> g_worlds, g_chars;

static void FillSaveList(HWND list, const std::wstring& dir, const wchar_t* ext,
                         std::vector<SaveEntry>& out, bool worldSizes)
{
    out.clear();
    ListView_DeleteAllItems(list);
    if (!DirExists(dir)) return;
    std::wstring pat = dir + L"\\*" + ext;
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW(pat.c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return;
    int i = 0;
    do {
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
        std::wstring fn = fd.cFileName;
        size_t dot = fn.find_last_of(L'.');
        std::wstring base = (dot == std::wstring::npos) ? fn : fn.substr(0, dot);

        SaveEntry e; e.name = base; e.file = dir + L"\\" + fn;
        out.push_back(e);

        LVITEMW it; memset(&it, 0, sizeof(it));
        it.mask = LVIF_TEXT | LVIF_PARAM;
        it.iItem = i;
        it.pszText = (LPWSTR)out[out.size() - 1].name.c_str();
        it.lParam = (LPARAM)i;
        ListView_InsertItem(list, &it);

        // taille : pour un monde on prend le .db (le gros fichier)
        ULONGLONG size = ((ULONGLONG)fd.nFileSizeHigh << 32) | fd.nFileSizeLow;
        FILETIME ft = fd.ftLastWriteTime;
        if (worldSizes) {
            std::wstring db = dir + L"\\" + base + L".db";
            WIN32_FILE_ATTRIBUTE_DATA ad;
            if (GetFileAttributesExW(db.c_str(), GetFileExInfoStandard, &ad)) {
                size = ((ULONGLONG)ad.nFileSizeHigh << 32) | ad.nFileSizeLow;
                ft = ad.ftLastWriteTime;
            }
        }
        wchar_t sz[40];
        wsprintfW(sz, L"%lu Ko", (unsigned long)(size / 1024));
        ListView_SetItemText(list, i, 1, sz);
        std::wstring dt = FileTimeText(ft);
        ListView_SetItemText(list, i, 2, (LPWSTR)dt.c_str());
        ++i;
    } while (FindNextFileW(h, &fd));
    FindClose(h);
}

static void RefreshSaves() {
    FillSaveList(g_hWorlds, WorldsDir(), L".fwl", g_worlds, true);
    FillSaveList(g_hChars,  CharsDir(),  L".fch", g_chars,  false);
}

static void BackupSelection(HWND hwnd) {
    HWND list = g_lastListIsWorld ? g_hWorlds : g_hChars;
    std::vector<SaveEntry>& vec = g_lastListIsWorld ? g_worlds : g_chars;
    std::wstring dir = g_lastListIsWorld ? WorldsDir() : CharsDir();
    int sel = ListView_GetNextItem(list, -1, LVNI_SELECTED);
    if (sel < 0 || sel >= (int)vec.size()) {
        Info(hwnd, L"Selectionne d'abord un monde ou un personnage.");
        return;
    }
    std::wstring name = vec[sel].name;
    std::wstring stamp = NowStamp();
    for (size_t i = 0; i < stamp.size(); ++i)
        if (stamp[i] == L':' || stamp[i] == L' ') stamp[i] = L'-';
    std::wstring dest = BackupRoot() + L"\\" +
        (g_lastListIsWorld ? L"mondes\\" : L"personnages\\") + name + L"_" + stamp;
    MakeDirs(dest);

    int n = 0;
    std::wstring pat = dir + L"\\" + name + L".*";
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW(pat.c_str(), &fd);
    if (h != INVALID_HANDLE_VALUE) {
        do {
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
            std::wstring src = dir + L"\\" + fd.cFileName;
            std::wstring dst = dest + L"\\" + fd.cFileName;
            if (CopyFileW(src.c_str(), dst.c_str(), FALSE)) ++n;
        } while (FindNextFileW(h, &fd));
        FindClose(h);
    }
    wchar_t msg[512];
    wsprintfW(msg, L"%d fichier(s) copie(s) dans :\n%s", n, dest.c_str());
    Info(hwnd, msg);
}

// ---------------------------------------------------------------- actions mod
// Chaque action prend l'index du mod concerne EXPLICITEMENT (idx), passe par
// le bouton de la carte qui l'a declenchee - il n'y a plus de "selection"
// au sens ListView depuis le passage a la vue en cartes.
static void UpdateTitle();

static bool ValidMod(HWND hwnd, int idx) {
    if (idx >= 0 && idx < (int)g_mods.size()) return true;
    Info(hwnd, L"Ce mod n'est plus dans la liste (elle a change entre-temps).");
    return false;
}
static void RefreshModsUI() {
    RefreshModsUI();
    UpdateTitle();
    if (g_hCardsHost) {
        RedrawWindow(g_hCardsHost, NULL, NULL,
            RDW_INVALIDATE | RDW_UPDATENOW | RDW_ALLCHILDREN);
    }
}

static void ActionOpen(HWND hwnd, int idx, bool stamp) {
    if (!ValidMod(hwnd, idx)) return;
    std::wstring url = g_mods[idx].url;
    if (url.empty()) { Info(hwnd, L"Ce mod n'a pas de lien."); return; }
    ShellExecuteW(NULL, L"open", url.c_str(), NULL, NULL, SW_SHOWNORMAL);
    if (stamp) {
        g_mods[idx].last = NowStamp();
        SaveMods();
        RefreshModsUI();
    }
}
static void ActionMark(HWND hwnd, int idx) {
    if (!ValidMod(hwnd, idx)) return;
    g_mods[idx].last = NowStamp();
    SaveMods();
    RefreshModsUI();
}
static void ActionAdd(HWND hwnd) {
    Mod m;
    std::wstring clip = ClipboardText();
    if (clip.compare(0, 4, L"http") == 0 && clip.size() < 400) m.url = clip;
    if (ShowModEditor(hwnd, L"Ajouter un mod", m)) {
        g_mods.push_back(m);
        SaveMods();
        RefreshModsUI();
    }
}
static void ActionEdit(HWND hwnd, int idx) {
    if (!ValidMod(hwnd, idx)) return;
    Mod m = g_mods[idx];
    if (ShowModEditor(hwnd, L"Modifier le mod", m)) {
        g_mods[idx] = m;
        SaveMods();
        RefreshModsUI();
    }
}
static void ActionDelete(HWND hwnd, int idx) {
    if (!ValidMod(hwnd, idx)) return;
    std::wstring q = L"Supprimer \"" + g_mods[idx].name + L"\" de la liste ?\n"
                     L"(le mod n'est pas desinstalle, seule la fiche est supprimee)";
    if (MessageBoxW(hwnd, q.c_str(), L"ValMods", MB_YESNO | MB_ICONQUESTION) != IDYES) return;
    g_mods.erase(g_mods.begin() + idx);
    SaveMods();
    RefreshModsUI();
}
static void ActionCopy(HWND hwnd, int idx) {
    if (!ValidMod(hwnd, idx)) return;
    CopyToClipboard(hwnd, g_mods[idx].url);
}
static void ActionOpenHistory(HWND hwnd, int idx) {
    if (!ValidMod(hwnd, idx)) return;
    if (g_mods[idx].changelogUrl.empty()) {
        Info(hwnd, L"Ce mod n'a pas de lien vers son historique des versions.\n"
                   L"Ajoute-le en modifiant le mod (bouton Modif.).");
        return;
    }
    ShellExecuteW(NULL, L"open", g_mods[idx].changelogUrl.c_str(), NULL, NULL, SW_SHOWNORMAL);
}
static void ActionLocateDll(HWND hwnd, int idx) {
    if (!ValidMod(hwnd, idx)) return;
    const std::wstring& p = g_mods[idx].dllPath;
    if (p.empty()) {
        Info(hwnd, L"Aucun DLL associe a ce mod.\nAssocie-le en modifiant le mod (bouton Modif.).");
        return;
    }
    if (!FileExists(p)) {
        std::wstring m = L"Le fichier DLL associe est introuvable :\n" + p;
        MessageBoxW(hwnd, m.c_str(), L"ValMods", MB_OK | MB_ICONWARNING);
        return;
    }
    RevealFile(p);
}
static void ActionOpenDllDir(HWND hwnd, int idx) {
    if (!ValidMod(hwnd, idx)) return;
    const std::wstring& p = g_mods[idx].dllPath;
    if (p.empty()) {
        Info(hwnd, L"Aucun DLL associe a ce mod.\nAssocie-le en modifiant le mod (bouton Modif.).");
        return;
    }
    size_t s = p.find_last_of(L"\\/");
    std::wstring dir = (s == std::wstring::npos) ? PluginsDir() : p.substr(0, s);
    OpenFolder(hwnd, dir, L"dossier du DLL");
}
static void ShowRowOverflowMenu(HWND hwnd, int idx) {
    if (!ValidMod(hwnd, idx)) return;
    g_ctxMenuModIndex = idx;
    POINT pt; GetCursorPos(&pt);
    HMENU m = CreatePopupMenu();
    AppendMenuW(m, MF_STRING, IDM_CTX_COPY,        L"Copier le lien");
    AppendMenuW(m, MF_STRING, IDM_CTX_LOCATE_DLL,  L"Localiser le DLL dans l'explorateur");
    AppendMenuW(m, MF_STRING, IDM_CTX_OPEN_DLLDIR, L"Ouvrir le dossier du DLL");
    AppendMenuW(m, MF_SEPARATOR, 0, NULL);
    AppendMenuW(m, MF_STRING, IDM_CTX_DELETE,      L"Supprimer");
    TrackPopupMenu(m, TPM_RIGHTBUTTON, pt.x, pt.y, 0, hwnd, NULL);
    DestroyMenu(m);
}
static void ActionCheckThunderstore(HWND hwnd, int idx) {
    if (!ValidMod(hwnd, idx)) return;
    std::wstring name = g_mods[idx].name;
    std::wstring url = g_mods[idx].url;

    HCURSOR oldCursor = SetCursor(LoadCursor(NULL, IDC_WAIT));
    TsCheckResult r = CheckThunderstoreVersion(url);
    SetCursor(oldCursor);

    if (!r.isThunderstore) {
        Info(hwnd, L"Le lien de ce mod ne pointe pas vers thunderstore.io.\n"
                   L"La verification automatique ne fonctionne que pour les mods\n"
                   L"heberges sur Thunderstore (Nexus, GitHub... ne sont pas geres).");
        return;
    }
    if (!r.ok) {
        std::wstring m = L"Verification impossible :\n" + r.error;
        MessageBoxW(hwnd, m.c_str(), L"ValMods", MB_OK | MB_ICONWARNING);
        return;
    }

    // la liste peut avoir ete retriee pendant l'appel reseau : on retrouve
    // le mod par son nom plutot que de garder l'index d'avant l'appel.
    for (size_t k = 0; k < g_mods.size(); ++k) {
        if (g_mods[k].name == name) {
            g_mods[k].tsVersion = r.latestVersion;
            // bonus non-destructif : on ne remplit la description que si
            // elle est vide, contrairement a Auto-remplir qui l'ecrase
            // toujours (ici ce n'est pas l'action demandee explicitement).
            if (g_mods[k].description.empty() && !r.description.empty())
                g_mods[k].description = r.description;
            g_mods[k].last = NowStamp();   // une verification en ligne compte comme une verification
            SaveMods();
            RefillMods();
            break;
        }
    }

    std::wstring msg = L"Derniere version publiee sur Thunderstore : " + r.latestVersion;
    if (r.deprecated)
        msg += L"\n\nATTENTION : ce mod est marque comme deprecie (abandonne) "
              L"par son auteur sur Thunderstore.";
    Info(hwnd, msg.c_str());
}
static void ActionDownloadLatest(HWND hwnd, int idx) {
    if (!ValidMod(hwnd, idx)) return;
    std::wstring name = g_mods[idx].name;
    std::wstring url = g_mods[idx].url;

    std::wstring ns, pkgName;
    if (!ParseThunderstoreUrl(url, ns, pkgName)) {
        Info(hwnd, L"Le lien de ce mod ne pointe pas vers thunderstore.io :\n"
                   L"le telechargement direct ne fonctionne que pour les mods\n"
                   L"heberges sur Thunderstore.");
        return;
    }

    std::wstring version = g_mods[idx].tsVersion;
    if (version.empty()) {
        // pas encore verifie : on va d'abord chercher la derniere version
        HCURSOR oldCursor = SetCursor(LoadCursor(NULL, IDC_WAIT));
        TsCheckResult chk = CheckThunderstoreVersion(url);
        SetCursor(oldCursor);
        if (!chk.ok) {
            std::wstring m = L"Impossible de determiner la derniere version :\n" + chk.error;
            MessageBoxW(hwnd, m.c_str(), L"ValMods", MB_OK | MB_ICONWARNING);
            return;
        }
        version = chk.latestVersion;
    }

    // "Parcourir" pour choisir ou enregistrer le zip - suggestion par defaut
    // dans downloads/ mais entierement modifiable, comme demande.
    MakeDirs(DownloadsRoot());
    std::wstring suggested = SanitizeFileName(ns) + L"-" + SanitizeFileName(pkgName) + L"-" +
                             SanitizeFileName(version) + L".zip";
    std::wstring destPath = BrowseSaveFile(hwnd,
        L"Archive zip (*.zip)\0*.zip\0Tous les fichiers (*.*)\0*.*\0",
        DownloadsRoot(), suggested, L"Enregistrer le zip du mod sous...");
    if (destPath.empty()) return;   // annule par l'utilisateur

    HCURSOR oldCursor = SetCursor(LoadCursor(NULL, IDC_WAIT));
    std::wstring err;
    bool ok = DownloadThunderstoreZip(ns, pkgName, version, destPath, err);
    SetCursor(oldCursor);

    if (!ok) {
        std::wstring m = L"Telechargement impossible :\n" + err;
        MessageBoxW(hwnd, m.c_str(), L"ValMods", MB_OK | MB_ICONWARNING);
        return;
    }

    // confirme/rafraichit tsVersion + date de verification (le telechargement
    // implique qu'on connait desormais la version exacte tres precisement)
    for (size_t k = 0; k < g_mods.size(); ++k) {
        if (g_mods[k].name == name) {
            g_mods[k].tsVersion = version;
            g_mods[k].last = NowStamp();
            SaveMods();
            RefillMods();
            break;
        }
    }

    std::wstring msg = L"Version " + version + L" telechargee :\n" + destPath +
        L"\n\nA extraire toi-meme dans BepInEx\\plugins - ValMods ne modifie\n"
        L"jamais tes fichiers de jeu automatiquement.";
    Info(hwnd, msg.c_str());
}

// ---------------------------------------------------------------- parametres
static int CALLBACK BrowseCB(HWND hwnd, UINT msg, LPARAM lp, LPARAM data) {
    if (msg == BFFM_INITIALIZED && data)
        SendMessageW(hwnd, BFFM_SETSELECTION, TRUE, data);
    return 0;
}
static void ChooseValheimDir(HWND hwnd) {
    BROWSEINFOW bi; memset(&bi, 0, sizeof(bi));
    wchar_t path[MAX_PATH] = L"";
    bi.hwndOwner = hwnd;
    bi.lpszTitle = L"Selectionne le dossier d'installation de Valheim (celui qui contient valheim.exe)";
    bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
    bi.lpfn = BrowseCB;
    bi.lParam = g_valheimDir.empty() ? 0 : (LPARAM)g_valheimDir.c_str();
    LPITEMIDLIST pidl = SHBrowseForFolderW(&bi);
    if (!pidl) return;
    if (SHGetPathFromIDListW(pidl, path)) {
        g_valheimDir = path;
        SaveIni();
        if (!FileExists(g_valheimDir + L"\\valheim.exe"))
            Info(hwnd, L"Attention : valheim.exe n'a pas ete trouve dans ce dossier.\n"
                       L"Le chemin est quand meme enregistre.");
    }
    CoTaskMemFree(pidl);
}

// ---------------------------------------------------------------- interface
static void ShowPage(int idx) {
    for (int i = 0; i < g_nMods; ++i)  ShowWindow(g_pageMods[i],  idx == 0 ? SW_SHOW : SW_HIDE);
    for (int i = 0; i < g_nSaves; ++i) ShowWindow(g_pageSaves[i], idx == 1 ? SW_SHOW : SW_HIDE);
    if (idx == 1) RefreshSaves();
}

static void LayoutAll(HWND hwnd) {
    RECT rc; GetClientRect(hwnd, &rc);
    int W = rc.right, H = rc.bottom;
    MoveWindow(g_hTab, 8, 8, W - 16, H - 16, TRUE);

    RECT pr = { 8, 8, W - 8, H - 8 };
    TabCtrl_AdjustRect(g_hTab, FALSE, &pr);
    int px = pr.left + 4, py = pr.top + 4;
    int pw = (pr.right - pr.left) - 8, ph = (pr.bottom - pr.top) - 8;
    if (pw < 100) pw = 100;
    if (ph < 100) ph = 100;

    // --- onglet mods : barre (Ajouter + tri) + panneau de cartes scrollable
    const int bh = 28, gap = 6, topH = bh + 4;
    HWND badd = GetDlgItem(hwnd, IDC_BADD);
    if (badd) MoveWindow(badd, px, py, 100, bh, TRUE);
    HWND combo = GetDlgItem(hwnd, IDC_SORTCOMBO);
    // la hauteur passee a un CBS_DROPDOWNLIST fixe celle de la liste DEROULEE,
    // pas celle du controle ferme (determinee par la police) - 200 est large.
    if (combo) MoveWindow(combo, px + 100 + gap, py, 220, 200, TRUE);
    HWND dirBtn = GetDlgItem(hwnd, IDC_SORTDIR);
    if (dirBtn) MoveWindow(dirBtn, px + 100 + gap + 220 + gap, py, 120, bh, TRUE);
    if (g_hCardsHost) MoveWindow(g_hCardsHost, px, py + topH, pw, ph - topH, TRUE);

    // --- onglet sauvegardes
    int lh = (ph - 84) / 2;
    if (lh < 60) lh = 60;
    int y = py;
    MoveWindow(GetDlgItem(hwnd, IDC_LBL1), px, y, pw, 18, TRUE); y += 20;
    MoveWindow(g_hWorlds, px, y, pw, lh, TRUE);                  y += lh + 8;
    MoveWindow(GetDlgItem(hwnd, IDC_LBL2), px, y, pw, 18, TRUE); y += 20;
    MoveWindow(g_hChars, px, y, pw, lh, TRUE);                   y += lh + 8;
    int sids[4] = { IDC_BREFRESH, IDC_BOPENSAVE, IDC_BBACKUP, IDC_BOPENBK };
    for (int i = 0; i < 4; ++i) {
        HWND b = GetDlgItem(hwnd, sids[i]);
        if (b) MoveWindow(b, px + i * (140 + gap), y, 140, bh, TRUE);
    }
}

static HWND MkButton(HWND p, int id, const wchar_t* txt) {
    HWND h = CreateWindowExW(0, L"BUTTON", txt,
        WS_CHILD | WS_TABSTOP | BS_PUSHBUTTON, 0, 0, 10, 10,
        p, (HMENU)(INT_PTR)id, g_hInst, NULL);
    SendMessageW(h, WM_SETFONT, (WPARAM)g_font, TRUE);
    return h;
}
static HWND MkList(HWND p, int id) {
    HWND h = CreateWindowExW(WS_EX_CLIENTEDGE, WC_LISTVIEWW, L"",
        WS_CHILD | WS_TABSTOP | LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS,
        0, 0, 10, 10, p, (HMENU)(INT_PTR)id, g_hInst, NULL);
    ListView_SetExtendedListViewStyle(h,
        LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES | LVS_EX_DOUBLEBUFFER);
    SendMessageW(h, WM_SETFONT, (WPARAM)g_font, TRUE);
    return h;
}
static void AddCol(HWND list, int i, const wchar_t* txt, int w) {
    LVCOLUMNW c; memset(&c, 0, sizeof(c));
    c.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_SUBITEM;
    c.iSubItem = i;
    c.pszText = (LPWSTR)txt;
    c.cx = w;
    ListView_InsertColumn(list, i, &c);
}
static void AddTip(HWND ctrl, const wchar_t* text) {
    if (!g_hTooltip || !ctrl) return;
    TOOLINFOW ti; memset(&ti, 0, sizeof(ti));
    ti.cbSize = sizeof(ti);
    ti.uFlags = TTF_IDISHWND | TTF_SUBCLASS;
    ti.hwnd = GetParent(ctrl);
    ti.uId = (UINT_PTR)ctrl;
    ti.lpszText = (LPWSTR)text;
    SendMessageW(g_hTooltip, TTM_ADDTOOLW, 0, (LPARAM)&ti);
}

// ---------------------------------------------------------------- panneau cartes
// Fenetre custom qui heberge les cartes de mods et gere son propre
// defilement vertical (molette + barre de scroll). RefillMods() cree/detruit
// les controles enfants ; cette proc ne fait que scroller/redimensionner ce
// qui existe deja, et transmettre la couleur de texte des controles "status"
// (voir MkCardStatic : la couleur est stockee dans GWLP_USERDATA de chacun).
static LRESULT CALLBACK CardsHostProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    // Un bouton envoie WM_COMMAND (BN_CLICKED) a son PARENT IMMEDIAT, qui est
    // g_hCardsHost pour tous les boutons de carte - pas la fenetre principale.
    // Sans ce relais, DefWindowProcW l'aurait simplement absorbe en silence :
    // c'est ce qui rendait tous les boutons de carte muets.
    case WM_COMMAND:
        return SendMessageW(GetParent(hwnd), WM_COMMAND, wp, lp);
    case WM_SIZE:
        UpdateCardsScrollInfo();
        RepositionCards();
        return 0;
    case WM_VSCROLL: {
        SCROLLINFO si; memset(&si, 0, sizeof(si));
        si.cbSize = sizeof(si); si.fMask = SIF_ALL;
        GetScrollInfo(hwnd, SB_VERT, &si);
        int pos = si.nPos;
        switch (LOWORD(wp)) {
            case SB_LINEUP:   pos -= 30; break;
            case SB_LINEDOWN: pos += 30; break;
            case SB_PAGEUP:   pos -= (int)si.nPage; break;
            case SB_PAGEDOWN: pos += (int)si.nPage; break;
            case SB_THUMBTRACK: case SB_THUMBPOSITION: pos = si.nTrackPos; break;
            case SB_TOP:    pos = si.nMin; break;
            case SB_BOTTOM: pos = si.nMax; break;
            default: break;
        }
        int maxPos = si.nMax - (int)si.nPage + 1; if (maxPos < 0) maxPos = 0;
        if (pos < 0) pos = 0;
        if (pos > maxPos) pos = maxPos;
        g_scrollPos = pos;
        si.fMask = SIF_POS; si.nPos = pos;
        SetScrollInfo(hwnd, SB_VERT, &si, TRUE);
        RepositionCards();
        return 0;
    }
    case WM_MOUSEWHEEL: {
        // GET_WHEEL_DELTA_WPARAM vient de <windowsx.h>, non inclus ici ;
        // c'est litteralement sa definition : le mot haut de wParam, signe.
        short delta = (short)HIWORD(wp);
        int amount = -((int)delta / WHEEL_DELTA) * 60;   // 60px par cran, sens naturel
        SCROLLINFO si; memset(&si, 0, sizeof(si));
        si.cbSize = sizeof(si); si.fMask = SIF_ALL;
        GetScrollInfo(hwnd, SB_VERT, &si);
        int maxPos = si.nMax - (int)si.nPage + 1; if (maxPos < 0) maxPos = 0;
        int pos = g_scrollPos + amount;
        if (pos < 0) pos = 0;
        if (pos > maxPos) pos = maxPos;
        g_scrollPos = pos;
        si.fMask = SIF_POS; si.nPos = pos;
        SetScrollInfo(hwnd, SB_VERT, &si, TRUE);
        RepositionCards();
        return 0;
    }
    case WM_CTLCOLORSTATIC: {
        HWND ctrl = (HWND)lp;
        LONG_PTR cr = GetWindowLongPtrW(ctrl, GWLP_USERDATA);
        if (cr != 0) {
            HDC hdc = (HDC)wp;
            SetTextColor(hdc, (COLORREF)cr);
            SetBkMode(hdc, TRANSPARENT);
            return (LRESULT)GetSysColorBrush(COLOR_WINDOW);
        }
        break;   // pas de couleur assignee (nom du mod, separateur...) : par defaut
    }
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

static void BuildMenu(HWND hwnd) {
    HMENU bar = CreateMenu();
    HMENU f = CreatePopupMenu();
    AppendMenuW(f, MF_STRING, IDM_OPENDATA, L"Ouvrir le dossier de donnees\tvalmods.json");
    AppendMenuW(f, MF_SEPARATOR, 0, NULL);
    AppendMenuW(f, MF_STRING, IDM_EXIT, L"Quitter");
    HMENU d = CreatePopupMenu();
    AppendMenuW(d, MF_STRING, IDM_PLUGINS, L"BepInEx\\plugins");
    AppendMenuW(d, MF_STRING, IDM_CONFIG,  L"BepInEx\\config");
    AppendMenuW(d, MF_STRING, IDM_BEPINEX, L"BepInEx");
    AppendMenuW(d, MF_STRING, IDM_GAMEDIR, L"Dossier du jeu");
    AppendMenuW(d, MF_SEPARATOR, 0, NULL);
    AppendMenuW(d, MF_STRING, IDM_SAVES,   L"Sauvegardes (LocalLow)");
    AppendMenuW(d, MF_STRING, IDM_DOWNLOADS, L"Telechargements (zips Thunderstore)");
    HMENU p = CreatePopupMenu();
    AppendMenuW(p, MF_STRING, IDM_SETDIR, L"Definir le dossier de Valheim...");
    HMENU a = CreatePopupMenu();
    AppendMenuW(a, MF_STRING, IDM_ABOUT, L"A propos");
    AppendMenuW(bar, MF_POPUP, (UINT_PTR)f, L"Fichier");
    AppendMenuW(bar, MF_POPUP, (UINT_PTR)d, L"Dossiers");
    AppendMenuW(bar, MF_POPUP, (UINT_PTR)p, L"Parametres");
    AppendMenuW(bar, MF_POPUP, (UINT_PTR)a, L"?");
    SetMenu(hwnd, bar);
}

static void UpdateTitle() {
    std::wstring t = L"ValMods - ";
    wchar_t n[32]; wsprintfW(n, L"%d mod(s)", (int)g_mods.size());
    t += n;
    if (!g_valheimDir.empty()) t += L"  |  " + g_valheimDir;
    else t += L"  |  dossier Valheim non defini";
    SetWindowTextW(g_hMain, t.c_str());
}

// ---------------------------------------------------------------- proc princ
static LRESULT CALLBACK MainProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_CREATE: {
        g_hMain = hwnd;
        BuildMenu(hwnd);

        g_hTab = CreateWindowExW(0, WC_TABCONTROLW, L"",
            WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS, 0, 0, 10, 10,
            hwnd, (HMENU)IDC_TAB, g_hInst, NULL);
        SendMessageW(g_hTab, WM_SETFONT, (WPARAM)g_font, TRUE);
        TCITEMW ti; memset(&ti, 0, sizeof(ti));
        ti.mask = TCIF_TEXT;
        ti.pszText = (LPWSTR)L"Mods";
        TabCtrl_InsertItem(g_hTab, 0, &ti);
        ti.pszText = (LPWSTR)L"Sauvegardes";
        TabCtrl_InsertItem(g_hTab, 1, &ti);

        // page mods : barre (Ajouter + tri) + panneau de cartes scrollable
        g_nMods = 0;
        g_pageMods[g_nMods++] = MkButton(hwnd, IDC_BADD, L"Ajouter");

        HWND combo = CreateWindowExW(WS_EX_CLIENTEDGE, L"COMBOBOX", L"",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | CBS_DROPDOWNLIST | WS_VSCROLL,
            0, 0, 10, 200, hwnd, (HMENU)IDC_SORTCOMBO, g_hInst, NULL);
        SendMessageW(combo, WM_SETFONT, (WPARAM)g_font, TRUE);
        const wchar_t* sortLabels[] = { L"Nom", L"Categorie", L"Derniere verification",
                                        L"DLL", L"MAJ Thunderstore", L"Lien", L"Note" };
        for (int si = 0; si < 7; ++si) SendMessageW(combo, CB_ADDSTRING, 0, (LPARAM)sortLabels[si]);
        SendMessageW(combo, CB_SETCURSEL, 0, 0);
        g_pageMods[g_nMods++] = combo;

        HWND dirBtn = MkButton(hwnd, IDC_SORTDIR, L"^ Croissant");
        g_pageMods[g_nMods++] = dirBtn;

        WNDCLASSEXW cwc; memset(&cwc, 0, sizeof(cwc));
        cwc.cbSize = sizeof(cwc);
        cwc.lpfnWndProc = CardsHostProc;
        cwc.hInstance = g_hInst;
        cwc.hCursor = LoadCursor(NULL, IDC_ARROW);
        cwc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
        cwc.lpszClassName = L"ValModsCards";
        RegisterClassExW(&cwc);
        g_hCardsHost = CreateWindowExW(WS_EX_CLIENTEDGE, L"ValModsCards", L"",
            WS_CHILD | WS_VISIBLE | WS_VSCROLL | WS_CLIPCHILDREN,
            0, 0, 10, 10, hwnd, NULL, g_hInst, NULL);
        g_pageMods[g_nMods++] = g_hCardsHost;

        // icone par defaut (mods sans logo choisi) : chargee une seule fois,
        // reutilisee par toutes les cartes qui n'ont pas d'iconPath valide.
        g_defaultIcon = MakeDefaultIcon(40);

        // info-bulles : repond a "a quoi sert ce bouton ?" directement dans
        // l'interface. Celles des boutons de chaque carte sont ajoutees a
        // la volee dans RefillMods (elles n'existent qu'apres la creation
        // des cartes correspondantes).
        g_hTooltip = CreateWindowExW(WS_EX_TOPMOST, TOOLTIPS_CLASSW, NULL,
            WS_POPUP | TTS_ALWAYSTIP | TTS_NOPREFIX,
            CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
            hwnd, NULL, g_hInst, NULL);
        if (g_hTooltip) {
            SetWindowPos(g_hTooltip, HWND_TOPMOST, 0, 0, 0, 0,
                SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
            AddTip(GetDlgItem(hwnd, IDC_BADD), L"Ajoute un nouveau mod a la liste");
            AddTip(combo,  L"Choisit le critere de tri de la liste");
            AddTip(dirBtn, L"Inverse l'ordre de tri (croissant / decroissant)");
        }


        // page sauvegardes
        g_nSaves = 0;
        HWND l1 = CreateWindowExW(0, L"STATIC", L"Mondes", WS_CHILD, 0, 0, 10, 10,
            hwnd, (HMENU)IDC_LBL1, g_hInst, NULL);
        SendMessageW(l1, WM_SETFONT, (WPARAM)g_font, TRUE);
        g_pageSaves[g_nSaves++] = l1;
        g_hWorlds = MkList(hwnd, IDC_WORLDS);
        g_pageSaves[g_nSaves++] = g_hWorlds;
        AddCol(g_hWorlds, 0, L"Monde", 300);
        AddCol(g_hWorlds, 1, L"Taille", 100);
        AddCol(g_hWorlds, 2, L"Modifie le", 160);

        HWND l2 = CreateWindowExW(0, L"STATIC", L"Personnages", WS_CHILD, 0, 0, 10, 10,
            hwnd, (HMENU)IDC_LBL2, g_hInst, NULL);
        SendMessageW(l2, WM_SETFONT, (WPARAM)g_font, TRUE);
        g_pageSaves[g_nSaves++] = l2;
        g_hChars = MkList(hwnd, IDC_CHARS);
        g_pageSaves[g_nSaves++] = g_hChars;
        AddCol(g_hChars, 0, L"Personnage", 300);
        AddCol(g_hChars, 1, L"Taille", 100);
        AddCol(g_hChars, 2, L"Modifie le", 160);

        g_pageSaves[g_nSaves++] = MkButton(hwnd, IDC_BREFRESH,  L"Rafraichir");
        g_pageSaves[g_nSaves++] = MkButton(hwnd, IDC_BOPENSAVE, L"Ouvrir les saves");
        g_pageSaves[g_nSaves++] = MkButton(hwnd, IDC_BBACKUP,   L"Backup selection");
        g_pageSaves[g_nSaves++] = MkButton(hwnd, IDC_BOPENBK,   L"Ouvrir les backups");

        LoadData();
        RefillMods();
        ShowPage(0);
        UpdateTitle();
        return 0;
    }

    case WM_SIZE:
        LayoutAll(hwnd);
        return 0;

    case WM_GETMINMAXINFO: {
        MINMAXINFO* mm = (MINMAXINFO*)lp;
        // 900 : marge confortable pour les 8 boutons de chaque carte de mod.
        mm->ptMinTrackSize.x = 900;
        mm->ptMinTrackSize.y = 480;
        return 0;
    }

    case WM_COMMAND: {
        int cid = LOWORD(wp);

        // boutons dynamiques d'une carte de mod (voir RA_BASE/RA_COUNT) :
        // id = RA_BASE + index_ligne * RA_COUNT + action. On POSTE l'action
        // (voir le commentaire pres de WM_APP_ROWACTION) sauf "..." qui se
        // contente d'ouvrir un menu et ne detruit aucune fenetre.
        if (cid >= RA_BASE) {
            int raw = cid - RA_BASE;
            int action = raw % RA_COUNT;
            int rowIdx = raw / RA_COUNT;
            if (action == RA_MORE) ShowRowOverflowMenu(hwnd, rowIdx);
            else PostMessageW(hwnd, WM_APP_ROWACTION, (WPARAM)action, (LPARAM)rowIdx);
            return 0;
        }
        if (cid == IDC_SORTCOMBO && HIWORD(wp) == CBN_SELCHANGE) {
            int sel = (int)SendMessageW(GetDlgItem(hwnd, IDC_SORTCOMBO), CB_GETCURSEL, 0, 0);
            static const int SORT_MAP[] = { COL_NAME, COL_CAT, COL_LASTCHECK, COL_DLL,
                                            COL_TSVER, COL_URL, COL_NOTE };
            if (sel >= 0 && sel < 7) { g_sortCol = SORT_MAP[sel]; RefillMods(); }
            return 0;
        }

        switch (cid) {
        case IDC_BADD: ActionAdd(hwnd); return 0;
        case IDC_SORTDIR:
            g_sortAsc = !g_sortAsc;
            SetWindowTextW(GetDlgItem(hwnd, IDC_SORTDIR), g_sortAsc ? L"^ Croissant" : L"v Decroissant");
            RefillMods();
            return 0;

        // menu "..." d'une carte (actions moins frequentes) - egalement
        // POSTEES : le menu a ete ouvert depuis un bouton de carte, on est
        // donc toujours dans la meme pile d'appels imbriquee que ci-dessus.
        case IDM_CTX_LOCATE_DLL:  PostMessageW(hwnd, WM_APP_ROWACTION, RA_MENU_LOCATE_DLL, g_ctxMenuModIndex);  return 0;
        case IDM_CTX_OPEN_DLLDIR: PostMessageW(hwnd, WM_APP_ROWACTION, RA_MENU_OPEN_DLLDIR, g_ctxMenuModIndex); return 0;
        case IDM_CTX_COPY:        PostMessageW(hwnd, WM_APP_ROWACTION, RA_MENU_COPY, g_ctxMenuModIndex);        return 0;
        case IDM_CTX_DELETE:      PostMessageW(hwnd, WM_APP_ROWACTION, RA_MENU_DELETE, g_ctxMenuModIndex);      return 0;

        case IDC_BREFRESH:  RefreshSaves(); return 0;
        case IDC_BOPENSAVE: OpenFolder(hwnd, SavesRoot(), L"sauvegardes"); return 0;
        case IDC_BBACKUP:   BackupSelection(hwnd); return 0;
        case IDC_BOPENBK:   MakeDirs(BackupRoot());
                            OpenFolder(hwnd, BackupRoot(), L"backups"); return 0;

        case IDM_PLUGINS:  OpenFolder(hwnd, PluginsDir(), L"BepInEx\\plugins"); return 0;
        case IDM_CONFIG:   OpenFolder(hwnd, ConfigDir(),  L"BepInEx\\config");  return 0;
        case IDM_BEPINEX:  OpenFolder(hwnd, BepInExDir(), L"BepInEx");          return 0;
        case IDM_GAMEDIR:  OpenFolder(hwnd, g_valheimDir, L"dossier du jeu");   return 0;
        case IDM_SAVES:    OpenFolder(hwnd, SavesRoot(),  L"sauvegardes");      return 0;
        case IDM_DOWNLOADS: MakeDirs(DownloadsRoot());
                            OpenFolder(hwnd, DownloadsRoot(), L"telechargements"); return 0;
        case IDM_OPENDATA: OpenFolder(hwnd, ExeDir(),     L"donnees");          return 0;
        case IDM_SETDIR:   ChooseValheimDir(hwnd); UpdateTitle(); return 0;
        case IDM_EXIT:     DestroyWindow(hwnd); return 0;
        case IDM_ABOUT: {
            std::wstring about =
                L"ValMods " + U2W(VALMODS_VERSION) + L" - gestionnaire manuel de mods Valheim\n\n";
            about +=
                L"Chaque mod est une carte avec ses propres boutons :\n"
                L"Watch          : ouvre la page du mod\n"
                L"Hist.          : ouvre la page des changements / versions\n"
                L"Check+         : ouvre la page du mod ET note la date de verification\n"
                L"OK             : note la date de verification SANS ouvrir de lien\n"
                L"                 (utile si tu as deja verifie ailleurs : Discord du\n"
                L"                 mod, changelog deja ouvert dans un autre onglet...)\n"
                L"TS             : verifie la derniere version sur Thunderstore\n"
                L"DL             : telecharge le zip (demande ou l'enregistrer)\n"
                L"Modif.         : modifie le mod\n"
                L"...            : copier le lien, localiser le DLL, ouvrir son\n"
                L"                 dossier, supprimer\n\n"
                L"Le menu deroulant en haut choisit le critere de tri, le bouton a\n"
                L"cote inverse l'ordre (croissant / decroissant).\n\n"
                L"La ligne de details (categorie / verif / DLL / TS) est coloree :\n"
                L"rouge = probleme (DLL manquant ou mise a jour disponible),\n"
                L"orange = verification ancienne (14+ jours), vert = tout va bien,\n"
                L"gris = jamais verifie.\n\n"
                L"Verif. TS interroge l'API publique de Thunderstore pour connaitre la\n"
                L"derniere version publiee (uniquement pour les mods heberges sur\n"
                L"thunderstore.io - Nexus/GitHub ne sont pas geres).\n\n"
                L"Dans l'editeur, Auto-remplir depuis Thunderstore recupere le nom, la\n"
                L"categorie, le lien historique, la description, la derniere version et\n"
                L"l'icone du mod a partir du lien colle (meme limite : Thunderstore\n"
                L"uniquement). Verif. TS complete aussi la description si elle est vide.\n\n"
                L"Telecharger (DL) propose une boite Enregistrer sous - a extraire\n"
                L"toi-meme dans BepInEx\\plugins, rien n'est installe automatiquement.\n\n"
                L"Donnees : valmods.json, a cote de l'exe.";
            Info(hwnd, about.c_str());
            return 0;
        }
        }
        break;
    }

    // Execution reelle, differee, des actions de carte (voir le commentaire
    // pres de WM_APP_ROWACTION) : on est ici sur une iteration de boucle de
    // messages fraiche, plus aucun bouton n'est en train de se traiter -
    // RefillMods() peut detruire/recreer les cartes sans risque.
    case WM_APP_ROWACTION: {
        int action = (int)wp;
        int idx = (int)lp;
        switch (action) {
            case RA_WATCH: ActionOpen(hwnd, idx, false);        break;
            case RA_HIST:  ActionOpenHistory(hwnd, idx);        break;
            case RA_CHECK: ActionOpen(hwnd, idx, true);         break;
            case RA_MARK:  ActionMark(hwnd, idx);               break;
            case RA_TS:    ActionCheckThunderstore(hwnd, idx);  break;
            case RA_DL:    ActionDownloadLatest(hwnd, idx);     break;
            case RA_EDIT:  ActionEdit(hwnd, idx);               break;
            case RA_MENU_COPY:        ActionCopy(hwnd, idx);        break;
            case RA_MENU_LOCATE_DLL:  ActionLocateDll(hwnd, idx);   break;
            case RA_MENU_OPEN_DLLDIR: ActionOpenDllDir(hwnd, idx);  break;
            case RA_MENU_DELETE:      ActionDelete(hwnd, idx);      break;
        }
        return 0;
    }

    case WM_NOTIFY: {
        NMHDR* nh = (NMHDR*)lp;
        if (nh->hwndFrom == g_hTab && nh->code == TCN_SELCHANGE) {
            ShowPage(TabCtrl_GetCurSel(g_hTab));
            return 0;
        }
        if (nh->hwndFrom == g_hWorlds || nh->hwndFrom == g_hChars) {
            if (nh->code == NM_CLICK || nh->code == NM_SETFOCUS)
                g_lastListIsWorld = (nh->hwndFrom == g_hWorlds);
            if (nh->code == NM_DBLCLK) {
                bool w = (nh->hwndFrom == g_hWorlds);
                std::vector<SaveEntry>& v = w ? g_worlds : g_chars;
                int sel = ListView_GetNextItem(nh->hwndFrom, -1, LVNI_SELECTED);
                if (sel >= 0 && sel < (int)v.size()) RevealFile(v[sel].file);
                return 0;
            }
        }
        break;
    }

    case WM_DESTROY:
        SaveMods();
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

// GUI app (/SUBSYSTEM:WINDOWS) has no console by default, so plain printf
// goes nowhere. --version reattaches to the parent console (if the exe was
// launched from one, e.g. CI or a terminal) so the CI smoke test - and
// anyone running `valmods.exe --version` from a shell - gets real output,
// then exits immediately without ever creating a window.
static bool HandleVersionFlag() {
    LPWSTR cmd = GetCommandLineW();
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(cmd, &argc);
    bool wantsVersion = false;
    for (int i = 1; i < argc; ++i)
        if (lstrcmpiW(argv[i], L"--version") == 0 || lstrcmpiW(argv[i], L"-v") == 0)
            wantsVersion = true;
    if (argv) LocalFree(argv);
    if (!wantsVersion) return false;

    if (AttachConsole(ATTACH_PARENT_PROCESS)) {
        FILE* f = NULL;
        freopen_s(&f, "CONOUT$", "w", stdout);
        printf("ValMods %s\n", VALMODS_VERSION);
        fflush(stdout);
    }
    return true;
}

// ---------------------------------------------------------------- WinMain
int WINAPI wWinMain(HINSTANCE hInst, HINSTANCE, LPWSTR, int nCmdShow) {
    if (HandleVersionFlag()) return 0;

    g_hInst = hInst;
    OleInitialize(NULL);

    // ICC_WIN95_CLASSES couvre entre autres les tooltips (TOOLTIPS_CLASSW)
    // utilises pour les info-bulles des boutons, en plus des listes/onglets.
    INITCOMMONCONTROLSEX ic;
    ic.dwSize = sizeof(ic);
    ic.dwICC = ICC_WIN95_CLASSES | ICC_STANDARD_CLASSES;
    InitCommonControlsEx(&ic);

    Gdiplus::GdiplusStartupInput gdiplusStartupInput;
    Gdiplus::Status gdiStatus =
        Gdiplus::GdiplusStartup(&g_gdiplusToken, &gdiplusStartupInput, NULL);
    // Si GDI+ echoue a s'initialiser (tres rare), on continue quand meme :
    // GetOrLoadHIcon/MakeDefaultIcon renverront simplement NULL, donc pas
    // d'icone affichee plutot qu'un crash.

    NONCLIENTMETRICSW ncm;
    ncm.cbSize = sizeof(ncm);
    if (SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0))
        g_font = CreateFontIndirectW(&ncm.lfMessageFont);
    if (!g_font) g_font = (HFONT)GetStockObject(DEFAULT_GUI_FONT);

    // variante grasse du meme corps de police, pour le nom du mod sur chaque
    // carte (repli sur g_font si la creation echoue, tres improbable).
    LOGFONTW lfBold; memset(&lfBold, 0, sizeof(lfBold));
    if (GetObjectW(g_font, sizeof(lfBold), &lfBold)) {
        lfBold.lfWeight = FW_BOLD;
        g_fontBold = CreateFontIndirectW(&lfBold);
    }
    if (!g_fontBold) g_fontBold = g_font;

    WNDCLASSEXW wc; memset(&wc, 0, sizeof(wc));
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = MainProc;
    wc.hInstance = hInst;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    wc.lpszClassName = L"ValModsMain";
    wc.hIcon = LoadIcon(NULL, IDI_APPLICATION);
    wc.hIconSm = LoadIcon(NULL, IDI_APPLICATION);
    if (!RegisterClassExW(&wc)) return 1;

    HWND hwnd = CreateWindowExW(0, L"ValModsMain", L"ValMods",
        WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
        CW_USEDEFAULT, CW_USEDEFAULT, 1060, 640,
        NULL, NULL, hInst, NULL);
    if (!hwnd) return 1;

    ShowWindow(hwnd, nCmdShow);
    UpdateWindow(hwnd);

    HACCEL hAcc = NULL;
    MSG msg;
    while (GetMessageW(&msg, NULL, 0, 0) > 0) {
        if (!IsDialogMessageW(hwnd, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }
    (void)hAcc;

    if (!g_iconCache.empty()) {
        for (std::map<std::wstring, HICON>::iterator it = g_iconCache.begin();
             it != g_iconCache.end(); ++it)
            DestroyIcon(it->second);
        g_iconCache.clear();
    }
    if (g_defaultIcon) { DestroyIcon(g_defaultIcon); g_defaultIcon = NULL; }
    if (g_fontBold && g_fontBold != g_font) { DeleteObject(g_fontBold); g_fontBold = NULL; }
    if (gdiStatus == Gdiplus::Ok) Gdiplus::GdiplusShutdown(g_gdiplusToken);
    OleUninitialize();
    return (int)msg.wParam;
}
