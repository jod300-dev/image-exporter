// ============================================================================
//  Volcado de la interfaz a texto: recorre el arbol de nodos del popup y escribe, por cada nodo, su clase,
//  posicion, ancla, tamano, escala y la caja que ocupa REALMENTE en pantalla (coordenadas de mundo).
//  Al final lista automaticamente los solapes entre textos/botones y lo que se sale del marco, para ver con
//  exactitud como genera GD la ventana y por que algo se ve mal.
// ============================================================================
#pragma once
#include <Geode/Geode.hpp>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <iomanip>
#include <sstream>
#include <string>
#include <typeinfo>
#include <vector>
#if !defined(GEODE_IS_WINDOWS)
#include <cxxabi.h>
#include <cstdlib>
#endif

namespace uidump {

using namespace geode::prelude;

// Nombre legible de la clase (typeid; en Windows se quitan los prefijos "class "/"struct ", en el resto se demangla).
inline std::string className(CCObject* o) {
    if (!o) return "null";
    std::string n = typeid(*o).name();
#if defined(GEODE_IS_WINDOWS)
    for (char const* pre : {"class ", "struct "}) {
        size_t len = std::char_traits<char>::length(pre), pos;
        while ((pos = n.find(pre)) != std::string::npos) n.erase(pos, len);
    }
#else
    int st = 0;
    char* d = abi::__cxa_demangle(n.c_str(), nullptr, nullptr, &st);
    if (st == 0 && d) { n = d; std::free(d); }
#endif
    return n;
}

struct Rec {
    int parent = -1;
    int depth = 0;
    std::string label;          // clase + id, para los avisos
    float x0 = 0, y0 = 0, x1 = 0, y1 = 0;
    bool visible = true;        // visible de verdad (todos sus ancestros tambien)
    int kind = 0;               // 0 otro, 1 texto, 2 boton/item de menu
};

inline void worldBox(CCNode* n, float& x0, float& y0, float& x1, float& y1) {
    CCSize s = n->getContentSize();
    CCPoint c[4] = {
        n->convertToWorldSpace(ccp(0, 0)),
        n->convertToWorldSpace(ccp(s.width, 0)),
        n->convertToWorldSpace(ccp(s.width, s.height)),
        n->convertToWorldSpace(ccp(0, s.height)),
    };
    x0 = x1 = c[0].x; y0 = y1 = c[0].y;
    for (auto const& p : c) {
        x0 = std::min(x0, p.x); x1 = std::max(x1, p.x);
        y0 = std::min(y0, p.y); y1 = std::max(y1, p.y);
    }
}

inline std::string escapeText(std::string s) {
    std::string out;
    for (char ch : s) {
        if (ch == '\n') out += "\\n";
        else if (ch == '"') out += "\\\"";
        else out += ch;
    }
    return out;
}

inline void walk(CCNode* n, int parent, int depth, bool parentVisible, std::vector<Rec>& recs, std::ostringstream& out) {
    if (!n) return;
    Rec r;
    r.parent = parent;
    r.depth = depth;
    r.visible = parentVisible && n->isVisible();
    worldBox(n, r.x0, r.y0, r.x1, r.y1);

    std::string cls = className(n);
    std::string id = n->getID();
    r.label = cls + (id.empty() ? "" : " #" + id);

    std::string extra;
    if (auto* l = typeinfo_cast<CCLabelBMFont*>(n)) {
        r.kind = 1;
        extra += " | texto=\"" + escapeText(l->getString() ? l->getString() : "") + "\"";
    } else if (auto* sp = typeinfo_cast<CCSprite*>(n)) {
        CCRect tr = sp->getTextureRect();
        char b[96];
        std::snprintf(b, sizeof b, " | textura=%.0fx%.0f flipX=%d flipY=%d", tr.size.width, tr.size.height, int(sp->isFlipX()), int(sp->isFlipY()));
        extra += b;
    }
    if (auto* it = typeinfo_cast<CCMenuItem*>(n)) {
        r.kind = 2;
        extra += std::string(" | boton activo=") + (it->isEnabled() ? "si" : "no");
        if (auto* tg = typeinfo_cast<CCMenuItemToggler*>(n)) extra += std::string(" marcado=") + (tg->isToggled() ? "si" : "no");
    }

    int idx = int(recs.size());
    recs.push_back(r);

    CCPoint pos = n->getPosition(), anc = n->getAnchorPoint();
    CCSize sz = n->getContentSize();
    char buf[420];
    std::snprintf(buf, sizeof buf,
        "[%d] %s tag=%d z=%d vis=%d(%d) pos=(%.1f,%.1f) ancla=(%.2f,%.2f) tam=(%.1f,%.1f) escala=(%.3f,%.3f) rot=%.1f"
        " | mundo=[%.1f,%.1f -> %.1f,%.1f] %.1fx%.1f",
        idx, cls.c_str(), n->getTag(), n->getZOrder(), int(n->isVisible()), int(r.visible),
        pos.x, pos.y, anc.x, anc.y, sz.width, sz.height, n->getScaleX(), n->getScaleY(), n->getRotation(),
        r.x0, r.y0, r.x1, r.y1, r.x1 - r.x0, r.y1 - r.y0);
    out << std::string(size_t(depth) * 2, ' ') << buf;
    if (!id.empty()) out << " id=" << id;
    out << extra << "\n";

    if (auto* children = n->getChildren()) {
        for (auto* c : CCArrayExt<CCNode*>(children)) walk(c, idx, depth + 1, r.visible, recs, out);
    }
}

inline bool isAncestor(std::vector<Rec> const& recs, int anc, int node) {
    for (int p = recs[size_t(node)].parent; p >= 0; p = recs[size_t(p)].parent) if (p == anc) return true;
    return false;
}

// `frame` = el marco visible del popup (se usa para detectar lo que se sale). `header` = datos del estado del mod.
inline std::string dumpTree(CCNode* root, CCNode* frame, std::string const& header) {
    std::ostringstream out;
    out << std::fixed << std::setprecision(1);
    out << header << "\n";

    float fx0 = 0, fy0 = 0, fx1 = 0, fy1 = 0;
    if (frame) worldBox(frame, fx0, fy0, fx1, fy1);
    out << "=== MARCO DEL POPUP (mundo) ===\n";
    out << "[" << fx0 << "," << fy0 << " -> " << fx1 << "," << fy1 << "]  " << (fx1 - fx0) << "x" << (fy1 - fy0) << "\n\n";

    out << "=== ARBOL DE NODOS ===\n";
    out << "(indice) clase tag z vis=propia(real) pos ancla tam escala rot | mundo=[caja en pantalla] ancho x alto\n";
    std::vector<Rec> recs;
    walk(root, -1, 0, true, recs, out);

    out << "\n=== SOLAPES entre textos/botones (visibles) ===\n";
    int overlaps = 0;
    for (size_t i = 0; i < recs.size(); ++i) {
        Rec const& a = recs[i];
        if (a.kind == 0 || !a.visible) continue;
        float aw = a.x1 - a.x0, ah = a.y1 - a.y0;
        if (aw < 1.f || ah < 1.f) continue;
        for (size_t j = i + 1; j < recs.size(); ++j) {
            Rec const& b = recs[j];
            if (b.kind == 0 || !b.visible) continue;
            if (isAncestor(recs, int(i), int(j)) || isAncestor(recs, int(j), int(i))) continue;
            float bw = b.x1 - b.x0, bh = b.y1 - b.y0;
            if (bw < 1.f || bh < 1.f) continue;
            float ox = std::min(a.x1, b.x1) - std::max(a.x0, b.x0);
            float oy = std::min(a.y1, b.y1) - std::max(a.y0, b.y0);
            if (ox <= 1.f || oy <= 1.f) continue;
            float area = ox * oy, small = std::min(aw * ah, bw * bh);
            float pct = small > 0 ? 100.f * area / small : 0.f;
            if (pct < 5.f) continue;
            char buf[400];
            std::snprintf(buf, sizeof buf, "[%zu] %s  <->  [%zu] %s : solape %.1fx%.1f (%.0f%% del menor)\n",
                          i, a.label.c_str(), j, b.label.c_str(), ox, oy, pct);
            out << buf;
            ++overlaps;
        }
    }
    if (!overlaps) out << "(ninguno)\n";

    out << "\n=== TEXTOS/BOTONES FUERA DEL MARCO (margen 0.5) ===\n";
    out << "(el boton X de cerrar sobresale a proposito)\n";
    int outside = 0;
    for (size_t i = 0; i < recs.size(); ++i) {
        Rec const& a = recs[i];
        if (a.kind == 0 || !a.visible) continue;
        float l = fx0 - a.x0, r = a.x1 - fx1, t = a.y1 - fy1, b = fy0 - a.y0;
        float worst = std::max(std::max(l, r), std::max(t, b));
        if (worst <= 0.5f) continue;
        char buf[400];
        std::snprintf(buf, sizeof buf, "[%zu] %s : izq %.1f  der %.1f  arriba %.1f  abajo %.1f (positivo = fuera)\n",
                      i, a.label.c_str(), l, r, t, b);
        out << buf;
        ++outside;
    }
    if (!outside) out << "(ninguno)\n";
    return out.str();
}

} // namespace uidump
