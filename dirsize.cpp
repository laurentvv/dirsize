// dirsize.cpp
//
// Usage : dirsize <repertoire> <sortie.csv>
//
// Pour chaque sous-répertoire de premier niveau de <repertoire>, calcule
// récursivement (dans tous les sous-niveaux) :
//   - la taille totale des fichiers (octets)
//   - le nombre de fichiers
//   - la date de modification du fichier le plus ancien
//   - la date de modification du fichier le plus récent
// et écrit le résultat dans un CSV (séparateur ';').
//
// Une ligne TOTAL (racine incluse) est ajoutée en fin de fichier.
//
// Compilation :
//   Windows (MinGW/MSYS2) : g++ -std=c++20 -O2 -static dirsize.cpp -o dirsize.exe
//   Windows (MSVC)        : cl /std:c++20 /EHsc dirsize.cpp
//   Linux / WSL           : g++ -std=c++20 -O2 dirsize.cpp -o dirsize
//   Vieux GCC (CentOS/RHEL 8, GCC 8-9) :
//                          g++ -std=c++17 -O2 dirsize.cpp -o dirsize -lstdc++fs
// (-lstdc++fs est requis sur GCC 8/9, inutile mais sans effet sur GCC >= 10 ;
//  en C++17 la conversion des dates passe par un repli lui aussi exact.)
//
// Notes :
//   - les dates utilisées sont les dates de dernière modification des fichiers ;
//   - les liens symboliques, jonctions NTFS et autres points d'analyse sont
//     ignorés (ni suivis ni comptés : pas de boucles ni doubles comptages) ;
//   - sous Windows le balayage est natif (FindFirstFileExW) : les métadonnées
//     sont lues dans le buffer d'énumération, sans stat() par fichier ;
//   - sous Linux : un seul stat() par fichier, et répertoires déjà visités
//     détectés par (device, inode) — un bind mount en boucle ne bloque pas ;
//   - les répertoires illisibles sont signalés sur stderr et ignorés ;
//   - le CSV est écrit en UTF-8, trié par taille décroissante.

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <sys/stat.h>
#include <functional>
#include <unordered_set>
#endif

namespace fs = std::filesystem;

struct DirStats {
    std::string name;     // chemin relatif au répertoire racine
    std::uintmax_t size = 0;   // somme des tailles des fichiers (octets)
    std::uintmax_t files = 0;  // nombre de fichiers (récursif)
    std::time_t oldest = 0;    // date du fichier le plus ancien
    std::time_t newest = 0;    // date du fichier le plus récent
    bool hasDate = false;      // au moins un fichier daté trouvé
};

// Conversion fs::file_time_type -> time_t (chemin POSIX uniquement : sous
// Windows, toutes les dates passent par la conversion FILETIME entière).
// En C++20 complet (GCC >= 13), chrono::clock_cast fait la conversion exacte.
// Sur les compilateurs plus anciens (ex. GCC 11 d'Ubuntu 22.04), repli sur la
// traduction de l'écart par rapport à "maintenant" : fileNow est échantillonné
// AVANT sysNow, donc l'écart résiduel (quelques µs) est positif et la
// troncature à la seconde reste exacte.
#ifndef _WIN32
static std::time_t toTimeT(const fs::file_time_type& ftime)
{
    using namespace std::chrono;
#if defined(__cpp_lib_chrono) && __cpp_lib_chrono >= 201907L
    return system_clock::to_time_t(clock_cast<system_clock>(ftime));
#else
    const auto fileNow = fs::file_time_type::clock::now();  // échantillonné en premier
    const auto sysNow  = system_clock::now();
    const auto asSystem = time_point_cast<system_clock::duration>(ftime - fileNow + sysNow);
    return system_clock::to_time_t(asSystem);
#endif
}
#endif

static std::string formatDate(std::time_t t)
{
    std::tm tmv{};
#ifdef _WIN32
    // localtime_s (UCRT) refuse les time_t negatifs (dates < 1970) ;
    // on passe par les API Win32 qui courent de 1601 a nos jours.
    if (t < -11644473600LL)  // avant 1601 : hors plage FILETIME
        return {};
    ULARGE_INTEGER u;
    u.QuadPart = static_cast<unsigned long long>(
        static_cast<long long>(t) * 10000000LL + 116444736000000000LL);
    FILETIME ft;
    ft.dwLowDateTime = u.LowPart;
    ft.dwHighDateTime = u.HighPart;
    SYSTEMTIME st;
    if (!FileTimeToSystemTime(&ft, &st) ||
        !SystemTimeToTzSpecificLocalTime(nullptr, &st, &st))
        return {};
    tmv.tm_year = st.wYear - 1900;
    tmv.tm_mon = st.wMonth - 1;
    tmv.tm_mday = st.wDay;
    tmv.tm_hour = st.wHour;
    tmv.tm_min = st.wMinute;
    tmv.tm_sec = st.wSecond;
#else
    if (localtime_r(&t, &tmv) == nullptr)
        return {};
#endif
    char buf[32];
    if (std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &tmv) == 0)
        return {};
    return buf;
}

static std::string toUtf8(const fs::path& p)
{
#ifdef _WIN32
    #if defined(__cpp_lib_char8_t)  // C++20 : u8string() renvoie std::u8string
    const auto u8s = p.u8string();
    return std::string(reinterpret_cast<const char*>(u8s.data()), u8s.size());
    #else
    return p.u8string();  // C++17 : std::string déjà encodé en UTF-8
    #endif
#else
    return p.string();    // sur Linux l'encodage natif est déjà UTF-8
#endif
}

static std::string csvQuote(const std::string& s)
{
    std::string out = "\"";
    for (char c : s) {
        if (c == '"')
            out += "\"\"";
        else
            out += c;
    }
    out += "\"";
    return out;
}

static void accountFile(DirStats& st, std::uintmax_t size, std::time_t t)
{
    st.size += size;
    ++st.files;
    if (!st.hasDate) {
        st.oldest = st.newest = t;
        st.hasDate = true;
    } else {
        if (t < st.oldest) st.oldest = t;
        if (t > st.newest) st.newest = t;
    }
}

#ifdef _WIN32
// FILETIME (100 ns depuis 1601) -> time_t : conversion exacte, sans arrondi.
// Calcul en signé : les dates antérieures à 1970 donnent un FILETIME dont la
// soustraction de l'epoch dépasserait la plage non signée.
static std::time_t fileTimeToTimeT(const FILETIME& ft)
{
    ULARGE_INTEGER u;
    u.LowPart = ft.dwLowDateTime;
    u.HighPart = ft.dwHighDateTime;
    const long long hundredNs = static_cast<long long>(u.QuadPart);
    return static_cast<std::time_t>((hundredNs - 116444736000000000LL) / 10000000LL);
}
#endif

// Fichiers du premier niveau (peu nombreux).
#ifdef _WIN32
// Un seul appel GetFileAttributesExW fournit taille et dates. On évite
// std::filesystem/chrono ici : clock_cast de libstdc++ (MinGW) renvoie -1
// pour les dates antérieures à 1970 (vérifié : fs::last_write_time est
// correct, c'est la conversion chrono qui sature).
static void addFile(const fs::directory_entry& entry, DirStats& st)
{
    WIN32_FILE_ATTRIBUTE_DATA fad;
    if (!GetFileAttributesExW(entry.path().wstring().c_str(),
                              GetFileExInfoStandard, &fad))
        return;
    if (fad.dwFileAttributes & (FILE_ATTRIBUTE_REPARSE_POINT | FILE_ATTRIBUTE_DIRECTORY))
        return;
    ULARGE_INTEGER sz;
    sz.LowPart = fad.nFileSizeLow;
    sz.HighPart = fad.nFileSizeHigh;
    accountFile(st, sz.QuadPart, fileTimeToTimeT(fad.ftLastWriteTime));
}
#else
static void addFile(const fs::directory_entry& entry, DirStats& st)
{
    std::error_code ec;
    const std::uintmax_t sz = entry.file_size(ec);
    if (ec)
        return;
    const auto wt = entry.last_write_time(ec);
    if (ec)
        return;
    accountFile(st, sz, toTimeT(wt));
}
#endif

// Fusionne les stats d'un sous-ensemble dans un accumulateur (pour le TOTAL).
static void mergeInto(DirStats& dst, const DirStats& src)
{
    dst.size += src.size;
    dst.files += src.files;
    if (src.hasDate) {
        if (!dst.hasDate) {
            dst.oldest = src.oldest;
            dst.newest = src.newest;
            dst.hasDate = true;
        } else {
            if (src.oldest < dst.oldest) dst.oldest = src.oldest;
            if (src.newest > dst.newest) dst.newest = src.newest;
        }
    }
}

#ifdef _WIN32
// Jonction NTFS / point d'analyse ? libstdc++ (MinGW) ne les classe PAS comme
// des symlinks — il faut tester l'attribut explicitement, sinon la cible de la
// jonction serait suivie et comptée deux fois.
static bool isReparsePoint(const fs::path& p)
{
    const DWORD attrs = GetFileAttributesW(p.wstring().c_str());
    return attrs != INVALID_FILE_ATTRIBUTES &&
           (attrs & FILE_ATTRIBUTE_REPARSE_POINT) != 0;
}

// Balayage natif : l'énumération FindFirstFileW/FindNextFileW fournit déjà
// taille, date et attributs dans son buffer — aucun appel stat() supplémentaire
// par fichier. Les points d'analyse (jonctions, liens) sont écartés d'emblée.
static void scanRecursive(const fs::path& dir, DirStats& st)
{
    std::wstring pattern = dir.wstring();
    for (wchar_t& c : pattern)
        if (c == L'/')
            c = L'\\';
    if (!pattern.empty() && pattern.back() != L'\\')
        pattern += L'\\';
    pattern += L'*';

    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileExW(pattern.c_str(), FindExInfoBasic, &fd,
                                FindExSearchNameMatch, nullptr,
                                FIND_FIRST_EX_LARGE_FETCH);
    if (h == INVALID_HANDLE_VALUE) {
        std::cerr << "avertissement : lecture impossible de " << toUtf8(dir)
                  << " (erreur Windows " << GetLastError() << ")\n";
        return;
    }
    do {
        const bool isDot = fd.cFileName[0] == L'.' &&
            (fd.cFileName[1] == L'\0' ||
             (fd.cFileName[1] == L'.' && fd.cFileName[2] == L'\0'));
        if (isDot || (fd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT))
            continue;  // "." / ".." et liens symboliques / jonctions : ignorés
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            scanRecursive(dir / fd.cFileName, st);
        } else {
            ULARGE_INTEGER sz;
            sz.LowPart = fd.nFileSizeLow;
            sz.HighPart = fd.nFileSizeHigh;
            accountFile(st, sz.QuadPart, fileTimeToTimeT(fd.ftLastWriteTime));
        }
    } while (FindNextFileW(h, &fd));
    FindClose(h);
}
#else
// Protection anti-cycles : un bind mount peut faire boucler l'arborescence
// (ce n'est pas un lien, le test symlink ne le voit pas). On mémorise les
// (device, inode) des répertoires déjà visités — même approche que find/du.
namespace {
struct DevIno {
    dev_t dev;
    ino_t ino;
    bool operator==(const DevIno& other) const
    { return dev == other.dev && ino == other.ino; }
};
struct DevInoHash {
    std::size_t operator()(const DevIno& k) const noexcept
    {
        const auto h = std::hash<unsigned long long>{};
        return h(static_cast<unsigned long long>(k.dev)) * 1315423911ULL
             ^ h(static_cast<unsigned long long>(k.ino));
    }
};
std::unordered_set<DevIno, DevInoHash> g_visitedDirs;
} // namespace

static bool alreadyScanned(const fs::path& dir)
{
    struct stat sb;
    if (::stat(dir.c_str(), &sb) != 0)
        return false;  // stat indisponible : on tente le scan quand même
    return !g_visitedDirs.insert({sb.st_dev, sb.st_ino}).second;
}

static void scanRecursive(const fs::path& dir, DirStats& st)
{
    if (alreadyScanned(dir))
        return;  // déjà visité : bind mount en amont, ou cycle
    std::error_code ec;
    fs::directory_iterator it(dir, fs::directory_options::skip_permission_denied, ec);
    if (ec) {
        std::cerr << "avertissement : lecture impossible de " << toUtf8(dir)
                  << " (" << ec.message() << ")\n";
        return;
    }
    for (const fs::directory_entry& entry : it) {
        std::error_code lec;
        if (entry.is_symlink(lec))
            continue;
        if (entry.is_directory(lec)) {
            scanRecursive(entry.path(), st);
        } else if (entry.is_regular_file(lec)) {
            // Un seul stat() par fichier : taille et mtime d'un seul appel,
            // st_mtime étant déjà un time_t exact (signé : dates < 1970 OK).
            struct stat sb;
            if (::stat(entry.path().c_str(), &sb) == 0)
                accountFile(st, static_cast<std::uintmax_t>(sb.st_size), sb.st_mtime);
        }
    }
}
#endif

int main(int argc, char* argv[])
{
    if (argc != 3) {
        std::cerr << "Usage : " << argv[0] << " <repertoire> <sortie.csv>\n";
        return 1;
    }

    const fs::path rootPath = fs::absolute(fs::path(argv[1]));
    const fs::path outPath(argv[2]);

    std::error_code ec;
    if (!fs::is_directory(rootPath, ec)) {
        std::cerr << "erreur : " << toUtf8(rootPath) << " n'est pas un repertoire valide\n";
        return 1;
    }

    std::vector<DirStats> rows;
    DirStats rootFiles;  // fichiers poses directement a la racine, hors sous-repertoires

    fs::directory_iterator it(rootPath, fs::directory_options::skip_permission_denied, ec);
    if (ec) {
        std::cerr << "erreur : lecture impossible de " << toUtf8(rootPath)
                  << " (" << ec.message() << ")\n";
        return 1;
    }
    for (const fs::directory_entry& entry : it) {
        std::error_code e2;
        if (entry.is_symlink(e2))
            continue;
        if (entry.is_directory(e2)) {
#ifdef _WIN32
            if (isReparsePoint(entry.path()))
                continue;  // jonction NTFS : jamais suivie
#endif
            DirStats st;
            st.name = toUtf8(entry.path().filename());
            scanRecursive(entry.path(), st);
            std::cout << "  " << st.name << " : " << st.files << " fichier(s), "
                      << st.size << " octets\n";
            rows.push_back(std::move(st));
        } else if (entry.is_regular_file(e2)) {
            addFile(entry, rootFiles);
        }
    }

    // Total = tous les sous-repertoires + fichiers a la racine
    DirStats total;
    total.name = "TOTAL (" + toUtf8(rootPath.filename()) + ")";
    for (const DirStats& st : rows)
        mergeInto(total, st);
    mergeInto(total, rootFiles);

    // Tri par taille decroissante
    std::sort(rows.begin(), rows.end(),
              [](const DirStats& a, const DirStats& b) { return a.size > b.size; });

    std::ofstream out(outPath, std::ios::binary | std::ios::trunc);
    if (!out) {
        std::cerr << "erreur : impossible de creer " << toUtf8(outPath) << "\n";
        return 1;
    }

#ifdef _WIN32
    out << "\xEF\xBB\xBF";  // BOM UTF-8 : accents corrects a l'ouverture dans Excel
#endif
    out << "\"nom rep\";\"taille\";\"nombre de fichiers\";"
           "\"date du fichier le plus ancien\";\"date du fichier le plus recent\"\r\n";

    const auto writeRow = [&out](const DirStats& st) {
        out << csvQuote(st.name) << ';'
            << st.size << ';'
            << st.files << ';'
            << (st.hasDate ? formatDate(st.oldest) : "") << ';'
            << (st.hasDate ? formatDate(st.newest) : "") << "\r\n";
    };
    for (const DirStats& st : rows)
        writeRow(st);
    writeRow(total);

    if (!out) {
        std::cerr << "erreur : echec d'ecriture dans " << toUtf8(outPath) << "\n";
        return 1;
    }

    std::cout << "\n" << rows.size() << " sous-repertoires, " << total.files
              << " fichiers, " << total.size << " octets au total\n"
              << "CSV ecrit dans " << toUtf8(outPath) << "\n";
    return 0;
}
