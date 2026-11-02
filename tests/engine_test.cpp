// Prueba autónoma del motor (sin Geode):
//   g++ -O2 -std=c++20 -Iinclude -Isrc/engine tests/engine_test.cpp -o engine_test
//   ./engine_test imagen.png [presupuesto] [carpeta_salida] [metodo|all]
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

#include <cstdlib>
#include <cstring>
#include <sstream>
#include <map>
#include <array>
#include "engine.hpp"
#include "gdexport.hpp"
#include "mono.hpp"

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

// --- Parseo inverso de la cadena de GD para validar el exportador -----------
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

static std::vector<Shape> parseGd(const std::string& str, const Result& r, const GdExportOptions& o, int& objCount, bool& formatOk, const std::map<int, std::array<uint8_t, 3>>* chanColors = nullptr) {
    std::vector<Shape> out; objCount = 0; formatOk = true; int textCount = 0;
    GdSize sz = gdImageSize(r, o);
    double ux = sz.w / r.rasterW, uy = sz.h / r.rasterH;
    std::stringstream ss(str);
    std::string obj;
    while (std::getline(ss, obj, ';')) {
        if (obj.empty()) continue;
        ++objCount;
        std::vector<std::string> tok; std::stringstream os(obj); std::string t;
        while (std::getline(os, t, ',')) tok.push_back(t);
        if (tok.size() % 2) { formatOk = false; continue; }
        double x = 0, y = 0, sx = 1, sy = 1, rot = 0; int z = 0; std::string hsv, b64; int id = 0, ch = 0;
        for (size_t i = 0; i + 1 < tok.size(); i += 2) {
            int k = std::atoi(tok[i].c_str()); const std::string& v = tok[i + 1];
            if (k == 1) id = std::atoi(v.c_str()); else if (k == 2) x = std::atof(v.c_str()); else if (k == 3) y = std::atof(v.c_str());
            else if (k == 128) sx = std::atof(v.c_str()); else if (k == 129) sy = std::atof(v.c_str());
            else if (k == 6) rot = std::atof(v.c_str()); else if (k == 25) z = std::atoi(v.c_str());
            else if (k == 43) hsv = v; else if (k == 21) ch = std::atoi(v.c_str()); else if (k == 31) b64 = v;
        }
        if (id == 914) {                                       // objeto de texto (ASCII)
            ++textCount; if (textCount > (int)r.textRows.size() || b64dec(b64) != r.textRows[textCount - 1].text) formatOk = false;
            continue;
        }
        if (id != o.objectId) formatOk = false;
        if (!o.monoFastPath && !chanColors && ch != o.colorChannel) formatOk = false;
        Shape sh;
        if (hsv.empty()) {                                   // modo monocromo: solo canal, sin HSV
            if (chanColors && chanColors->count(ch)) {         // canal de paleta definido por un trigger
                auto const& c3 = chanColors->at(ch); sh.r = c3[0]; sh.g = c3[1]; sh.b = c3[2];
                if (ch < 2) formatOk = false;
            } else {
                if (!o.monoFastPath || (ch != o.blackChannel && ch != o.whiteChannel)) { formatOk = false; continue; }
                uint8_t cv = ch == o.blackChannel ? 0 : 255; sh.r = sh.g = sh.b = cv;
            }
        } else {
            double h, s, v; std::stringstream hs(hsv); std::string a; std::vector<double> n;
            while (std::getline(hs, a, 'a')) n.push_back(std::atof(a.c_str()));
            if (n.size() != 5) { formatOk = false; continue; }
            h = n[0]; s = n[1]; v = n[2]; hsvToRgb(h, s, v, sh.r, sh.g, sh.b);
        } sh.cx = float((x - o.originX) / ux + r.rasterW * 0.5); sh.cy = float((o.originY - y) / uy + r.rasterH * 0.5);
        bool rotated = std::abs(rot) > 0.001; double bl = rotated ? 0 : o.bleed;
        sh.w = float((sx - bl) / ux); sh.h = float((sy - bl) / uy); sh.rot = float(o.flipRotation ? -rot : rot); sh.z = int16_t(z - o.zBase);
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
    if (const char* c = std::getenv("GDX_COLOR")) opt.colorChannels = std::atoi(c) != 0;
    if (const char* k = std::getenv("GDX_PALETTE")) opt.paletteSize = std::atoi(k);

    std::printf("Imagen %dx%d  presupuesto=%d  alfa=%s\n", src.w, src.h, budget, hasTransparency(src, 128) ? "si" : "no");
    std::printf("%-28s %8s %8s %9s %8s %s\n", "metodo", "objetos", "PSNR dB", "tiempo ms", "raster", "validacion GD");
    int fails = 0;
    for (int m = 0; m < int(Method::COUNT); ++m) {
        if (only >= 0 && m != only) continue;
        Result r = convert(src, Method(m), opt, nullptr);
        bool okCount = int(r.shapes.size() + r.textRows.size()) <= budget;

        GdExportOptions eo; eo.monoFastPath = r.mono; eo.originX = 1234.5; eo.originY = -321.25; eo.longSide = 600; eo.zBase = 0;
        std::string gd = toGdString(r, eo);
        std::string trig = toGdColorTriggers(r, eo);
        // canal -> color según los triggers (solo en modo color)
        std::map<int, std::array<uint8_t, 3>> chanColors; bool trigOk = true; int trigCount = 0;
        if (r.colored) {
            std::stringstream ts(trig); std::string ob;
            while (std::getline(ts, ob, ';')) {
                if (ob.empty()) continue; ++trigCount;
                std::vector<std::string> tk; std::stringstream os2(ob); std::string t2;
                while (std::getline(os2, t2, ',')) tk.push_back(t2);
                int id = 0, tch = 0, R = -1, G = -1, B = -1;
                for (size_t i = 0; i + 1 < tk.size(); i += 2) {
                    int k = std::atoi(tk[i].c_str()), v = std::atoi(tk[i + 1].c_str());
                    if (k == 1) id = v; else if (k == 7) R = v; else if (k == 8) G = v; else if (k == 9) B = v; else if (k == 23) tch = v;
                }
                if (id != 899 || R < 0 || G < 0 || B < 0 || tch < 2 || tch > 999 || chanColors.count(tch)) trigOk = false;
                chanColors[tch] = {uint8_t(R), uint8_t(G), uint8_t(B)};
            }
            if (trigCount > opt.paletteSize || trigCount == 0) trigOk = false;
        }
        int cnt; bool fmtOk;
        auto back = parseGd(gd, r, eo, cnt, fmtOk, r.colored ? &chanColors : nullptr);
        if (r.mono && gd.find(",43,") != std::string::npos) fmtOk = false;       // los estilos ligeros NUNCA llevan HSV
        if (!trigOk) fmtOk = false;
        Image a = renderShapes(r.shapes, r.rasterW, r.rasterH), b = renderShapes(back, r.rasterW, r.rasterH);
        double rt = computePsnr(a, b, 1);                         // ida y vuelta del exportador (informativo)
        // validación real: parámetros de cada objeto tras exportar y reinterpretar
        double maxPos = 0, maxRot = 0; int maxCol = 0;
        if (back.size() == r.shapes.size()) {      // (las filas de texto se validan aparte)
            for (size_t i = 0; i < back.size(); ++i) {
                const Shape &p0 = r.shapes[i], &p1 = back[i];
                maxPos = std::max({maxPos, double(std::abs(p0.cx - p1.cx)), double(std::abs(p0.cy - p1.cy)), double(std::abs(p0.w - p1.w)), double(std::abs(p0.h - p1.h))});
                double dr = std::abs(std::fmod(p0.rot - p1.rot, 180.0f)); maxRot = std::max(maxRot, std::min(dr, 180.0 - dr));
                maxCol = std::max({maxCol, std::abs(int(p0.r) - p1.r), std::abs(int(p0.g) - p1.g), std::abs(int(p0.b) - p1.b)});
            }
        }
        bool okGd = fmtOk && cnt == int(r.shapes.size() + r.textRows.size()) && maxPos < 0.01 && maxRot < 0.02 && maxCol <= 1;
        if (!okCount || !okGd) ++fails;

        // PSNR comparable: todas las formas se escalan a una referencia común
        double common = 0;
        if (!r.mono) {
            int rw, rh; fitSize(src.w, src.h, 512, rw, rh);
            Image refc = resizeArea(src, rw, rh);
            std::vector<Shape> sc = r.shapes; float fx = float(rw) / r.rasterW, fy = float(rh) / r.rasterH;
            for (auto& s : sc) { s.cx *= fx; s.cy *= fy; s.w *= fx; s.h *= fy; }
            common = computePsnr(refc, renderShapes(sc, rw, rh), 128);
        }
        r.psnr = r.mono ? -1 : common;
        if (!r.textRows.empty()) {                              // muestra del arte ASCII
            { char tp[512]; std::snprintf(tp, sizeof tp, "%s/ascii_%d.txt", outDir.c_str(), budget); FILE* f = std::fopen(tp, "w");
              if (f) { for (auto& t : r.textRows) std::fprintf(f, "%s\n", t.text.c_str()); std::fclose(f); } }
            std::printf("--- ASCII: %zu filas x %zu columnas ---\n", r.textRows.size(), r.textRows[0].text.size());

        }
        char name[64]; std::snprintf(name, sizeof name, "%s", methodInfo(Method(m)).name);
        char ps[16]; if (r.mono) std::snprintf(ps, sizeof ps, "    -"); else std::snprintf(ps, sizeof ps, "%.2f", r.psnr);
        std::printf("%-28s %8zu %8s %9.0f %4dx%-4d %s%s  (gd %zu KB, dpos %.4f drot %.3f dcol %d%s)\n", name, r.shapes.size() + r.textRows.size(), ps, r.ms,
                    r.rasterW, r.rasterH, okCount ? "OK" : "SUPERA-PRESUPUESTO ", okGd ? "" : " GD-FALLA", gd.size() / 1024, maxPos, maxRot, maxCol, r.colored ? (std::string(", canales ") + std::to_string(trigCount)).c_str() : ""); (void)rt;
        char path[512]; std::snprintf(path, sizeof path, "%s/m%d_%d.png", outDir.c_str(), m + 1, budget);
        // vista previa ampliada a un lado largo de ~800 px
        int kk = std::max(1, 1200 / std::max(r.rasterW, r.rasterH));
        savePreview(path, r.mono ? renderShapesScaled(r.shapes, r.rasterW, r.rasterH, kk) : a, true);
    }
    // referencia redimensionada para comparar a ojo
    {
        int w, h; fitSize(src.w, src.h, 768, w, h);
        char path[512]; std::snprintf(path, sizeof path, "%s/ref.png", outDir.c_str());
        savePreview(path, resizeArea(src, w, h), true);
    }
    std::printf(fails ? "\nHAY FALLOS: %d\n" : "\nTodo correcto.\n", fails);
    return fails ? 2 : 0;
}
