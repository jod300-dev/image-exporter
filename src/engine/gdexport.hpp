// ============================================================================
//  Convierte un Result en la cadena de objetos que entiende
//  LevelEditorLayer::createObjectsFromString (formato clave,valor;...).
//
//  Objeto base: 3092 (cuadrado "pixel" de 1x1 unidad, escalable en X/Y con las
//  claves 128/129). El color se aplica con HSV absoluto sobre un canal base
//  (por defecto 1010, negro), igual que hace el mod "Art importer".
// ============================================================================
#pragma once
#include <array>
#include <map>
#include "core.hpp"

namespace gdx {

struct GdExportOptions {
    double originX = 0, originY = 0;   // centro de la imagen en el editor (unidades GD)
    double longSide = 600;             // lado largo de la imagen en unidades GD (30 u = 1 bloque)
    int colorChannel = 1010;           // canal de color base (1010 = negro)
    int objectId = 3092;               // objeto cuadrado 1x1 u
    double bleed = 0.15;               // solape (u) para evitar costuras entre rectángulos
    int zBase = 0;                     // z-order del fondo
    bool flipRotation = false;         // invierte el sentido de giro si hiciera falta
    // Modo monocromo: negro -> canal 1010 y blanco -> canal 1011, SIN claves de HSV (objetos mas ligeros).
    bool monoFastPath = false;
    int blackChannel = 1010, whiteChannel = 1011;
    // ASCII: ancho (unidades) que ocupa un carácter con escala 1 en la fuente del nivel.
    // Depende de la fuente; se calibra en los ajustes.
    double asciiAdvance = 24.0;
    // Colores en canales (estilos ligeros con "colorChannels"): primer canal usado (nunca el 1)
    // y posición de los triggers de color que definen cada canal (antes del inicio del nivel).
    int paletteChannelStart = 2;
    double triggerX = -90.0, triggerY0 = 15.0;
};

namespace detail {
inline void appendNum(std::string& s, double v, int decimals) {
    char buf[40];
    int n = std::snprintf(buf, sizeof(buf), "%.*f", decimals, v);
    while (n > 0 && buf[n - 1] == '0') --n;
    if (n > 0 && buf[n - 1] == '.') --n;
    if (n == 0 || (n == 1 && buf[0] == '-')) { s += '0'; return; }
    if (n == 2 && buf[0] == '-' && buf[1] == '0') { s += '0'; return; }
    s.append(buf, size_t(n));
}
} // namespace detail

// RGB -> cadena HSV de GD "h a s a v a 1 a 1" (saturación y brillo absolutos).
inline void appendGdHsv(std::string& s, uint8_t R, uint8_t G, uint8_t B) {
    float r = R / 255.f, g = G / 255.f, b = B / 255.f;
    float mx = std::max(r, std::max(g, b)), mn = std::min(r, std::min(g, b)), d = mx - mn;
    float hue = 0, sat = 0, val = mx;
    if (d > 0) {
        sat = d / mx;
        if (mx == r) hue = 60.f * ((g - b) / d);
        else if (mx == g) hue = 60.f * (((b - r) / d) + 2.f);
        else hue = 60.f * (((r - g) / d) + 4.f);
    }
    if (hue < 0) hue += 360.f;
    if (hue >= 180.f) hue -= 360.f;                    // GD usa -180..180
    if (std::abs(hue) < 0.005f) hue = 0.05f;           // el tono 0 exacto da problemas en GD (el mod original usa 1 grado; 0,05 evita el error de color)
    detail::appendNum(s, hue, 2); s += 'a';
    detail::appendNum(s, sat, 3); s += 'a';
    detail::appendNum(s, val, 3); s += "a1a1";
}

// Colores realmente usados por la tinta, en orden de aparición -> canal = start + índice.
struct ChannelMap {
    std::vector<std::array<uint8_t, 3>> colors;
    std::map<uint32_t, int> index;
    static uint32_t key(uint8_t r, uint8_t g, uint8_t b) { return (uint32_t(r) << 16) | (uint32_t(g) << 8) | b; }
    int add(uint8_t r, uint8_t g, uint8_t b) {
        auto it = index.find(key(r, g, b));
        if (it != index.end()) return it->second;
        int i = int(colors.size()); colors.push_back({r, g, b}); index[key(r, g, b)] = i; return i;
    }
};

inline ChannelMap buildChannelMap(const Result& r) {
    ChannelMap m;
    if (!r.colored) return m;
    for (const Shape& s : r.shapes) if (s.z >= 1) m.add(s.r, s.g, s.b);
    for (const TextRow& t : r.textRows) if (t.colored) m.add(t.r, t.g, t.b);
    return m;
}

struct GdSize { double w, h; };
inline GdSize gdImageSize(const Result& r, const GdExportOptions& o) {
    double a = r.aspect > 0 ? r.aspect : 1.0;
    return a >= 1.0 ? GdSize{o.longSide, o.longSide / a} : GdSize{o.longSide * a, o.longSide};
}

inline std::string base64Encode(const std::string& in) {
    static const char* T = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out; out.reserve((in.size() + 2) / 3 * 4);
    size_t i = 0;
    for (; i + 2 < in.size(); i += 3) {
        uint32_t v = (uint8_t(in[i]) << 16) | (uint8_t(in[i + 1]) << 8) | uint8_t(in[i + 2]);
        out += T[(v >> 18) & 63]; out += T[(v >> 12) & 63]; out += T[(v >> 6) & 63]; out += T[v & 63];
    }
    if (i + 1 == in.size()) {
        uint32_t v = uint8_t(in[i]) << 16;
        out += T[(v >> 18) & 63]; out += T[(v >> 12) & 63]; out += "==";
    } else if (i + 2 == in.size()) {
        uint32_t v = (uint8_t(in[i]) << 16) | (uint8_t(in[i + 1]) << 8);
        out += T[(v >> 18) & 63]; out += T[(v >> 12) & 63]; out += T[(v >> 6) & 63]; out += '=';
    }
    return out;
}

inline std::string toGdString(const Result& r, const GdExportOptions& o) {
    std::string out;
    if ((r.shapes.empty() && r.textRows.empty()) || r.rasterW <= 0 || r.rasterH <= 0) return out;
    out.reserve(r.shapes.size() * 112);
    GdSize sz = gdImageSize(r, o);
    const double ux = sz.w / r.rasterW, uy = sz.h / r.rasterH;
    const ChannelMap cmap = buildChannelMap(r);
    auto channelOf = [&](uint8_t R, uint8_t G, uint8_t B) { return std::min(999, o.paletteChannelStart + cmap.index.at(ChannelMap::key(R, G, B))); };
    for (const Shape& s : r.shapes) {
        double x = o.originX + (s.cx - r.rasterW * 0.5) * ux;
        double y = o.originY - (s.cy - r.rasterH * 0.5) * uy;
        bool rotated = std::abs(std::fmod(s.rot, 180.0f)) > 0.01f;
        double bl = rotated ? 0.0 : o.bleed;
        double sx = s.w * ux + bl, sy = s.h * uy + bl;
        out += "1,"; out += std::to_string(o.objectId);
        out += ",2,"; detail::appendNum(out, x, 3);
        out += ",3,"; detail::appendNum(out, y, 3);
        bool isBlack = s.r == 0 && s.g == 0 && s.b == 0, isWhite = s.r == 255 && s.g == 255 && s.b == 255;
        if (r.colored && s.z >= 1) {                       // color de paleta: canal propio, sin HSV
            out += ",21,"; out += std::to_string(channelOf(s.r, s.g, s.b));
        } else if (o.monoFastPath && (isBlack || isWhite)) {
            out += ",21,"; out += std::to_string(isBlack ? o.blackChannel : o.whiteChannel);
        } else {
            out += ",21,"; out += std::to_string(o.colorChannel);
            out += ",41,1,43,"; appendGdHsv(out, s.r, s.g, s.b);
        }
        if (o.zBase + s.z != 0) { out += ",25,"; out += std::to_string(o.zBase + s.z); }
        out += ",128,"; detail::appendNum(out, sx, 3);
        out += ",129,"; detail::appendNum(out, sy, 3);
        if (rotated) {
            double rot = o.flipRotation ? -s.rot : s.rot;
            rot = std::fmod(rot, 360.0);
            if (rot > 180) rot -= 360; else if (rot <= -180) rot += 360;
            out += ",6,"; detail::appendNum(out, rot, 2);
        }
        out += ';';
    }
    // Arte ASCII: una fila = un objeto de texto (914) con el contenido en base64 (clave 31)
    if (!r.textRows.empty()) {
        const int baseCh = r.inkValue == 0 ? o.blackChannel : o.whiteChannel;
        const double scale = ux / std::max(0.01, o.asciiAdvance);
        for (const TextRow& t : r.textRows) {
            const int ch = t.colored ? channelOf(t.r, t.g, t.b) : baseCh;
            out += "1,914,2,"; detail::appendNum(out, o.originX + (t.cx - r.rasterW * 0.5) * ux, 3);
            out += ",3,"; detail::appendNum(out, o.originY - (t.cy - r.rasterH * 0.5) * uy, 3);
            out += ",21,"; out += std::to_string(ch);
            out += ",31,"; out += base64Encode(t.text);
            out += ",25,"; out += std::to_string(o.zBase + 1);
            out += ",32,"; detail::appendNum(out, scale, 4);
            out += ';';
        }
    }
    if (!out.empty() && out.back() == ';') out.pop_back();   // createObjectsFromString no necesita el último ';'
    return out;
}

// Triggers de color (objeto 899) que fijan el color de cada canal al empezar el nivel.
// Claves: 7/8/9 = RGB, 10 = duración, 23 = canal destino, 35 = opacidad, 36 = 1 (como en la referencia de SPWN).
// Se colocan a la izquierda del inicio (x < 0) para que se apliquen al arrancar.
inline std::string toGdColorTriggers(const Result& r, const GdExportOptions& o) {
    std::string out;
    ChannelMap cmap = buildChannelMap(r);
    for (size_t i = 0; i < cmap.colors.size(); ++i) {
        int ch = std::min(999, o.paletteChannelStart + int(i));
        out += "1,899,2,"; detail::appendNum(out, o.triggerX, 1);
        out += ",3,"; detail::appendNum(out, o.triggerY0 + 30.0 * double(i), 1);
        out += ",36,1,7,"; out += std::to_string(cmap.colors[i][0]);
        out += ",8,"; out += std::to_string(cmap.colors[i][1]);
        out += ",9,"; out += std::to_string(cmap.colors[i][2]);
        out += ",10,0,35,1,23,"; out += std::to_string(ch);
        out += ';';
    }
    if (!out.empty() && out.back() == ';') out.pop_back();
    return out;
}

} // namespace gdx
