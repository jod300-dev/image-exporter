// ============================================================================
//  gdx :: API pública del motor
// ============================================================================
#pragma once
#include "core.hpp"
#include "partition.hpp"
#include "regions.hpp"
#include "geometrize.hpp"
#include "mono.hpp"

namespace gdx {

constexpr int kNormalMethodCount = 8;     // métodos "normales"; el resto son estilos monocromos
inline bool isMonoMethod(Method m) { return int(m) >= kNormalMethodCount; }

struct MethodInfo { const char* name; const char* desc; bool slow; };

inline const MethodInfo& methodInfo(Method m) {
    static const MethodInfo kInfo[int(Method::COUNT)] = {
        {"1. Uniform grid",
         "One cell per object using the average color. Fast; ideal for pixel art.", false},
        {"2. Adaptive quadtree",
         "Splits into 4 only where there is detail. Uses exactly the object limit.", false},
        {"3. Adaptive BSP (k-d)",
         "Optimal cuts in X or Y: rectangles of any shape. Great for photos.", false},
        {"4. Layer pyramid",
         "A big block plus detail layers only where the color changes. Saves objects.", false},
        {"5. Palette regions",
         "Reduces the palette and merges equal areas into rectangles. For illustrations.", false},
        {"6. Horizontal strips",
         "Color strips by rows or columns with tolerance. Scanline style, very fast.", false},
        {"7. Rotated rectangles",
         "Rotated rectangles found by random search (Geometrize style). Painterly look.", true},
        {"8. Hybrid BSP + rotated",
         "BSP base plus rotated rectangles over the leftovers. Maximum quality.", true},
        {"9. Halftone (diamonds)",
         "Newspaper halftone: diamonds whose size follows darkness. No HSV.", false},
        {"10. Line engraving",
         "Parallel lines of varying thickness, like an engraving. Few objects.", false},
        {"11. Cross-hatching",
         "Stripe layers at 45, 135, 0 and 90 degrees depending on darkness. No HSV.", false},
        {"12. Stippling (dots)",
         "Dot cloud, denser where the image is dark (pointillism).", false},
        {"13. Bayer dithering",
         "Black and white ordered dithering, merged into rectangles. No HSV.", false},
        {"14. Ink (comic)",
         "Adaptive threshold: solid ink blots, comic style. Very few objects.", false},
        {"15. Stroke sketch",
         "Short strokes oriented along the edges, like a pencil drawing.", false},
        {"16. Spiral",
         "A spiral whose thickness follows the darkness of the image.", false},
        {"17. 10 PRINT diagonals",
         "One / or \\ diagonal per cell depending on local structure and tone.", false},
        {"18. ASCII art (text)",
         "Text in text objects: the image in a few objects. Needs a monospaced font.", false},
    };
    return kInfo[int(m)];
}

// Convierte la imagen a formas con el método indicado. Garantiza shapes.size() + textRows.size() <= opt.budget.
inline Result convert(const Image& srcIn, Method method, const Options& opt, Progress* prog = nullptr) {
    Result res;
    double t0 = nowMs();
    Options o = opt;
    o.budget = std::clamp(o.budget, 1, 80000);
    o.maxRaster = std::clamp(o.maxRaster, 32, 2048);
    o.maxChannels = std::clamp(o.maxChannels, 0, kChanPool);
    res.aspect = srcIn.h > 0 ? float(srcIn.w) / float(srcIn.h) : 1.f;
    if (srcIn.empty()) return res;
    res.hadAlpha = hasTransparency(srcIn, o.alphaThreshold);
    report(prog, 0.f);

    const bool light = isMonoMethod(method);
    // Métodos principales en escala de grises: se trabaja con la imagen en grises desde el principio.
    Image grayStore;
    const Image* sp = &srcIn;
    if (!light && o.grayscale) { grayStore = toGrayscale(srcIn); sp = &grayStore; }
    const Image& src = *sp;

    auto treeRaster = [&](Image& ras) {
        int w, h; fitSize(src.w, src.h, o.maxRaster, w, h);
        ras = resizeArea(src, w, h);
    };

    // Las formas libres que se solapan no pueden dejar huecos transparentes:
    // con transparencia se usa BSP (que sí respeta los bordes) y se avisa.
    if (res.hadAlpha && (method == Method::Geometrize || method == Method::Hybrid)) {
        method = Method::BSP;
        res.note = "Image with transparency: BSP was used (rotated rectangles cannot leave gaps).";
    }

    Image reference;            // raster contra el que se mide el PSNR
    bool haveRef = false;
    switch (method) {
        case Method::UniformGrid: {
            int w, h; res.shapes = gridShapes(src, o.budget, o.alphaThreshold, w, h);
            res.rasterW = w; res.rasterH = h;
            reference = resizeArea(src, w, h); haveRef = true;
            break;
        }
        case Method::Quadtree:
        case Method::BSP: {
            Image ras; treeRaster(ras);
            res.shapes = treeShapes(ras, o.budget, o.alphaThreshold, method == Method::BSP, prog);
            res.rasterW = ras.w; res.rasterH = ras.h; reference = std::move(ras); haveRef = true;
            break;
        }
        case Method::Pyramid: {
            Image ras; treeRaster(ras);
            res.shapes = pyramidShapes(ras, o.budget, o.alphaThreshold, prog);
            res.rasterW = ras.w; res.rasterH = ras.h; reference = std::move(ras); haveRef = true;
            break;
        }
        case Method::Regions: {
            Candidate c = regionsBest(src, o, prog);
            res.shapes = std::move(c.shapes); res.rasterW = c.w; res.rasterH = c.h; res.psnr = c.psnr;
            break;
        }
        case Method::Runs: {
            Candidate c = runsBest(src, o, prog);
            res.shapes = std::move(c.shapes); res.rasterW = c.w; res.rasterH = c.h; res.psnr = c.psnr;
            break;
        }
        case Method::Geometrize:
        case Method::Hybrid: {
            int w, h; Image target;
            res.shapes = geometrizeShapes(src, o, method == Method::Hybrid ? hybridFractionFor(o) : 0.0, prog, w, h, &target);
            res.rasterW = w; res.rasterH = h; reference = std::move(target); haveRef = true;
            break;
        }
        case Method::Halftone: case Method::Engrave: case Method::Hatch: case Method::Stipple:
        case Method::Dither: case Method::Ink: case Method::Strokes: case Method::Spiral: case Method::Truchet:
        case Method::Ascii: {
            res.mono = true;
            const bool shapesStyle = method != Method::Ascii;
            Options os = o;
            if (o.colorChannels && shapesStyle) os.budget = std::max(50, int(o.budget * 0.65));   // 35 % queda para la base de color
            res.shapes = monoShapes(src, method, os, res, prog);
            if (shapesStyle) {
                if (o.colorChannels && !isCancelled(prog)) {
                    int baseBudget = std::min(o.budget - int(res.shapes.size()), int(o.budget * 0.4));
                    colorizeLightShapes(res, src, o, std::max(20, baseBudget));
                } else {
                    res.colored = !o.lightSpecialChannels;       // blanco y negro: 2 canales libres (o los especiales 1010/1011)
                }
            }
            if (res.colored && !o.colorChannels && o.maxChannels < 2) {       // sin canales libres: especiales 1010/1011
                res.colored = false;
                for (auto& t : res.textRows) t.colored = false;
                res.note = "No free channels: the special channels 1010/1011 are used.";
            }
            res.psnr = -1;                       // la fidelidad tonal no se mide en dB para tramas
            break;
        }
        default: break;
    }
    if (isCancelled(prog)) res.cancelled = true;

    // Métodos principales con canales de color (sin HSV): paleta de K colores, cada uno = un canal.
    if (!light && o.mainChannels && o.maxChannels >= 2 && !res.shapes.empty()) {
        int K = o.grayscale ? std::min(o.mainPaletteSize, 32) : o.mainPaletteSize;
        K = std::clamp(K, 2, std::max(2, o.maxChannels));
        quantizeShapesToPalette(res.shapes, K);
        res.colored = true;
        if (method == Method::Regions || method == Method::Runs) {         // su PSNR se calculó antes de la paleta
            int rw, rh; fitSize(src.w, src.h, 512, rw, rh);
            res.psnr = psnrScaled(resizeArea(src, rw, rh), res.shapes, res.rasterW, res.rasterH, o.alphaThreshold);
        }
    }

    // Garantía dura del presupuesto (no debería activarse).
    if (int(res.shapes.size() + res.textRows.size()) > o.budget) {
        size_t keep = size_t(std::max(0, o.budget));
        if (res.textRows.size() > keep) res.textRows.resize(keep);
        res.shapes.resize(std::min(res.shapes.size(), keep - res.textRows.size()));
    }

    if (haveRef && !res.mono) {
        Image r = renderShapes(res.shapes, res.rasterW, res.rasterH);
        res.psnr = computePsnr(reference, r, o.alphaThreshold);
    }
    res.ms = nowMs() - t0;
    report(prog, 1.f);
    return res;
}

} // namespace gdx
