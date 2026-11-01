// ============================================================================
//  gdx :: API pública del motor
// ============================================================================
#pragma once
#include "core.hpp"
#include "partition.hpp"
#include "regions.hpp"
#include "geometrize.hpp"

namespace gdx {

struct MethodInfo { const char* name; const char* desc; bool slow; };

inline const MethodInfo& methodInfo(Method m) {
    static const MethodInfo kInfo[int(Method::COUNT)] = {
        {"1. Cuadricula uniforme",
         "Una celda por objeto con el color medio. Rapido y predecible; ideal para pixel art.", false},
        {"2. Quadtree adaptativo",
         "Divide en 4 solo donde hay detalle (mejor-primero). Usa justo el limite de objetos.", false},
        {"3. BSP adaptativo (k-d)",
         "Cortes optimos en X o Y: rectangulos de cualquier proporcion. Muy buena calidad en fotos.", false},
        {"4. Piramide de capas",
         "Bloque grande + capas de detalle solo donde el color difiere. Ahorra muchos objetos.", false},
        {"5. Regiones por paleta",
         "Reduce a una paleta y fusiona zonas iguales en rectangulos grandes. Ideal para ilustraciones.", false},
        {"6. Franjas horizontales",
         "Tiras de color por filas con tolerancia. Estilo escaneo; muy rapido y con pocos objetos.", false},
        {"7. Rectangulos rotados",
         "Busqueda aleatoria de rectangulos girados que reducen el error (estilo Geometrize).", true},
        {"8. Hibrido BSP + rotados",
         "Base BSP y el resto del presupuesto en rectangulos rotados sobre los residuos. Maxima calidad.", true},
    };
    return kInfo[int(m)];
}

// Convierte la imagen a formas con el método indicado. Garantiza shapes.size() <= opt.budget.
inline Result convert(const Image& src, Method method, const Options& opt, Progress* prog = nullptr) {
    Result res;
    double t0 = nowMs();
    Options o = opt;
    o.budget = std::clamp(o.budget, 1, 80000);
    o.maxRaster = std::clamp(o.maxRaster, 32, 2048);
    res.aspect = src.h > 0 ? float(src.w) / float(src.h) : 1.f;
    if (src.empty()) return res;
    res.hadAlpha = hasTransparency(src, o.alphaThreshold);
    report(prog, 0.f);

    auto treeRaster = [&](Image& ras) {
        int w, h; fitSize(src.w, src.h, o.maxRaster, w, h);
        ras = resizeArea(src, w, h);
    };

    // Las formas libres que se solapan no pueden dejar huecos transparentes:
    // con transparencia se usa BSP (que sí respeta los bordes) y se avisa.
    if (res.hadAlpha && (method == Method::Geometrize || method == Method::Hybrid)) {
        method = Method::BSP;
        res.note = "Imagen con transparencia: se uso BSP (los rectangulos rotados no pueden dejar huecos).";
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
            res.shapes = geometrizeShapes(src, o, method == Method::Hybrid ? 0.55 : 0.0, prog, w, h, &target);
            res.rasterW = w; res.rasterH = h; reference = std::move(target); haveRef = true;
            break;
        }
        default: break;
    }
    if (isCancelled(prog)) res.cancelled = true;

    // Garantía dura del presupuesto (no debería activarse).
    if (int(res.shapes.size()) > o.budget) res.shapes.resize(size_t(o.budget));

    if (haveRef) {
        Image r = renderShapes(res.shapes, res.rasterW, res.rasterH);
        res.psnr = computePsnr(reference, r, o.alphaThreshold);
    }
    res.ms = nowMs() - t0;
    report(prog, 1.f);
    return res;
}

} // namespace gdx
