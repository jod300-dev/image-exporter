// Banco de calidad de los 8 métodos principales: PSNR + SSIM (luma) contra una referencia común.
//   g++ -O2 -std=c++20 -Iinclude -Isrc/engine tests/bench.cpp -o bench
//   ./bench "1000,3000,10000" "0,1,2,3,4,5,6,7" img1.png img2.png ...
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"
#include <cstdlib>
#include <sstream>
#include "engine.hpp"
using namespace gdx;

static Image load(const char* p) {
    int w, h, c; unsigned char* d = stbi_load(p, &w, &h, &c, 4); Image im;
    if (!d) return im; im.w = w; im.h = h; im.rgba.assign(d, d + size_t(w) * h * 4); stbi_image_free(d); return im;
}
static double lumaPx(const uint8_t* p) { return 0.299 * p[0] + 0.587 * p[1] + 0.114 * p[2]; }

// SSIM sobre luma con ventanas 8x8 (paso 4)
static double ssim(const Image& a, const Image& b) {
    const double C1 = 6.5025, C2 = 58.5225; double sum = 0; int n = 0;
    for (int y = 0; y + 8 <= a.h; y += 4)
        for (int x = 0; x + 8 <= a.w; x += 4) {
            double ma = 0, mb = 0, saa = 0, sbb = 0, sab = 0;
            for (int j = 0; j < 8; ++j) for (int i = 0; i < 8; ++i) {
                double va = lumaPx(a.at(x + i, y + j)), vb = lumaPx(b.at(x + i, y + j));
                ma += va; mb += vb; saa += va * va; sbb += vb * vb; sab += va * vb;
            }
            ma /= 64; mb /= 64; saa = saa / 64 - ma * ma; sbb = sbb / 64 - mb * mb; sab = sab / 64 - ma * mb;
            sum += ((2 * ma * mb + C1) * (2 * sab + C2)) / ((ma * ma + mb * mb + C1) * (saa + sbb + C2)); ++n;
        }
    return n ? sum / n : 1;
}

int main(int argc, char** argv) {
    if (argc < 4) { std::printf("uso: bench presupuestos metodos img...\n"); return 1; }
    std::vector<int> budgets, methods; { std::stringstream s(argv[1]); std::string t; while (std::getline(s, t, ',')) budgets.push_back(std::atoi(t.c_str())); }
    { std::stringstream s(argv[2]); std::string t; while (std::getline(s, t, ',')) methods.push_back(std::atoi(t.c_str())); }
    Options opt; if (const char* q = std::getenv("GDX_QUALITY")) opt.quality = std::atoi(q);
    if (const char* f = std::getenv("GDX_HYBF")) opt.hybridFraction = float(std::atof(f));
    std::printf("%-14s %6s", "imagen", "obj");
    for (int m : methods) std::printf(" | m%d PSNR  SSIM   ms", m + 1);
    std::printf("\n");
    std::vector<double> accP(methods.size() * budgets.size(), 0), accS(methods.size() * budgets.size(), 0); int nimg = 0;
    for (int ai = 3; ai < argc; ++ai) {
        Image src = load(argv[ai]); if (src.empty()) continue; ++nimg;
        int rw, rh; fitSize(src.w, src.h, 512, rw, rh); Image ref = resizeArea(src, rw, rh);
        std::string name = argv[ai]; name = name.substr(name.find_last_of('/') + 1);
        for (size_t bi = 0; bi < budgets.size(); ++bi) {
            opt.budget = budgets[bi];
            std::printf("%-14s %6d", name.substr(0, 14).c_str(), budgets[bi]);
            for (size_t mi = 0; mi < methods.size(); ++mi) {
                Result r = convert(src, Method(methods[mi]), opt, nullptr);
                std::vector<Shape> sc = r.shapes; float fx = float(rw) / r.rasterW, fy = float(rh) / r.rasterH;
                for (auto& s : sc) { s.cx *= fx; s.cy *= fy; s.w *= fx; s.h *= fy; }
                Image rend = renderShapesScaled(sc, rw, rh, 1);
                double p = computePsnr(ref, rend, 128), s = ssim(ref, rend);
                accP[mi * budgets.size() + bi] += p; accS[mi * budgets.size() + bi] += s;
                std::printf(" | %5.2f %5.3f %5.0f", p, s, r.ms);
                if (int(r.shapes.size()) > budgets[bi]) std::printf("!");
            }
            std::printf("\n");
        }
    }
    std::printf("\nMEDIA (%d imagenes):\n", nimg);
    for (size_t bi = 0; bi < budgets.size(); ++bi) {
        std::printf("%-14s %6d", "media", budgets[bi]);
        for (size_t mi = 0; mi < methods.size(); ++mi) std::printf(" | %5.2f %5.3f      ", accP[mi * budgets.size() + bi] / nimg, accS[mi * budgets.size() + bi] / nimg);
        std::printf("\n");
    }
}
