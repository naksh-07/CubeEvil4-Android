// CubeEvil4 Studio, step S1 (Mods, 19.11.2026; port/docs/studio-plan.md): the host files of the debug tools.
//
// The game's tools read and write files on the development PC through the SN Systems file server (game/file.cpp:
// PCopen, PCread ... rooted at d:\bio4). The motion viewer (debug menu 17, Tools/t_mv.cpp with tools/db_mod.cpp) reads
// Room/Em/mot_tbl.txt there and then the models (BIN, TPL) and motions (FCV) it lists, one file each; the sequence
// editor (t_motseq.cpp) saves there. Until now these calls were stubs and the viewer stopped at once.
//
// Here the file server is <data>/studio (the folder next to disc0/disc1; it switches the host files on, patch 0930):
//  - a real file there is read and written as on the PC (folders made as needed);
//  - "@/<file on the disc>/eNNN" is entry NNN of that archive of the disc (a mod pack's file when one replaces it),
//    converted to host byte order as the game converts it, "@/<file>/mNNN" its NNN-th motion (FCV entry);
//  - Room/Em/mot_tbl.txt, when the folder has none, is made from the disc's em/ archives: a model set per BIN entry
//    (with the archive's next texture palette after it, else the last one before it) and the archive's motions,
//    numbered (the viewer's motion number). A copy goes to studio/mot_tbl.generated.txt.
#include "re4_runtime.h"

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <string>
#include <vector>

namespace {

const int kFds = 16;
const int kEntries = 4096;
const char kTable[] = "room/em/mot_tbl.txt";

struct Fd {
    bool used;
    FILE* file;                      // a real file of the folder
    std::vector<unsigned char> mem;  // a virtual one
    long pos;
};

Fd gFd[kFds];

// the archive last opened (the viewer reads its model, palette and motions in turn)
std::string gArcName;
std::vector<unsigned char> gArcBody;
std::vector<Re4StudioEntry> gArcEntries;

std::string gCatalog;
bool gCatalogMade;

std::string root()
{
    return std::string(re4_data_dir()) + "/studio";
}

bool isDir(const std::string& path)
{
    DIR* d = opendir(path.c_str());  // opendir rather than stat (dvd.cpp: the i686 test sysroot)
    if (!d) {
        return false;
    }
    closedir(d);
    return true;
}

// d:\bio4\Room\Em\x.bin, d:/bio4/..., Room/Em/x.bin -> Room/Em/x.bin; "@/..." stays
std::string normalize(const char* path)
{
    std::string p(path ? path : "");
    for (char& c : p) {
        if (c == '\\') {
            c = '/';
        }
    }
    if (p.size() >= 2 && isalpha((unsigned char) p[0]) && p[1] == ':') {
        p.erase(0, 2);
    }
    while (!p.empty() && p[0] == '/') {
        p.erase(0, 1);
    }
    if (p.size() >= 5 && strncasecmp(p.c_str(), "bio4/", 5) == 0) {
        p.erase(0, 5);
    }
    return p;
}

std::string lower(std::string s)
{
    for (char& c : s) {
        c = (char) tolower((unsigned char) c);
    }
    return s;
}

// a real file of the folder: as written, else with any case of its parts (the PC's file system ignores it)
std::string realPath(const std::string& rel)
{
    std::string exact = root() + "/" + rel;
    FILE* f = fopen(exact.c_str(), "rb");
    if (f) {
        fclose(f);
        return exact;
    }
    std::string at = root();
    size_t from = 0;
    while (from <= rel.size()) {
        size_t slash = rel.find('/', from);
        std::string part = rel.substr(from, slash == std::string::npos ? std::string::npos : slash - from);
        DIR* d = opendir(at.c_str());
        if (!d) {
            return exact;
        }
        std::string found;
        while (dirent* e = readdir(d)) {
            if (strcasecmp(e->d_name, part.c_str()) == 0) {
                found = e->d_name;
                break;
            }
        }
        closedir(d);
        if (found.empty()) {
            return exact;
        }
        at += "/" + found;
        if (slash == std::string::npos) {
            break;
        }
        from = slash + 1;
    }
    return at;
}

void makeFolders(const std::string& file)
{
    for (size_t i = root().size() + 1; i < file.size(); i++) {
        if (file[i] == '/') {
            mkdir(file.substr(0, i).c_str(), 0775);
        }
    }
}

bool loadArchive(const std::string& rel)
{
    if (rel == gArcName && !gArcBody.empty()) {
        return true;
    }
    gArcName.clear();
    gArcBody.clear();
    gArcEntries.assign(kEntries, Re4StudioEntry());
    unsigned len = 0;
    int count = 0;
    unsigned char* body = re4_studio_archive(rel.c_str(), &len, gArcEntries.data(), kEntries, &count);
    if (!body) {
        re4_log("studio: %s is no archive of the disc (or it is packed)", rel.c_str());
        return false;
    }
    gArcBody.assign(body, body + len);
    free(body);
    gArcEntries.resize((size_t) count);
    gArcName = rel;
    re4_log("studio: %s: %d entries, %u bytes, converted", rel.c_str(), count, len);
    return true;
}

// "@/em/pl08.drs/e012" (entry 12) or ".../m005" (the 6th FCV entry)
bool virtualFile(const std::string& path, std::vector<unsigned char>& out)
{
    const size_t slash = path.rfind('/');
    if (path.size() < 4 || slash == std::string::npos || slash < 3 || slash + 2 > path.size()) {
        return false;
    }
    const std::string rel = path.substr(2, slash - 2);
    const char what = (char) tolower((unsigned char) path[slash + 1]);
    char* end = 0;
    const long no = strtol(path.c_str() + slash + 2, &end, 10);
    if ((what != 'e' && what != 'm') || no < 0 || !end || *end || !loadArchive(rel)) {
        return false;
    }
    long index = what == 'e' ? no : -1;
    if (what == 'm') {
        long seen = 0;
        for (size_t i = 0; i < gArcEntries.size(); i++) {
            if (strncasecmp(gArcEntries[i].kind, "FCV", 3) == 0 && gArcEntries[i].length && seen++ == no) {
                index = (long) i;
                break;
            }
        }
    }
    if (index < 0 || (size_t) index >= gArcEntries.size() || !gArcEntries[index].length) {
        return false;
    }
    const Re4StudioEntry& e = gArcEntries[index];
    if ((size_t) e.start + e.length > gArcBody.size()) {
        return false;
    }
    out.assign(gArcBody.begin() + e.start, gArcBody.begin() + e.start + e.length);
    return true;
}

void line(std::string& out, const std::string& text)
{
    out += text;
    out += "\r\n";  // mottblGetLine ends a line at CR LF only
}

// The viewer's table (tools/db_mod.cpp mottblInit: units _SET, _BIN, _TPL, _FCV, _ONTO of "name {" ... "}" blocks;
// a set's line names its bin, tpl and motion blocks; the motion file's '#' run takes the motion number).
const std::string& catalog()
{
    if (gCatalogMade) {
        return gCatalog;
    }
    gCatalogMade = true;
    std::vector<char> list(256 * 1024);
    const int files = re4_studio_disc_files("em", list.data(), (int) list.size());
    std::string sets, bins, tpls, mots;
    std::vector<std::string> setNames;
    std::vector<std::string> setLines;
    std::vector<Re4StudioEntry> table(kEntries);
    int models = 0, archives = 0;
    for (const char* p = list.data(); *p;) {
        const char* nl = strchr(p, '\n');
        std::string rel(p, nl ? (size_t) (nl - p) : strlen(p));
        p = nl ? nl + 1 : p + strlen(p);
        const std::string low = lower(rel);
        if (low.size() < 5 || (low.compare(low.size() - 4, 4, ".drs") != 0 && low.compare(low.size() - 4, 4, ".das") != 0)) {
            continue;
        }
        const int n = re4_studio_archive_table(rel.c_str(), table.data(), kEntries);
        if (n <= 0) {
            continue;
        }
        std::string stem = low.substr(low.find('/') + 1);
        stem = stem.substr(0, stem.size() - 4);
        int fcv = 0;
        for (int i = 0; i < n; i++) {
            fcv += strncasecmp(table[i].kind, "FCV", 3) == 0 && table[i].length;
        }
        const std::string motName = fcv ? stem + "_m" : "null";
        if (fcv) {
            line(mots, motName + " {");
            line(mots, "\t@/" + rel + "/m###");
            line(mots, "}");
        }
        archives++;
        for (int i = 0; i < n; i++) {
            if (strncasecmp(table[i].kind, "BIN", 3) != 0 || !table[i].length) {
                continue;
            }
            int tpl = -1;
            for (int k = i + 1; k < n && tpl < 0; k++) {
                if (strncasecmp(table[k].kind, "TPL", 3) == 0 && table[k].length) {
                    tpl = k;
                }
            }
            for (int k = i - 1; k >= 0 && tpl < 0; k--) {
                if (strncasecmp(table[k].kind, "TPL", 3) == 0 && table[k].length) {
                    tpl = k;
                }
            }
            if (tpl < 0) {
                continue;
            }
            char name[64], file[256];
            snprintf(name, sizeof(name), "%s_%03d", stem.c_str(), i);
            snprintf(file, sizeof(file), "\t@/%s/e%03d", rel.c_str(), i);
            line(bins, std::string(name) + "b {");
            line(bins, file);
            line(bins, "}");
            snprintf(file, sizeof(file), "\t@/%s/e%03d", rel.c_str(), tpl);
            line(tpls, std::string(name) + "t {");
            line(tpls, file);
            line(tpls, "}");
            setNames.push_back(name);
            setLines.push_back(std::string(name) + "b, " + name + "t, " + motName);
            models++;
        }
    }
    // the viewer's type filter (L / R) looks for the next set of each kind and would look for ever without one
    static const char* const kinds[] = {"pl", "wep", "em", "obm", "et", "idm", "itm", "pcs"};
    const size_t real = setNames.size();
    for (const char* kind : kinds) {
        bool has = false;
        for (size_t i = 0; i < real && !has; i++) {
            has = setNames[i].compare(0, strlen(kind), kind) == 0;
        }
        if (!has && real) {
            setNames.push_back(std::string(kind) + "_none");
            setLines.push_back(setLines[0]);
        }
    }
    std::string& out = gCatalog;
    out.clear();
    line(out, "// CubeEvil4 Studio: made from the disc's em/ archives (runtime/studio_host.cpp)");
    line(out, "// put your own Room/Em/mot_tbl.txt into the studio folder to use it instead");
    line(out, "_SET {");
    for (size_t i = 0; i < setNames.size(); i++) {
        line(out, setNames[i] + " {");
        line(out, "\t" + setLines[i]);
        line(out, "}");
    }
    line(out, "}");
    line(out, "_BIN {");
    out += bins;
    line(out, "}");
    line(out, "_TPL {");
    out += tpls;
    line(out, "}");
    line(out, "_FCV {");
    out += mots;
    line(out, "}");
    line(out, "_ONTO {");
    line(out, "}");
    re4_log("studio: viewer table made: %d models of %d archives (%d files in em/), %u bytes", models, archives, files,
            (unsigned) out.size());
    const std::string copy = root() + "/mot_tbl.generated.txt";
    if (FILE* f = fopen(copy.c_str(), "wb")) {
        fwrite(out.data(), 1, out.size(), f);
        fclose(f);
    }
    return out;
}

int newFd()
{
    for (int i = 0; i < kFds; i++) {
        if (!gFd[i].used) {
            gFd[i].used = true;
            gFd[i].file = 0;
            gFd[i].mem.clear();
            gFd[i].pos = 0;
            return i + 1;
        }
    }
    re4_log("studio: more than %d host files open", kFds);
    return -1;
}

Fd* fdOf(int fd)
{
    return fd >= 1 && fd <= kFds && gFd[fd - 1].used ? &gFd[fd - 1] : 0;
}

int hostOpen(const char* path, bool write)
{
    if (!re4_host_files() || !path) {
        return -1;
    }
    if (strncmp(path, "SETROOT:", 8) == 0) {
        return 0;  // the root is the studio folder
    }
    const std::string rel = normalize(path);
    if (rel.empty()) {
        return -1;
    }
    if (!write && rel.compare(0, 2, "@/") == 0) {
        std::vector<unsigned char> data;
        if (!virtualFile(rel, data)) {
            re4_log("studio: %s: no such entry", rel.c_str());
            return -1;
        }
        const int fd = newFd();
        if (fd > 0) {
            gFd[fd - 1].mem.swap(data);
        }
        return fd;
    }
    const std::string real = realPath(rel);
    if (write) {
        makeFolders(real);
    }
    FILE* f = fopen(real.c_str(), write ? "wb" : "rb");
    if (!f && !write && lower(rel) == kTable) {
        const std::string& table = catalog();
        if (table.empty()) {
            return -1;
        }
        const int fd = newFd();
        if (fd > 0) {
            gFd[fd - 1].mem.assign(table.begin(), table.end());
        }
        return fd;
    }
    if (!f) {
        return -1;
    }
    const int fd = newFd();
    if (fd < 0) {
        fclose(f);
        return -1;
    }
    gFd[fd - 1].file = f;
    re4_log("studio: %s %s", write ? "writing" : "reading", rel.c_str());
    return fd;
}

}  // namespace

extern "C" {

// Patch 0930 (game/file.cpp): the host file calls work while the studio folder exists.
int re4_host_files(void)
{
    return isDir(root()) ? 1 : 0;
}

int PCinit()
{
    return 0;
}

int PCopen(const char* path, int flags, int mode)
{
    (void) mode;
    return hostOpen(path, flags != 0);
}

int PCcreat(const char* path, int mode)
{
    (void) mode;
    return hostOpen(path, true);
}

int PCclose(int fd)
{
    Fd* f = fdOf(fd);
    if (!f) {
        return -1;
    }
    if (f->file) {
        fclose(f->file);
    }
    f->file = 0;
    std::vector<unsigned char>().swap(f->mem);
    f->used = false;
    return 0;
}

int PCread(int fd, void* buf, int size)
{
    Fd* f = fdOf(fd);
    if (!f || size < 0 || !buf) {
        return -1;
    }
    if (f->file) {
        return (int) fread(buf, 1, (size_t) size, f->file);
    }
    long left = (long) f->mem.size() - f->pos;
    const int n = left <= 0 ? 0 : (size < left ? size : (int) left);
    if (n > 0) {
        memcpy(buf, f->mem.data() + f->pos, (size_t) n);
        f->pos += n;
    }
    return n;
}

int PCwrite(int fd, const void* buf, int size)
{
    Fd* f = fdOf(fd);
    if (!f || !f->file || size < 0 || !buf) {
        return -1;
    }
    return (int) fwrite(buf, 1, (size_t) size, f->file);
}

int PClseek(int fd, int offset, int whence)
{
    Fd* f = fdOf(fd);
    if (!f) {
        return -1;
    }
    if (f->file) {
        if (fseek(f->file, offset, whence == 1 ? SEEK_CUR : whence == 2 ? SEEK_END : SEEK_SET) != 0) {
            return -1;
        }
        return (int) ftell(f->file);
    }
    long at = whence == 1 ? f->pos + offset : whence == 2 ? (long) f->mem.size() + offset : offset;
    if (at < 0) {
        return -1;
    }
    f->pos = at;
    return (int) at;
}

}  // extern "C"
