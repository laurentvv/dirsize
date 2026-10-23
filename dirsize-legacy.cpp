// dirsize-legacy.cpp
//
// Variante pour compilateurs anciens sans C++17 (GCC 4.8.5 de CentOS 7/RHEL 7,
// en production, sans mise a jour) : C++11 uniquement, aucune dependance a
// std::filesystem — l'acces au systeme de fichiers passe par dirent.h/lstat.
//
// Le comportement et la sortie CSV sont identiques a dirsize.cpp sur Linux :
//   - taille et nombre de fichiers en recursif pour chaque sous-repertoire
//     de premier niveau, dates (modification) la plus ancienne / plus recente ;
//   - liens symboliques ignores ;
//   - repertoires deja visites memorises par (device, inode) : un bind mount
//     en boucle ne bloque pas le scan, un arbre monte deux fois ne compte
//     qu'une fois (meme approche que find/du) ;
//   - dates anterieures a 1970 supportees (st_mtime est deja un time_t signe) ;
//   - CSV point-virgule, UTF-8 sans BOM, fin de lignes CRLF, tri par taille
//     decroissante, ligne TOTAL finale.
//
// Compilation :
//   g++ -std=c++11 -O2 dirsize-legacy.cpp -o dirsize
//
// Usage : dirsize <repertoire> <sortie.csv>

#include <algorithm>
#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <fstream>
#include <functional>
#include <iostream>
#include <string>
#include <unordered_set>
#include <vector>

#include <dirent.h>
#include <sys/stat.h>
#include <sys/types.h>

struct DirStats {
    std::string name;          // nom du sous-repertoire (premier niveau)
    std::uintmax_t size;       // somme des tailles des fichiers (octets)
    std::uintmax_t files;      // nombre de fichiers (recursif)
    std::time_t oldest;        // date du fichier le plus ancien
    std::time_t newest;        // date du fichier le plus recent
    bool hasDate;              // au moins un fichier date trouve

    DirStats()
        : size(0), files(0), oldest(0), newest(0), hasDate(false) {}
};

// Repertoires deja visites : protection contre les cycles de bind mounts
// (ce ne sont pas des liens, un test symlink ne les voit pas).
namespace {
struct DevIno {
    dev_t dev;
    ino_t ino;
    bool operator==(const DevIno& other) const
    { return dev == other.dev && ino == other.ino; }
};
struct DevInoHash {
    std::size_t operator()(const DevIno& k) const
    {
        const std::hash<unsigned long long> h;
        return h(static_cast<unsigned long long>(k.dev)) * 1315423911ULL
             ^ h(static_cast<unsigned long long>(k.ino));
    }
};
std::unordered_set<DevIno, DevInoHash> g_visitedDirs;
} // namespace

static std::string joinPath(const std::string& dir, const std::string& name)
{
    if (!dir.empty() && dir[dir.size() - 1] != '/')
        return dir + "/" + name;
    return dir + name;
}

static std::string baseNameOf(std::string p)
{
    while (p.size() > 1 && p[p.size() - 1] == '/')
        p.erase(p.size() - 1);
    const std::size_t pos = p.find_last_of('/');
    const std::string base = (pos == std::string::npos) ? p : p.substr(pos + 1);
    return base.empty() ? p : base;
}

static std::string formatDate(std::time_t t)
{
    std::tm tmv;
    std::memset(&tmv, 0, sizeof(tmv));
    if (localtime_r(&t, &tmv) == NULL)
        return {};  // hors plage : champ laisse vide
    char buf[32];
    if (std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &tmv) == 0)
        return {};
    return buf;
}

static std::string csvQuote(const std::string& s)
{
    std::string out = "\"";
    for (std::size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '"')
            out += "\"\"";
        else
            out += s[i];
    }
    out += "\"";
    return out;
}

static void accountFile(DirStats& st, std::uintmax_t size, std::time_t mtime)
{
    st.size += size;
    ++st.files;
    if (!st.hasDate) {
        st.oldest = st.newest = mtime;
        st.hasDate = true;
    } else {
        if (mtime < st.oldest) st.oldest = mtime;
        if (mtime > st.newest) st.newest = mtime;
    }
}

// Un seul lstat() par entree : classification (lien ? repertoire ? fichier
// regulier ?), taille, date et couple (device, inode) d'un seul appel.
static void scanRecursive(const std::string& dir, DirStats& st)
{
    DIR* d = opendir(dir.c_str());
    if (d == NULL) {
        std::cerr << "avertissement : lecture impossible de " << dir
                  << " (" << std::strerror(errno) << ")\n";
        return;
    }
    struct dirent* e;
    while ((e = readdir(d)) != NULL) {
        const std::string name(e->d_name);
        if (name == "." || name == "..")
            continue;
        const std::string full = joinPath(dir, name);
        struct stat sb;
        if (::lstat(full.c_str(), &sb) != 0)
            continue;  // entree illisible : ignoree
        if (S_ISLNK(sb.st_mode))
            continue;  // lien symbolique : ni suivi ni compte
        if (S_ISDIR(sb.st_mode)) {
            const DevIno key = { sb.st_dev, sb.st_ino };
            if (g_visitedDirs.insert(key).second)
                scanRecursive(full, st);
            // sinon : deja visite (bind mount en amont) -> ignore
        } else if (S_ISREG(sb.st_mode)) {
            accountFile(st, static_cast<std::uintmax_t>(sb.st_size), sb.st_mtime);
        }
    }
    closedir(d);
}

static void writeCsv(const std::string& outPath, const std::vector<DirStats>& rows,
                     const DirStats& total)
{
    std::ofstream out(outPath.c_str(), std::ios::binary | std::ios::trunc);
    if (!out) {
        std::cerr << "erreur : impossible de creer " << outPath << "\n";
        std::exit(1);
    }
    out << "nom_repertoire;taille;nb_fichier;date_fichier_old;date_fichier_recent\r\n";
    for (std::size_t i = 0; i < rows.size(); ++i) {
        out << csvQuote(rows[i].name) << ';'
            << rows[i].size << ';'
            << rows[i].files << ';'
            << (rows[i].hasDate ? formatDate(rows[i].oldest) : "") << ';'
            << (rows[i].hasDate ? formatDate(rows[i].newest) : "") << "\r\n";
    }
    out << csvQuote(total.name) << ';'
        << total.size << ';'
        << total.files << ';'
        << (total.hasDate ? formatDate(total.oldest) : "") << ';'
        << (total.hasDate ? formatDate(total.newest) : "") << "\r\n";
    out.flush();
    if (!out) {
        std::cerr << "erreur : echec d'ecriture dans " << outPath << "\n";
        std::exit(1);
    }
    out.close();
    if (!out) {
        std::cerr << "erreur : echec de fermeture de " << outPath << "\n";
        std::exit(1);
    }
}

int main(int argc, char* argv[])
{
    if (argc != 3) {
        std::cerr << "Usage : " << argv[0] << " <repertoire> <sortie.csv>\n";
        return 1;
    }

    const std::string rootPath = argv[1];
    const std::string outPath = argv[2];

    struct stat sb;
    if (::stat(rootPath.c_str(), &sb) != 0 || !S_ISDIR(sb.st_mode)) {
        // stat suit les liens : la racine demandee explicitement est acceptee
        std::cerr << "erreur : " << rootPath << " n'est pas un repertoire valide\n";
        return 1;
    }

    std::vector<DirStats> rows;
    DirStats rootFiles;  // fichiers poses directement a la racine

    DIR* d = opendir(rootPath.c_str());
    if (d == NULL) {
        std::cerr << "erreur : lecture impossible de " << rootPath
                  << " (" << std::strerror(errno) << ")\n";
        return 1;
    }
    struct dirent* e;
    while ((e = readdir(d)) != NULL) {
        const std::string name(e->d_name);
        if (name == "." || name == "..")
            continue;
        const std::string full = joinPath(rootPath, name);
        struct stat esb;
        if (::lstat(full.c_str(), &esb) != 0)
            continue;
        if (S_ISLNK(esb.st_mode))
            continue;
        if (S_ISDIR(esb.st_mode)) {
            const DevIno key = { esb.st_dev, esb.st_ino };
            if (!g_visitedDirs.insert(key).second)
                continue;  // deja visite (bind mount en amont)
            DirStats st;
            st.name = name;
            scanRecursive(full, st);
            std::cout << "  " << st.name << " : " << st.files
                      << " fichier(s), " << st.size << " octets\n";
            rows.push_back(st);
        } else if (S_ISREG(esb.st_mode)) {
            accountFile(rootFiles, static_cast<std::uintmax_t>(esb.st_size),
                        esb.st_mtime);
        }
    }
    closedir(d);

    DirStats total;
    total.name = "TOTAL (" + baseNameOf(rootPath) + ")";
    for (std::size_t i = 0; i < rows.size(); ++i) {
        total.size += rows[i].size;
        total.files += rows[i].files;
        if (rows[i].hasDate) {
            if (!total.hasDate) {
                total.oldest = rows[i].oldest;
                total.newest = rows[i].newest;
                total.hasDate = true;
            } else {
                if (rows[i].oldest < total.oldest) total.oldest = rows[i].oldest;
                if (rows[i].newest > total.newest) total.newest = rows[i].newest;
            }
        }
    }
    total.size += rootFiles.size;
    total.files += rootFiles.files;
    if (rootFiles.hasDate) {
        if (!total.hasDate) {
            total.oldest = rootFiles.oldest;
            total.newest = rootFiles.newest;
            total.hasDate = true;
        } else {
            if (rootFiles.oldest < total.oldest) total.oldest = rootFiles.oldest;
            if (rootFiles.newest > total.newest) total.newest = rootFiles.newest;
        }
    }

    std::sort(rows.begin(), rows.end(),
              [](const DirStats& a, const DirStats& b) { return a.size > b.size; });

    writeCsv(outPath, rows, total);

    std::cout << "\n" << rows.size() << " sous-repertoires, " << total.files
              << " fichiers, " << total.size << " octets au total\n"
              << "CSV ecrit dans " << outPath << "\n";
    return 0;
}
