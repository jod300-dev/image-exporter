// ============================================================================
//  Volcado COMPLETO de la interfaz a texto. Escribe, sin resumir nada:
//    - la escena en ejecucion (todas sus capas de primer nivel) y el arbol de nodos entero de CADA ventana
//      (FLAlertLayer / Popup) que haya encima: la del mod, el cuadro de info, la de ajustes de Geode, avisos...
//    - por cada nodo: clase, id, tag, z, visible, posicion, ancla, tamano, escala, sesgo, rotacion, color,
//      opacidad, hijos y la caja REAL en pantalla (coordenadas de mundo)
//    - por cada texto: cadena literal (con los bytes en hexadecimal si hay caracteres no ASCII), glifos que la
//      fuente de GD NO tiene (y por eso no se dibujan), numero de lineas y separacion REAL entre lineas
//    - resumenes de textos y de botones, solapes entre textos/botones y lo que se sale del marco de cada ventana
// ============================================================================
#pragma once
#include <Geode/Geode.hpp>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <iomanip>
#include <set>
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
    std::string text;           // (textos) cadena literal
    std::string missing;        // (textos) glifos que la fuente no tiene
    std::string lineInfo;       // (textos) lineas y separacion real
    float x0 = 0, y0 = 0, x1 = 0, y1 = 0;
    bool visible = true;        // visible de verdad (todos sus ancestros tambien)
    int kind = 0;               // 0 otro, 1 texto, 2 boton/item de menu
    bool on = true;             // (botones) activo
    std::string state;          // (botones) marcado/no marcado
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

// Cadena literal: \n y \" escapados; si hay bytes no ASCII se anaden en hexadecimal (asi se ve exactamente que se envio a la fuente).
inline std::string escapeText(std::string const& s) {
    std::string out;
    bool nonAscii = false;
    for (char ch : s) {
        if (ch == '\n') out += "\\n";
        else if (ch == '"') out += "\\\"";
        else out += ch;
        if (static_cast<unsigned char>(ch) >= 0x80) nonAscii = true;
    }
    if (nonAscii) {
        out += "  [hex:";
        char b[8];
        for (char ch : s) { std::snprintf(b, sizeof b, " %02X", static_cast<unsigned>(static_cast<unsigned char>(ch))); out += b; }
        out += "]";
    }
    return out;
}

inline std::vector<uint32_t> utf8ToCodepoints(std::string const& s) {
    std::vector<uint32_t> v;
    for (size_t i = 0; i < s.size();) {
        unsigned char c = static_cast<unsigned char>(s[i]);
        uint32_t cp = 0xFFFD; size_t n = 1;
        if (c < 0x80) { cp = c; n = 1; }
        else if ((c >> 5) == 0x6) { cp = c & 0x1Fu; n = 2; }
        else if ((c >> 4) == 0xE) { cp = c & 0x0Fu; n = 3; }
        else if ((c >> 3) == 0x1E) { cp = c & 0x07u; n = 4; }
        if (n > 1) for (size_t k = 1; k < n && i + k < s.size(); ++k) cp = (cp << 6) | (static_cast<unsigned char>(s[i + k]) & 0x3Fu);
        v.push_back(cp);
        i += n;
    }
    return v;
}

// Analiza un texto de GD: cada caracter dibujado es un hijo con tag = indice del caracter en la cadena. Un indice sin hijo =
// la fuente no tiene ese glifo (no se dibuja). Tambien agrupa los glifos por linea para medir la separacion REAL entre lineas.
inline void analyzeLabel(CCLabelBMFont* l, std::string& missing, std::string& lineInfo) {
    std::string str = l->getString() ? l->getString() : "";
    auto cps = utf8ToCodepoints(str);
    std::set<int> tags;
    std::vector<float> ys;
    if (auto* children = l->getChildren()) {
        for (auto* c : CCArrayExt<CCNode*>(children)) {
            tags.insert(c->getTag());
            CCSize cs = c->getContentSize();
            if (cs.width > 0.f && cs.height > 0.f) ys.push_back(c->getPositionY());
        }
    }
    std::ostringstream m;
    int nMissing = 0;
    for (size_t i = 0; i < cps.size(); ++i) {
        if (cps[i] == '\n' || tags.count(int(i))) continue;
        char b[48];
        std::snprintf(b, sizeof b, "%sindice %zu = U+%04X", nMissing ? "; " : "", i, static_cast<unsigned>(cps[i]));
        m << b;
        ++nMissing;
    }
    missing = nMissing ? m.str() : "";

    std::sort(ys.begin(), ys.end(), std::greater<float>());
    std::vector<float> lines;                      // y local medio de cada linea
    float sum = 0; int cnt = 0;
    for (size_t i = 0; i < ys.size(); ++i) {
        if (cnt && std::abs(ys[i] - ys[i - 1]) > 7.f) { lines.push_back(sum / float(cnt)); sum = 0; cnt = 0; }
        sum += ys[i]; ++cnt;
    }
    if (cnt) lines.push_back(sum / float(cnt));
    CCPoint w0 = l->convertToWorldSpace(ccp(0, 0)), w1 = l->convertToWorldSpace(ccp(0, 1));
    float perUnit = std::sqrt((w1.x - w0.x) * (w1.x - w0.x) + (w1.y - w0.y) * (w1.y - w0.y));
    std::ostringstream o;
    o << std::fixed << std::setprecision(1);
    o << "lineas=" << lines.size() << " (saltos de linea en la cadena=" << std::count(str.begin(), str.end(), '\n') << ")";
    if (!lines.empty()) {
        o << " y_local=[";
        for (size_t i = 0; i < lines.size(); ++i) o << (i ? ", " : "") << lines[i];
        o << "]";
        if (lines.size() > 1) {
            o << " paso_local=[";
            for (size_t i = 1; i < lines.size(); ++i) o << (i > 1 ? ", " : "") << (lines[i - 1] - lines[i]);
            o << "] paso_mundo=[";
            for (size_t i = 1; i < lines.size(); ++i) o << (i > 1 ? ", " : "") << (lines[i - 1] - lines[i]) * perUnit;
            o << "]";
        }
    }
    lineInfo = o.str();
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
    char cb[160];
    if (auto* l = typeinfo_cast<CCLabelBMFont*>(n)) {
        r.kind = 1;
        r.text = l->getString() ? l->getString() : "";
        analyzeLabel(l, r.missing, r.lineInfo);
        ccColor3B c = l->getColor();
        std::snprintf(cb, sizeof cb, " | color=(%d,%d,%d) op=%d", int(c.r), int(c.g), int(c.b), int(l->getOpacity()));
        extra += " | texto=\"" + escapeText(r.text) + "\"" + cb;
        extra += " | " + r.lineInfo;
        if (!r.missing.empty()) extra += " | GLIFOS AUSENTES (la fuente no los tiene): " + r.missing;
    } else if (auto* sp = typeinfo_cast<CCSprite*>(n)) {
        CCRect tr = sp->getTextureRect();
        ccColor3B c = sp->getColor();
        std::snprintf(cb, sizeof cb, " | textura=%.0fx%.0f en (%.0f,%.0f) flipX=%d flipY=%d color=(%d,%d,%d) op=%d",
                      tr.size.width, tr.size.height, tr.origin.x, tr.origin.y, int(sp->isFlipX()), int(sp->isFlipY()),
                      int(c.r), int(c.g), int(c.b), int(sp->getOpacity()));
        extra += cb;
    } else if (auto* lc = typeinfo_cast<CCLayerColor*>(n)) {
        ccColor3B c = lc->getColor();
        std::snprintf(cb, sizeof cb, " | color=(%d,%d,%d) op=%d", int(c.r), int(c.g), int(c.b), int(lc->getOpacity()));
        extra += cb;
    }
    if (auto* ly = typeinfo_cast<CCLayer*>(n)) {
        std::snprintf(cb, sizeof cb, " | touch=%d prioridad=%d", int(ly->isTouchEnabled()), int(ly->getTouchPriority()));
        extra += cb;
    }
    if (auto* it = typeinfo_cast<CCMenuItem*>(n)) {
        r.kind = 2;
        r.on = it->isEnabled();
        extra += std::string(" | boton activo=") + (r.on ? "si" : "no");
        if (auto* tg = typeinfo_cast<CCMenuItemToggler*>(n)) {
            r.state = tg->isToggled() ? "marcado" : "sin marcar";
            extra += std::string(" marcado=") + (tg->isToggled() ? "si" : "no");
        }
    }

    int idx = int(recs.size());
    recs.push_back(r);

    CCPoint pos = n->getPosition(), anc = n->getAnchorPoint();
    CCSize sz = n->getContentSize();
    size_t nch = n->getChildren() ? n->getChildrenCount() : 0;
    char buf[560];
    std::snprintf(buf, sizeof buf,
        "[%d] %s tag=%d z=%d vis=%d(%d) pos=(%.2f,%.2f) ancla=(%.2f,%.2f) tam=(%.2f,%.2f) escala=(%.3f,%.3f) sesgo=(%.1f,%.1f) rot=%.1f hijos=%zu"
        " | mundo=[%.1f,%.1f -> %.1f,%.1f] %.1fx%.1f",
        idx, cls.c_str(), n->getTag(), n->getZOrder(), int(n->isVisible()), int(r.visible),
        pos.x, pos.y, anc.x, anc.y, sz.width, sz.height, n->getScaleX(), n->getScaleY(), n->getSkewX(), n->getSkewY(),
        n->getRotation(), nch, r.x0, r.y0, r.x1, r.y1, r.x1 - r.x0, r.y1 - r.y0);
    out << std::string(size_t(depth) * 2, ' ') << buf;
    if (!id.empty()) out << " id=" << id;
    out << extra << "\n";

    if (auto* children = n->getChildren()) {
        for (auto* c : CCArrayExt<CCNode*>(children)) walk(c, idx, depth + 1, recs[size_t(idx)].visible, recs, out);
    }
}

inline bool isAncestor(std::vector<Rec> const& recs, int anc, int node) {
    for (int p = recs[size_t(node)].parent; p >= 0; p = recs[size_t(p)].parent) if (p == anc) return true;
    return false;
}

// Arbol completo de UNA capa + resumenes, solapes y desbordes respecto a `frame` (el marco visible de esa ventana).
inline void dumpLayer(CCNode* root, CCNode* frame, std::ostringstream& out) {
    float fx0 = 0, fy0 = 0, fx1 = 0, fy1 = 0;
    if (frame) worldBox(frame, fx0, fy0, fx1, fy1);
    out << "--- MARCO (mundo) ---\n";
    out << "[" << fx0 << "," << fy0 << " -> " << fx1 << "," << fy1 << "]  " << (fx1 - fx0) << "x" << (fy1 - fy0) << "\n\n";

    out << "--- ARBOL DE NODOS ---\n";
    out << "[indice] clase tag z vis=propia(real) pos ancla tam escala sesgo rot hijos | mundo=[caja en pantalla] ancho x alto | extras\n";
    std::vector<Rec> recs;
    walk(root, -1, 0, true, recs, out);

    out << "\n--- RESUMEN DE TEXTOS (visibles) ---\n";
    out << "[indice] caja en pantalla | texto literal | glifos ausentes\n";
    int nt = 0;
    for (size_t i = 0; i < recs.size(); ++i) {
        Rec const& a = recs[i];
        if (a.kind != 1 || !a.visible || a.text.empty()) continue;
        char buf[200];
        std::snprintf(buf, sizeof buf, "[%zu] [%.1f,%.1f -> %.1f,%.1f] %.1fx%.1f | ", i, a.x0, a.y0, a.x1, a.y1, a.x1 - a.x0, a.y1 - a.y0);
        out << buf << "\"" << escapeText(a.text) << "\" | " << (a.missing.empty() ? "(todos los glifos existen)" : a.missing) << "\n";
        ++nt;
    }
    if (!nt) out << "(ninguno)\n";

    out << "\n--- RESUMEN DE BOTONES (visibles) ---\n";
    int nb = 0;
    for (size_t i = 0; i < recs.size(); ++i) {
        Rec const& a = recs[i];
        if (a.kind != 2 || !a.visible) continue;
        char buf[200];
        std::snprintf(buf, sizeof buf, "[%zu] %s [%.1f,%.1f -> %.1f,%.1f] %.1fx%.1f activo=%s %s\n", i, a.label.c_str(), a.x0, a.y0, a.x1, a.y1,
                      a.x1 - a.x0, a.y1 - a.y0, a.on ? "si" : "no", a.state.c_str());
        out << buf;
        ++nb;
    }
    if (!nb) out << "(ninguno)\n";

    out << "\n--- SOLAPES entre textos/botones (visibles) ---\n";
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
            float area = ox * oy, minArea = std::min(aw * ah, bw * bh);     // (no llamar "small": es una macro de Windows)
            float pct = minArea > 0 ? 100.f * area / minArea : 0.f;
            if (pct < 5.f) continue;
            char buf[400];
            std::snprintf(buf, sizeof buf, "[%zu] %s  <->  [%zu] %s : solape %.1fx%.1f (%.0f%% del menor)\n",
                          i, a.label.c_str(), j, b.label.c_str(), ox, oy, pct);
            out << buf;
            ++overlaps;
        }
    }
    if (!overlaps) out << "(ninguno)\n";

    out << "\n--- TEXTOS/BOTONES FUERA DEL MARCO (margen 0.5) ---\n";
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
}

// Instantanea completa: cabecera del mod + escena + TODAS las ventanas (FLAlertLayer/Popup) que haya encima, cada una con su arbol entero.
inline std::string dumpScene(std::string const& header) {
    std::ostringstream out;
    out << std::fixed << std::setprecision(1);
    out << header << "\n";

    auto* scene = CCDirector::get()->getRunningScene();
    out << "=== ESCENA EN EJECUCION ===\n";
    if (!scene) { out << "(ninguna)\n"; return out.str(); }
    out << "clase=" << className(scene) << " hijos=" << scene->getChildrenCount() << "\n";
    std::vector<FLAlertLayer*> alerts;
    if (auto* children = scene->getChildren()) {
        int i = 0;
        for (auto* c : CCArrayExt<CCNode*>(children)) {
            float x0, y0, x1, y1;
            worldBox(c, x0, y0, x1, y1);
            CCPoint p = c->getPosition(); CCSize s = c->getContentSize();
            char buf[400];
            std::snprintf(buf, sizeof buf, "  (%d) %s tag=%d z=%d vis=%d pos=(%.1f,%.1f) tam=(%.1f,%.1f) escala=(%.3f,%.3f) hijos=%zu | mundo=[%.1f,%.1f -> %.1f,%.1f]\n",
                          i++, className(c).c_str(), c->getTag(), c->getZOrder(), int(c->isVisible()), p.x, p.y, s.width, s.height,
                          c->getScaleX(), c->getScaleY(), c->getChildrenCount(), x0, y0, x1, y1);
            out << buf;
            if (auto* al = typeinfo_cast<FLAlertLayer*>(c)) alerts.push_back(al);
        }
    }

    int k = 0;
    for (auto* al : alerts) {
        out << "\n############################################################\n";
        out << "### VENTANA " << ++k << " de " << alerts.size() << ": " << className(al) << "  z=" << al->getZOrder() << " vis=" << int(al->isVisible()) << "\n";
        out << "############################################################\n";
        dumpLayer(al, al->m_mainLayer, out);
    }
    if (alerts.empty()) out << "\n(no hay ninguna ventana FLAlertLayer/Popup en la escena)\n";
    return out.str();
}

} // namespace uidump
