// Prueba autónoma del motor (sin Geode):
//   g++ -O2 -std=c++20 -Iinclude -Isrc/engine tests/engine_test.cpp -o engine_test
//   ./engine_test imagen.png [presupuesto] [carpeta_salida] [metodo(0-17)|all]
// Variables de entorno:
//   GDX_MAINMODE=0..3   métodos 1-8: 0 HSV color, 1 canales de color, 2 gris HSV, 3 gris canales
//   GDX_LIGHTCOLOR=1    estilos 9-18 en color (base de color + tinta del mismo tono, canales)
//   GDX_PALETTE=N       canales de la paleta de los estilos ligeros   GDX_MAINPAL=N  la de los métodos principales
//   GDX_SPECIAL=1       estilos ligeros en blanco y negro con los canales especiales 1010/1011
//   GDX_CHUNK=N         caracteres máximos por objeto de texto        GDX_ASPECT=x   alto/ancho de carácter
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

#include <cstdlib>
#include <cstring>
#include <set>
#include <sstream>
#include "engine.hpp"
#include "gdexport.hpp"

using namespace gdx;

static Image loadImage(const char* path) {
    int w, h, c;
    unsigned char* d = stbi_load(path, &w, &h, &c, 4);
    Image im;
    if (!d) return im;
    im.w = w; im.h = h; im.rgba.assign(d, d + size_t(w) * h * 4);
    stbi_image_free(d);
    return im;
}

static void savePreview(const char* path, const Image& render, bool checker) {
    std::vector<uint8_t> out(size_t(render.w) * render.h * 3);
    for (int y = 0; y < render.h; ++y)
        for (int x = 0; x < render.w; ++x) {
            const uint8_t* p = render.at(x, y);
            uint8_t bg = checker ? (((x / 4 + y / 4) & 1) ? 200 : 140) : 0;
            float a = p[3] / 255.f;
            for (int k = 0; k < 3; ++k) out[(size_t(y) * render.w + x) * 3 + k] = uint8_t(p[k] * a + bg * (1 - a));
        }
    stbi_write_png(path, render.w, render.h, 3, out.data(), render.w * 3);
}

static void hsvToRgb(double h, double s, double v, uint8_t& R, uint8_t& G, uint8_t& B) {
    if (h < 0) h += 360;
    double c = v * s, hp = h / 60.0, x = c * (1 - std::abs(std::fmod(hp, 2.0) - 1)), r = 0, g = 0, b = 0;
    if (hp < 1) { r = c; g = x; } else if (hp < 2) { r = x; g = c; } else if (hp < 3) { g = c; b = x; }
    else if (hp < 4) { g = x; b = c; } else if (hp < 5) { r = x; b = c; } else { r = c; b = x; }
    double m = v - c;
    R = uint8_t(std::lround((r + m) * 255)); G = uint8_t(std::lround((g + m) * 255)); B = uint8_t(std::lround((b + m) * 255));
}

static std::string b64dec(const std::string& in) {
    static const std::string T = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out; uint32_t v = 0; int bits = 0;
    for (char c : in) { if (c == '=') break; size_t k = T.find(c); if (k == std::string::npos) continue; v = (v << 6) | uint32_t(k); bits += 6; if (bits >= 8) { bits -= 8; out += char((v >> bits) & 0xFF); } }
    return out;
}

struct ParsedObj { int id = 0, ch = 0, layer = -999, z = -999; bool hasHsv = false, hasZ = false, hasLayer = false; std::string b64; };

// Reinterpreta la cadena de GD: devuelve las formas reconstruidas y datos de cada objeto para validarlos.
static std::vector<Shape> parseGd(const std::string& str, const Result& r, const GdExportOptions& o,
                                  const std::map<int, std::array<uint8_t, 3>>& chanColors, std::vector<ParsedObj>& objs, bool& formatOk) {
    std::vector<Shape> out; formatOk = true;
    GdSize sz = gdImageSize(r, o);
    double ux = sz.w / r.rasterW, uy = sz.h / r.rasterH;
    std::stringstream ss(str);
    std::string obj;
    while (std::getline(ss, obj, ';')) {
        if (obj.empty()) continue;
        std::vector<std::string> tok; std::stringstream os(obj); std::string t;
        while (std::getline(os, t, ',')) tok.push_back(t);
        if (tok.size() % 2) { formatOk = false; continue; }
        double x = 0, y = 0, sx = 1, sy = 1, rot = 0; std::string hsv; ParsedObj po;
        for (size_t i = 0; i + 1 < tok.size(); i += 2) {
            int k = std::atoi(tok[i].c_str()); const std::string& v = tok[i + 1];
            if (k == 1) po.id = std::atoi(v.c_str()); else if (k == 2) x = std::atof(v.c_str()); else if (k == 3) y = std::atof(v.c_str());
            else if (k == 128) sx = std::atof(v.c_str()); else if (k == 129) sy = std::atof(v.c_str());
            else if (k == 6) rot = std::atof(v.c_str());
            else if (k == 25) { po.z = std::atoi(v.c_str()); po.hasZ = true; }
            else if (k == 24) { po.layer = std::atoi(v.c_str()); po.hasLayer = true; }
            else if (k == 43) { hsv = v; po.hasHsv = true; } else if (k == 41) po.hasHsv = true;
            else if (k == 21) po.ch = std::atoi(v.c_str()); else if (k == 31) po.b64 = v;
        }
        objs.push_back(po);
        if (po.id == 914) continue;                              // texto: se valida aparte
        Shape sh;
        if (!hsv.empty()) {
            std::stringstream hs(hsv); std::string a; std::vector<double> n;
            while (std::getline(hs, a, 'a')) n.push_back(std::atof(a.c_str()));
            if (n.size() != 5) { formatOk = false; continue; }
            hsvToRgb(n[0], n[1], n[2], sh.r, sh.g, sh.b);
        } else if (chanColors.count(po.ch)) {
            auto c = chanColors.at(po.ch); sh.r = c[0]; sh.g = c[1]; sh.b = c[2];
        } else if (po.ch == o.blackChannel) { sh.r = sh.g = sh.b = 0; }
        else if (po.ch == o.whiteChannel) { sh.r = sh.g = sh.b = 255; }
        else { formatOk = false; continue; }
        sh.cx = float((x - o.originX) / ux + r.rasterW * 0.5); sh.cy = float((o.originY - y) / uy + r.rasterH * 0.5);
        bool rotated = std::abs(rot) > 0.001; double bl = rotated ? 0 : o.bleed;
        sh.w = float((sx - bl) / ux); sh.h = float((sy - bl) / uy); sh.rot = float(o.flipRotation ? -rot : rot); sh.z = int16_t(po.z - o.zBase);
        out.push_back(sh);
    }
    return out;
}

int main(int argc, char** argv) {
    if (argc < 2) { std::printf("uso: %s imagen [presupuesto] [salida] [metodo|all]\n", argv[0]); return 1; }
    Image src = loadImage(argv[1]);
    if (src.empty()) { std::printf("no se pudo cargar %s\n", argv[1]); return 1; }
    int budget = argc > 2 ? std::atoi(argv[2]) : 10000;
    std::string outDir = argc > 3 ? argv[3] : ".";
    int only = (argc > 4 && std::strcmp(argv[4], "all") != 0) ? std::atoi(argv[4]) : -1;

    Options opt; opt.budget = budget;
    if (const char* q = std::getenv("GDX_QUALITY")) opt.quality = std::atoi(q);
    if (const char* a = std::getenv("GDX_ASPECT")) opt.asciiAspect = float(std::atof(a));
    int mainMode = 0; if (const char* m = std::getenv("GDX_MAINMODE")) mainMode = std::atoi(m);
    opt.mainChannels = mainMode == 1 || mainMode == 3; opt.grayscale = mainMode >= 2;
    if (const char* c = std::getenv("GDX_LIGHTCOLOR")) opt.colorChannels = std::atoi(c) != 0;
    if (const char* k = std::getenv("GDX_PALETTE")) opt.paletteSize = std::atoi(k);
    if (const char* k = std::getenv("GDX_MAINPAL")) opt.mainPaletteSize = std::atoi(k);
    if (const char* k = std::getenv("GDX_CHUNK")) opt.asciiChunk = std::atoi(k);
    if (const char* k = std::getenv("GDX_SPECIAL")) opt.lightSpecialChannels = std::atoi(k) != 0;

    // Canales que "ya usa el nivel": 2..9 y los múltiplos de 7. La paleta debe esquivarlos.
    std::set<int> used; for (int c = 2; c <= 9; ++c) used.insert(c); for (int c = 7; c <= 999; c += 7) used.insert(c);
    opt.maxChannels = countFreeChannels(used);

    std::printf("Imagen %dx%d  presupuesto=%d  alfa=%s  modo-principal=%d  ligero-color=%d  canales-libres=%d\n", src.w, src.h, budget,
                hasTransparency(src, 128) ? "si" : "no", mainMode, int(opt.colorChannels), opt.maxChannels);
    std::printf("%-26s %7s %8s %7s %6s %4s  %s\n", "metodo", "objetos", "PSNR dB", "ms", "KB", "can", "validacion");
    int fails = 0;
    for (int m = 0; m < int(Method::COUNT); ++m) {
        if (only >= 0 && m != only) continue;
        Result r = convert(src, Method(m), opt, nullptr);
        std::string why;
        size_t nObj = r.shapes.size() + r.textRows.size();
        if (int(nObj) > budget) why += "SUPERA-PRESUPUESTO ";
        if (nObj == 0) why += "VACIO ";

        // canales libres para este resultado
        ChannelMap cmap = buildChannelMap(r);
        GdExportOptions eo; eo.originX = 1234.5; eo.originY = -321.25; eo.longSide = 600;
        eo.monoFastPath = r.mono && !r.colored;
        eo.asciiKerning = 0;
        std::map<int, std::array<uint8_t, 3>> chanColors;
        if (r.colored) {
            eo.channels = pickFreeChannels(used, 2, int(cmap.colors.size()));
            if (eo.channels.size() != cmap.colors.size()) why += "FALTAN-CANALES ";
            for (size_t i = 0; i < eo.channels.size(); ++i) {
                chanColors[eo.channels[i]] = cmap.colors[i];
                if (eo.channels[i] == 1 || used.count(eo.channels[i]) || eo.channels[i] > 999) why += "CANAL-INVALIDO ";
            }
            if (int(cmap.colors.size()) > opt.maxChannels) why += "MAS-COLORES-QUE-CANALES ";
        }
        std::string gd = toGdString(r, eo);
        std::vector<ParsedObj> objs; bool fmtOk;
        auto back = parseGd(gd, r, eo, chanColors, objs, fmtOk);
        if (!fmtOk) why += "FORMATO ";
        if (objs.size() != nObj) why += "NUM-OBJETOS ";

        // reglas de la cadena
        int baseMaxKey = -1000000, inkMinKey = 1000000; int textIdx = 0;
        for (auto& po : objs) {
            if (!po.hasZ) why += "SIN-Z-ORDER ";
            if (r.colored && po.hasHsv) why += "HSV-EN-MODO-CANALES ";
            if (!r.colored && !r.mono && !po.hasHsv) why += "SIN-HSV ";
            if (r.colored && (po.ch < 2 || po.ch > 999 || used.count(po.ch))) why += "CANAL-USADO ";
            if (r.mono) {
                if (!po.hasLayer) why += "SIN-CAPA ";
                // orden de dibujo: (capa, z-order). La base/papel debe quedar siempre por debajo de la tinta.
                int key = po.layer * 100000 + po.z;
                bool isText = po.id == 914;
                bool isBase = !isText && po.layer == eo.layerBase;
                if (isBase) baseMaxKey = std::max(baseMaxKey, key); else inkMinKey = std::min(inkMinKey, key);
            }
            if (po.id == 914) {
                std::string txt = b64dec(po.b64);
                if (int(txt.size()) > opt.asciiChunk) why += "TEXTO-MAS-LARGO-QUE-CHUNK ";
                if (textIdx >= int(r.textRows.size()) || txt != r.textRows[size_t(textIdx)].text) why += "TEXTO-DISTINTO ";
                ++textIdx;
            }
        }
        if (r.mono && baseMaxKey != -1000000 && inkMinKey != 1000000 && baseMaxKey >= inkMinKey) why += "BASE-POR-ENCIMA-DE-LA-TINTA ";

        // parámetros tras exportar y reinterpretar
        double maxPos = 0, maxRot = 0; int maxCol = 0;
        if (back.size() == r.shapes.size()) {
            for (size_t i = 0; i < back.size(); ++i) {
                const Shape &p0 = r.shapes[i], &p1 = back[i];
                maxPos = std::max({maxPos, double(std::abs(p0.cx - p1.cx)), double(std::abs(p0.cy - p1.cy)), double(std::abs(p0.w - p1.w)), double(std::abs(p0.h - p1.h))});
                double dr = std::abs(std::fmod(p0.rot - p1.rot, 180.0f)); maxRot = std::max(maxRot, std::min(dr, 180.0 - dr));
                maxCol = std::max({maxCol, std::abs(int(p0.r) - p1.r), std::abs(int(p0.g) - p1.g), std::abs(int(p0.b) - p1.b)});
            }
        } else why += "FORMAS ";
        if (maxPos >= 0.01 || maxRot >= 0.02 || maxCol > 1) why += "DESVIACION ";

        // cobertura: sin huecos transparentes cuando la imagen es opaca
        double cover = 1.0;
        Image a = renderShapes(r.shapes, r.rasterW, r.rasterH);
        if (!hasTransparency(src, 128) && !r.shapes.empty()) {
            size_t c = 0; for (size_t i = 3; i < a.rgba.size(); i += 4) c += a.rgba[i] > 0;
            cover = double(c) / double(a.w * a.h);
            if (cover < 0.999) why += "HUECOS(" + std::to_string(int(cover * 1000) / 10.0).substr(0, 4) + "%) ";
        }

        // PSNR comparable (solo métodos principales)
        double common = -1;
        if (!r.mono) {
            Image srcRef = opt.grayscale ? toGrayscale(src) : src;
            int rw, rh; fitSize(src.w, src.h, 512, rw, rh);
            Image refc = resizeArea(srcRef, rw, rh);
            std::vector<Shape> sc = r.shapes; float fx = float(rw) / r.rasterW, fy = float(rh) / r.rasterH;
            for (auto& s : sc) { s.cx *= fx; s.cy *= fy; s.w *= fx; s.h *= fy; }
            common = computePsnr(refc, renderShapes(sc, rw, rh), 128);
        }
        if (!why.empty()) ++fails;

        char ps[16]; if (common < 0) std::snprintf(ps, sizeof ps, "    -"); else std::snprintf(ps, sizeof ps, "%.2f", common);
        std::printf("%-26s %7zu %8s %7.0f %6zu %4zu  %s\n", methodInfo(Method(m)).name, nObj, ps, r.ms, gd.size() / 1024,
                    r.colored ? cmap.colors.size() : size_t(0), why.empty() ? "OK" : why.c_str());

        char path[512]; std::snprintf(path, sizeof path, "%s/m%d_%d.png", outDir.c_str(), m + 1, budget);
        if (!r.previewImg.empty()) {                                   // ASCII: bloques de color de la vista previa
            Image big = r.previewImg;
            savePreview(path, big, true);
        } else {
            int kk = std::max(1, (r.mono ? 1200 : 640) / std::max(r.rasterW, r.rasterH));
            savePreview(path, r.mono ? renderShapesScaled(r.shapes, r.rasterW, r.rasterH, kk) : a, true);
        }
        if (!r.textRows.empty()) {
            char tp[512]; std::snprintf(tp, sizeof tp, "%s/ascii_%d.txt", outDir.c_str(), budget);
            if (FILE* f = std::fopen(tp, "w")) { for (auto& t : r.textRows) std::fprintf(f, "%.1f %.1f %d %d %d %s\n", t.cx, t.cy, t.r, t.g, t.b, t.text.c_str()); std::fclose(f); }
        }
    }
    {
        int w, h; fitSize(src.w, src.h, 768, w, h);
        char path[512]; std::snprintf(path, sizeof path, "%s/ref.png", outDir.c_str());
        savePreview(path, resizeArea(src, w, h), true);
    }
    std::printf(fails ? "\nHAY FALLOS: %d\n" : "\nTodo correcto.\n", fails);
    return fails ? 2 : 0;
}
