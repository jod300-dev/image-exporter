#include "ExportPopup.hpp"
#include "UiDump.hpp"
#include "InfoPopup.hpp"

#include <Geode/ui/GeodeUI.hpp>
#include <Geode/ui/Notification.hpp>
#include <Geode/utils/file.hpp>
#include <Geode/utils/async.hpp>
#include <thread>
#include <ctime>
#include <sstream>

#define STBI_NO_STDIO
#include "stb_image.h"

namespace {
    constexpr int kMaxObjects = 10000;           // tope duro pedido: 10k objetos
    constexpr int kMinObjects = 500;
    constexpr float kPopupW = 430.f, kPopupH = 288.f;     // cabe en los 320 de alto de la pantalla de GD (deja ~16 arriba y abajo para el boton X)
    constexpr float kPrevSize = 140.f;
    constexpr int kMinSize = 150, kMaxSize = 6000;        // lado largo, unidades GD
    constexpr int kStyleCount = int(gdx::Method::COUNT) - gdx::kNormalMethodCount;          // estilos ligeros

    // Canales que este mod ya ha repartido en el editor abierto. Se suman a los detectados en el nivel: asi, aunque GD no
    // devuelva algun canal alto (>= 1020) como "usado", dos imagenes colocadas seguidas NUNCA comparten canal (y no se mezclan los colores).
    std::set<int> s_claimed;
    LevelEditorLayer* s_claimedFor = nullptr;

    std::vector<std::string> s_snaps;              // instantaneas del diagnostico de esta sesion
    int s_snapCount = 0;

    int qualityFromSetting() {
        auto q = Mod::get()->getSettingValue<std::string>("quality");
        if (q == "Fast" || q == "Rapido") return 0;      // (the old Spanish values still work)
        if (q == "Normal") return 1;
        if (q == "Maximum" || q == "Maximo") return 3;
        return 2;                                  // High (default)
    }
    // sliders: 0..1 <-> valor
    float budgetToSlider(int b) { return std::clamp(float(b - kMinObjects) / float(kMaxObjects - kMinObjects), 0.f, 1.f); }
    int sliderToBudget(float v) { return kMinObjects + int(std::lround(std::clamp(v, 0.f, 1.f) * 95.f)) * 100; }    // pasos de 100
    float sizeToSlider(int u) { return std::clamp(float(std::log(double(u) / kMinSize) / std::log(double(kMaxSize) / kMinSize)), 0.f, 1.f); }
    int sliderToSize(float v) {
        double u = kMinSize * std::pow(double(kMaxSize) / kMinSize, std::clamp(v, 0.f, 1.f));
        int step = u < 600 ? 10 : u < 2000 ? 50 : 100;
        return std::clamp(int(std::lround(u / step)) * step, kMinSize, kMaxSize);
    }
}

ExportPopup* ExportPopup::create(EditorUI* editor) {
    auto ret = new ExportPopup();
    if (ret->init(editor)) {
        ret->autorelease();
        return ret;
    }
    delete ret;
    return nullptr;
}

ExportPopup::~ExportPopup() {
    if (m_job) m_job->prog.cancel.store(true);
}

// Parte el texto en líneas cuyo ancho FINAL (con la escala aplicada) no pase de maxWidth. Se mide con una etiqueta real
// en vez de fiarse del ancho de ajuste de línea de CCLabelBMFont, que no se comporta igual con distintas escalas.
std::string ExportPopup::wrapText(std::string const& text, float scale, float maxWidth) {
    std::string out, line, word;
    for (size_t i = 0; i <= text.size(); ++i) {
        if (i < text.size() && text[i] != ' ') { word += text[i]; continue; }
        if (word.empty()) continue;
        std::string cand = line.empty() ? word : line + " " + word;
        m_measure->setString(cand.c_str());
        if (!line.empty() && m_measure->getContentSize().width * scale > maxWidth) { out += line + "\n"; line = word; }
        else line = cand;
        word.clear();
    }
    return out + line;
}

bool ExportPopup::init(EditorUI* editor) {
    if (!Popup::init(kPopupW, kPopupH)) return false;
    m_editor = editor;
    s_snaps.clear();                         // nueva sesion de diagnostico
    s_snapCount = 0;
    m_measure = CCLabelBMFont::create("", "chatFont.fnt");
    this->setTitle("Image Exporter");

    // ajustes recordados de la última vez
    auto* mod = Mod::get();
    m_tab = std::clamp(mod->getSavedValue<int>("tab", 0), 0, 1);
    m_method = std::clamp(mod->getSavedValue<int>("method", 2), 0, gdx::kNormalMethodCount - 1);
    m_style = std::clamp(mod->getSavedValue<int>("style", 0), 0, kStyleCount - 1);
    m_mainMode = std::clamp(mod->getSavedValue<int>("main-mode", 0), 0, 3);
    m_colorCheck = mod->getSavedValue<bool>("color-channels", false);
    m_budget = std::clamp(mod->getSavedValue<int>("budget", kMaxObjects), kMinObjects, kMaxObjects);
    m_sizeU = std::clamp(mod->getSavedValue<int>("size-u", 600), kMinSize, kMaxSize);

    auto put = [&](CCNode* n, float x, float y) { m_mainLayer->addChildAtPosition(n, Anchor::TopLeft, ccp(x, -y)); };
    auto putBtn = [&](CCNode* n, float x, float y) { m_buttonMenu->addChildAtPosition(n, Anchor::TopLeft, ccp(x, -y)); };
    auto label = [&](char const* text, char const* font, float scale, float x, float y, float ax = 0.5f) {
        auto l = CCLabelBMFont::create(text, font);
        l->setScale(scale);
        l->setAnchorPoint({ax, 0.5f});
        put(l, x, y);
        return l;
    };

    // ============ columna izquierda: vista previa y estado ============
    m_previewHolder = CCNode::create();
    m_previewHolder->setContentSize({kPrevSize, kPrevSize});
    m_previewHolder->setAnchorPoint({0.5f, 0.5f});
    m_previewHolder->addChild(CCLayerColor::create(ccc4(0, 0, 0, 120), kPrevSize, kPrevSize));
    auto hint = CCLabelBMFont::create("Preview", "chatFont.fnt");
    hint->setOpacity(120);
    hint->setPosition({kPrevSize / 2, kPrevSize / 2});
    m_previewHolder->addChild(hint);
    put(m_previewHolder, 84.f, 104.f);

    m_statA = label("Choose an image", "chatFont.fnt", 0.85f, 84.f, 186.f);
    m_statB = label("", "chatFont.fnt", 0.75f, 84.f, 199.f);
    // La nota (naranja) se dibuja con 3 etiquetas, una por linea, a separacion fija; asi no depende del interlineado de la fuente.
    for (int i = 0; i < 3; ++i) {
        auto l = CCLabelBMFont::create("", "chatFont.fnt");
        l->setScale(0.6f);
        l->setColor({255, 200, 80});
        l->setAnchorPoint({0.5f, 1.f});
        put(l, 84.f, 210.f + 9.5f * float(i));
        m_noteLines.push_back(l);
    }

    // ============ columna derecha ============
    // pestañas
    // Los ButtonSprite miden 39 de alto fijos; se reducen con la escala del sprite (el texto sube a 0.46 para
    // que se lea igual que antes) para que no pisen el titulo ni el boton Ajustes.
    auto makeTab = [&](char const* text, SEL_MenuHandler cb) {
        auto off = ButtonSprite::create(text, "bigFont.fnt", "GJ_button_04.png", 0.46f);
        auto on = ButtonSprite::create(text, "bigFont.fnt", "GJ_button_01.png", 0.46f);
        off->setScale(0.72f);
        on->setScale(0.72f);
        return CCMenuItemToggler::create(off, on, this, cb);
    };
    m_tabMain = makeTab("Main", menu_selector(ExportPopup::onTabMain));
    m_tabLight = makeTab("Light", menu_selector(ExportPopup::onTabLight));
    putBtn(m_tabMain, 262.f, 50.f);
    putBtn(m_tabLight, 335.f, 50.f);

    // método: flechas + nombre + descripción
    auto arrow = [&](bool flip, SEL_MenuHandler cb, float x, float y) {
        auto spr = CCSprite::createWithSpriteFrameName("GJ_arrow_03_001.png");
        spr->setScale(0.5f);
        spr->setFlipX(flip);
        putBtn(CCMenuItemSpriteExtra::create(spr, this, cb), x, y);
    };
    arrow(false, menu_selector(ExportPopup::onPrev), 178.f, 79.f);
    arrow(true,  menu_selector(ExportPopup::onNext), 406.f, 79.f);
    m_methodLabel = label("", "bigFont.fnt", 0.5f, 292.f, 79.f);
    // La descripcion tambien va en etiquetas sueltas (hasta 3 lineas, separacion fija de 11.5): una sola etiqueta de varias
    // lineas dejaba un hueco enorme entre la primera y la segunda linea.
    for (int i = 0; i < 3; ++i) {
        auto l = CCLabelBMFont::create("", "chatFont.fnt");
        l->setScale(0.7f);
        l->setAnchorPoint({0.5f, 1.f});
        put(l, 292.f, 92.f + 11.5f * float(i));
        m_descLines.push_back(l);
    }

    // sliders: objetos y lado largo
    label("Objects", "bigFont.fnt", 0.34f, 168.f, 136.f, 0.f);
    m_budgetValue = label("", "bigFont.fnt", 0.34f, 418.f, 136.f, 1.f);
    m_budgetSlider = Slider::create(this, menu_selector(ExportPopup::onBudgetSlider), 0.78f);
    m_budgetSlider->setValue(budgetToSlider(m_budget));
    put(m_budgetSlider, 292.f, 157.f);

    label("Long side", "bigFont.fnt", 0.34f, 168.f, 182.f, 0.f);
    m_sizeValue = label("", "bigFont.fnt", 0.34f, 418.f, 182.f, 1.f);
    m_sizeSlider = Slider::create(this, menu_selector(ExportPopup::onSizeSlider), 0.78f);
    m_sizeSlider->setValue(sizeToSlider(m_sizeU));
    put(m_sizeSlider, 292.f, 203.f);

    // fila de color: cambia según la pestaña
    auto addCheck = [&](CCMenuItemToggler*& slot, std::vector<CCNode*>& group, SEL_MenuHandler cb, bool on, float x, char const* text) {
        slot = CCMenuItemToggler::createWithStandardSprites(this, cb, 0.5f);
        slot->toggle(on);
        putBtn(slot, x, 229.f);
        auto l = label(text, "bigFont.fnt", 0.28f, x + 12.f, 229.f, 0.f);
        group.push_back(slot);
        group.push_back(l);
    };
    addCheck(m_chkChannels, m_mainOpts, menu_selector(ExportPopup::onMainChannels), m_mainMode == 1, 174.f, "Channels");
    addCheck(m_chkGrayHsv,  m_mainOpts, menu_selector(ExportPopup::onMainGrayHsv),  m_mainMode == 2, 256.f, "Gray HSV");
    addCheck(m_chkGrayCh,   m_mainOpts, menu_selector(ExportPopup::onMainGrayCh),   m_mainMode == 3, 338.f, "Gray channels");
    addCheck(m_chkColor,    m_lightOpts, menu_selector(ExportPopup::onColorToggle), m_colorCheck, 174.f, "Colors in channels (no HSV)");

    // botones de acción
    // Botones mas bajos (escala del sprite 0.8 con el texto a 0.52: se lee igual) centrados bajo la columna derecha.
    auto pickSpr = ButtonSprite::create("Image", "bigFont.fnt", "GJ_button_01.png", 0.52f);
    pickSpr->setScale(0.8f);
    auto pickBtn = CCMenuItemSpriteExtra::create(pickSpr, this, menu_selector(ExportPopup::onPick));
    putBtn(pickBtn, 252.f, 260.f);
    auto placeSpr = ButtonSprite::create("Place", "bigFont.fnt", "GJ_button_02.png", 0.52f);
    placeSpr->setScale(0.8f);
    auto placeBtn = CCMenuItemSpriteExtra::create(placeSpr, this, menu_selector(ExportPopup::onPlace));
    putBtn(placeBtn, 327.f, 260.f);
    // Fila del titulo: Ajustes e info a la derecha, dentro del marco y sin tocar las pestanas.
    auto infoBtn = CCMenuItemSpriteExtra::create(CCSprite::createWithSpriteFrameName("GJ_infoIcon_001.png"), this, menu_selector(ExportPopup::onInfo));
    putBtn(infoBtn, 408.f, 21.f);
    // Ajustes: misma fila, mismo alto y mismo estilo de texto que Imagen / Colocar, centrado bajo la vista previa.
    auto setSpr = ButtonSprite::create("Settings", "bigFont.fnt", "GJ_button_04.png", 0.52f);
    setSpr->setScale(0.8f);
    auto setBtn = CCMenuItemSpriteExtra::create(setSpr, this, menu_selector(ExportPopup::onSettings));
    putBtn(setBtn, 84.f, 260.f);

    this->schedule(schedule_selector(ExportPopup::tick), 0.1f);
    this->setTab(m_tab);                       // aplica pestaña, etiquetas y visibilidad
    // Diagnostico de la interfaz (ajuste "dump-ui"): 1 s despues, cuando ya termino la animacion de entrada y las cajas son las finales
    this->dumpUiAfter("window opened", 1.0f);
    return true;
}

// ------------------------------------------------------------- etiquetas ---

gdx::Method ExportPopup::effectiveMethod() const {
    return m_tab == 1 ? gdx::Method(gdx::kNormalMethodCount + m_style) : gdx::Method(m_method);
}

// CCMenuItemToggler cambia su propio estado justo DESPUES de ejecutar el callback. Si el callback ya lo habia puesto como
// queria, el toggler lo invierte y las dos pestanas quedaban grises (y los checks de Principales desincronizados).
// Por eso el estado real se vuelve a aplicar en el siguiente frame, cuando el toggler ya termino de cambiar.
void ExportPopup::syncToggles(float) {
    m_tabMain->toggle(m_tab == 0);
    m_tabLight->toggle(m_tab == 1);
    m_chkChannels->toggle(m_mainMode == 1);
    m_chkGrayHsv->toggle(m_mainMode == 2);
    m_chkGrayCh->toggle(m_mainMode == 3);
}

void ExportPopup::setTab(int tab) {
    m_tab = tab;
    this->syncToggles();
    this->scheduleOnce(schedule_selector(ExportPopup::syncToggles), 0.f);
    for (auto* n : m_mainOpts) n->setVisible(tab == 0);
    for (auto* n : m_lightOpts) n->setVisible(tab == 1);
    this->refreshLabels();
    this->scheduleRecompute();
}

void ExportPopup::refreshLabels() {
    auto const& info = gdx::methodInfo(effectiveMethod());
    m_methodLabel->setString(info.name);
    m_methodLabel->limitLabelWidth(210.f, 0.5f, 0.2f);
    this->setLines(m_descLines, wrapText(info.desc, 0.7f, 244.f), 0.7f);

    char buf[64];
    std::snprintf(buf, sizeof buf, "%d", m_budget);
    m_budgetValue->setString(buf);
    std::snprintf(buf, sizeof buf, "%d u (%.1f blocks)", m_sizeU, m_sizeU / 30.0);
    m_sizeValue->setString(buf);

    auto* mod = Mod::get();
    mod->setSavedValue<int>("tab", m_tab);
    mod->setSavedValue<int>("method", m_method);
    mod->setSavedValue<int>("style", m_style);
    mod->setSavedValue<int>("main-mode", m_mainMode);
    mod->setSavedValue<bool>("color-channels", m_colorCheck);
    mod->setSavedValue<int>("budget", m_budget);
    mod->setSavedValue<int>("size-u", m_sizeU);
}

// Reparte un texto ya partido (lineas separadas por '\n') en las etiquetas sueltas. Si sobran lineas, la ultima termina en "...".
void ExportPopup::setLines(std::vector<CCLabelBMFont*> const& lines, std::string const& text, float) {
    std::vector<std::string> parts;
    std::string cur;
    for (char c : text) {
        if (c == '\n') { parts.push_back(cur); cur.clear(); }
        else cur += c;
    }
    parts.push_back(cur);
    if (&lines == &m_descLines) m_descText = text; else m_noteText = text;
    size_t n = lines.size();
    if (parts.size() > n) {
        parts.resize(n);
        parts.back() += " ...";
    }
    for (size_t i = 0; i < n; ++i) lines[i]->setString(i < parts.size() ? parts[i].c_str() : "");
}

void ExportPopup::setStatus(std::string const& a, std::string const& b, std::string const& note) {
    m_statA->setString(a.c_str());
    m_statA->limitLabelWidth(140.f, 0.85f, 0.4f);
    m_statB->setString(b.c_str());
    m_statB->limitLabelWidth(140.f, 0.75f, 0.4f);
    this->setLines(m_noteLines, wrapText(note, 0.6f, 140.f), 0.6f);
}

void ExportPopup::stepMethod(int dir) {
    if (m_tab == 0) m_method = (m_method + dir + gdx::kNormalMethodCount) % gdx::kNormalMethodCount;
    else m_style = (m_style + dir + kStyleCount) % kStyleCount;
    this->refreshLabels();
    this->scheduleRecompute();
}

void ExportPopup::onBudgetSlider(CCObject* sender) {
    int b = sliderToBudget(static_cast<SliderThumb*>(sender)->getValue());
    if (b == m_budget) return;
    m_budget = b;
    this->refreshLabels();
    this->scheduleRecompute();             // se recalcula 0,35 s después de soltar
}

void ExportPopup::onSizeSlider(CCObject* sender) {
    m_sizeU = sliderToSize(static_cast<SliderThumb*>(sender)->getValue());
    this->refreshLabels();                 // el tamaño solo afecta a la colocación: no hace falta recalcular
}

// Los tres checks de Principales funcionan como una selección única: ninguno = HSV a color.
void ExportPopup::setMainMode(int mode) {
    m_mainMode = mode;
    this->syncToggles();
    this->scheduleOnce(schedule_selector(ExportPopup::syncToggles), 0.f);
    this->refreshLabels();
    if (m_tab == 0) this->scheduleRecompute();
}

void ExportPopup::onColorToggle(CCObject*) {
    m_colorCheck = !m_colorCheck;                 // en el callback el toggler aún no ha cambiado su estado
    this->refreshLabels();
    if (m_tab == 1) this->scheduleRecompute();
}

void ExportPopup::onSettings(CCObject*) {
    openSettingsPopup(Mod::get());
    this->dumpUiAfter("settings window opened", 1.2f);
}

// ------------------------------------------------------- diagnostico de UI ---

// Cabecera del txt: version, pantalla, estado del mod y datos del resultado (incluye cuantas formas sobresalen de la imagen).
std::string ExportPopup::dumpHeader() const {
    auto* mod = Mod::get();
    std::ostringstream h;
    h << std::fixed << std::setprecision(2);
    std::time_t now = std::time(nullptr);
    char tbuf[48] = "";
    std::tm tmv{};
#if defined(GEODE_IS_WINDOWS)
    localtime_s(&tmv, &now);
#else
    localtime_r(&now, &tmv);
#endif
    std::strftime(tbuf, sizeof tbuf, "%Y-%m-%d %H:%M:%S", &tmv);
    h << "=== Image Exporter :: interface diagnostics ===\n";
    h << "date: " << tbuf << "\n";
    h << "mod: " << mod->getVersion().toVString() << "   geode: " << Loader::get()->getVersion().toVString()
      << "   gd: " << Loader::get()->getGameVersion() << "   platform: " << GEODE_PLATFORM_NAME << "\n";

    auto* dir = CCDirector::get();
    CCSize win = dir->getWinSize(), winPx = dir->getWinSizeInPixels();
    h << "winSize: " << win.width << "x" << win.height << "   winSizePixels: " << winPx.width << "x" << winPx.height
      << "   contentScaleFactor: " << dir->getContentScaleFactor() << "\n";
    if (auto* view = dir->getOpenGLView()) {
        CCSize fs = view->getFrameSize();
        h << "game window (frame): " << fs.width << "x" << fs.height << "\n";
    }

    auto info = [&](char const* name, CCNode* n) {
        if (!n) { h << name << ": (null)\n"; return; }
        CCSize s = n->getContentSize(); CCPoint p = n->getPosition(), a = n->getAnchorPoint();
        h << name << ": size=" << s.width << "x" << s.height << " pos=(" << p.x << "," << p.y << ") anchor=(" << a.x << "," << a.y
          << ") scale=" << n->getScaleX() << "," << n->getScaleY() << "\n";
    };
    h << "nominal popup: " << kPopupW << "x" << kPopupH << "\n";
    info("m_mainLayer", m_mainLayer);
    info("m_buttonMenu", m_buttonMenu);

    {
        auto used = usedChannels();
        int nHigh = 0;
        for (int c : used) if (c >= gdx::kChanHighMin) ++nHigh;
        h << "\n=== COLOR CHANNELS ===\n";
        h << "occupied channels detected: " << used.size() << " (in the 1020+ range: " << nHigh << ")   usable free: "
          << gdx::countFreeChannels(used) << "   reserved by the mod this session: " << s_claimed.size() << "\n";
        h << "occupied >= 1000: ";
        int shown = 0;
        for (int c : used) if (c >= 1000 && shown++ < 120) h << c << " ";
        h << (shown ? "" : "(none)") << "\n";
        h << "first 40 occupied: ";
        shown = 0;
        for (int c : used) if (shown++ < 40) h << c << " ";
        h << "\n";
    }

    h << "\n=== MOD STATE ===\n";
    h << "tab=" << m_tab << " method=" << m_method << " style=" << m_style << " mainMode=" << m_mainMode
      << " colorChannels=" << int(m_colorCheck) << " objects=" << m_budget << " longSide=" << m_sizeU << "u\n";
    h << "effective method: " << gdx::methodInfo(effectiveMethod()).name << "\n";
    h << "settings: quality=" << mod->getSettingValue<std::string>("quality") << " max-raster=" << mod->getSettingValue<int>("max-raster")
      << " alpha-threshold=" << mod->getSettingValue<int>("alpha-threshold") << " bleed=" << mod->getSettingValue<double>("bleed")
      << " flip-rotation=" << int(mod->getSettingValue<bool>("flip-rotation")) << "\n";
    auto text = [&](char const* name, CCLabelBMFont* l) { h << name << ": \"" << (l && l->getString() ? l->getString() : "") << "\"\n"; };
    text("method label", m_methodLabel);
    h << "description label (" << m_descLines.size() << " labels, one per line): \"" << uidump::escapeText(m_descText) << "\"\n";
    text("status A", m_statA);
    text("status B", m_statB);
    h << "note (" << m_noteLines.size() << " labels, one per line): \"" << uidump::escapeText(m_noteText) << "\"\n";

    h << "\n=== IMAGE AND RESULT ===\n";
    if (m_image) h << "original image: " << m_image->w << "x" << m_image->h << "\n"; else h << "original image: (none)\n";
    if (m_hasResult) {
        int outside = 0, rotated = 0; double worst = 0;
        for (auto const& s : m_result.shapes) {
            double rad = s.rot * 3.14159265358979323846 / 180.0, cs = std::cos(rad), sn = std::sin(rad), hx = s.w * 0.5, hy = s.h * 0.5;
            double ex = std::abs(hx * cs) + std::abs(hy * sn), ey = std::abs(hx * sn) + std::abs(hy * cs);
            double ov = std::max({0.0, ex - s.cx, s.cx + ex - m_result.rasterW, ey - s.cy, s.cy + ey - m_result.rasterH});
            if (std::fmod(std::abs(s.rot), 90.0) > 1e-3) ++rotated;
            if (ov > 0.05) { ++outside; worst = std::max(worst, ov); }
        }
        h << "shapes=" << m_result.shapes.size() << " textRows=" << m_result.textRows.size() << " raster=" << m_result.rasterW << "x"
          << m_result.rasterH << " aspect=" << m_result.aspect << " psnr=" << m_result.psnr << " ms=" << m_result.ms << "\n";
        h << "hasTransparency=" << int(m_result.hadAlpha) << " mono=" << int(m_result.mono) << " withChannels=" << int(m_result.colored)
          << " rotated=" << rotated << "\n";
        h << "shapes that STICK OUT of the image: " << outside << " (worst: " << worst << " raster px)\n";
        h << "engine note: \"" << m_result.note << "\"\n";
    } else {
        h << "(no result yet)\n";
    }
    return h.str();
}

// Programa una instantanea del diagnostico dentro de `delay` segundos (da tiempo a que termine la animacion de entrada de las ventanas).
void ExportPopup::dumpUiAfter(char const* reason, float delay) {
    if (!Mod::get()->getSettingValue<bool>("dump-ui")) return;
    m_dumpReason = reason;
    this->scheduleOnce(schedule_selector(ExportPopup::dumpUi), delay);
}

// Escribe <carpeta de guardado del mod>/ui-dump.txt con TODAS las instantaneas de esta sesion (cada vez que abres el mod se empieza
// de cero). Solo actua con el ajuste "Guardar diagnostico de la interfaz" activo.
void ExportPopup::dumpUi(float) {
    auto* mod = Mod::get();
    if (!mod->getSettingValue<bool>("dump-ui")) return;
    std::string text;
    try {
        text = uidump::dumpScene(dumpHeader());
    } catch (...) {
        text = "Internal error while generating the diagnostics.\n";
    }
    std::time_t now = std::time(nullptr);
    char tb[32] = "";
    std::tm tmv{};
#if defined(GEODE_IS_WINDOWS)
    localtime_s(&tmv, &now);
#else
    localtime_r(&now, &tmv);
#endif
    std::strftime(tb, sizeof tb, "%H:%M:%S", &tmv);
    s_snaps.push_back("\n\n################################################################################\n"
                      "##### SNAPSHOT " + std::to_string(++s_snapCount) + " (" + tb + ") - reason: " + m_dumpReason + "\n"
                      "################################################################################\n" + text);
    size_t total = 0;
    for (auto const& x : s_snaps) total += x.size();
    while (total > 4000000 && s_snaps.size() > 1) { total -= s_snaps.front().size(); s_snaps.erase(s_snaps.begin()); }   // tope ~4 MB: se descartan las mas viejas
    std::string all = "Image Exporter - interface diagnostics. Snapshots in this session: " + std::to_string(s_snaps.size()) + "\n";
    for (auto const& x : s_snaps) all += x;

    auto path = mod->getSaveDir() / "ui-dump.txt";
    auto res = utils::file::writeString(path, all);
    std::string shown = utils::string::pathToString(path);
    if (res.isOk()) log::info("Interface diagnostics saved to {}", shown);
    else log::error("Could not save the interface diagnostics to {}", shown);
    if (!m_dumpNotified) {
        m_dumpNotified = true;
        if (res.isOk()) Notification::create("Diagnostics saved to:\n" + shown, NotificationIcon::Success, 6.f)->show();
        else Notification::create("Could not save the diagnostics:\n" + shown, NotificationIcon::Error, 6.f)->show();
    }
}

// ------------------------------------------------------------ cargar imagen --

void ExportPopup::onPick(CCObject*) {
    auto options = file::FilePickOptions{
        std::nullopt,
        {{"Images", {"*.png", "*.jpg", "*.jpeg", "*.bmp", "*.tga", "*.gif"}}}
    };
    async::spawn(
        file::pick(file::PickMode::OpenFile, options),
        [weak = WeakRef<ExportPopup>(this)](auto result) {
            auto self = weak.lock();
            if (!self) return;
            if (!result.isOk()) {
                self->setStatus("Could not open", "the file picker");
                return;
            }
            auto opt = result.unwrap();
            if (!opt) return;                                    // cancelado
            auto bytes = utils::file::readBinary(opt.value());
            if (!bytes.isOk()) {
                self->setStatus("Could not read", "the file");
                return;
            }
            self->loadImageBytes(bytes.unwrap());
        }
    );
}

void ExportPopup::loadImageBytes(std::vector<uint8_t> const& bytes) {
    int w = 0, h = 0, c = 0;
    unsigned char* d = stbi_load_from_memory(bytes.data(), int(bytes.size()), &w, &h, &c, 4);
    if (!d) {
        this->setStatus("Could not read the image", "", "Formats: PNG, JPG, BMP, TGA, GIF");
        return;
    }
    if (int64_t(w) * h > 100000000) {
        stbi_image_free(d);
        this->setStatus("Image too large", "(max. 100 megapixels)");
        return;
    }
    auto img = std::make_shared<gdx::Image>();
    img->w = w; img->h = h;
    img->rgba.assign(d, d + size_t(w) * h * 4);
    stbi_image_free(d);
    m_image = img;
    m_hasResult = false;
    this->recompute();
}

// ----------------------------------------------------------------- cálculo --

gdx::Options ExportPopup::makeOptions() const {
    gdx::Options o;
    auto* mod = Mod::get();
    o.budget = std::clamp(m_budget, kMinObjects, kMaxObjects);      // nunca más de 10.000
    o.maxRaster = std::clamp(mod->getSettingValue<int>("max-raster"), 128, 1024);
    o.alphaThreshold = std::clamp(mod->getSettingValue<int>("alpha-threshold"), 1, 255);
    o.quality = qualityFromSetting();
    o.invertInk = mod->getSettingValue<bool>("invert-ink");
    o.asciiAspect = float(std::clamp(mod->getSettingValue<double>("ascii-aspect"), 0.3, 3.0));
    o.asciiChunk = std::clamp(mod->getSettingValue<int>("ascii-chunk"), 8, 400);
    o.lightSpecialChannels = mod->getSettingValue<bool>("light-special-channels");
    o.colorChannels = colorModeOn();
    o.paletteSize = std::clamp(mod->getSettingValue<int>("palette-size"), 4, 200);
    o.mainChannels = m_tab == 0 && (m_mainMode == 1 || m_mainMode == 3);
    o.grayscale = m_tab == 0 && m_mainMode >= 2;
    o.mainPaletteSize = std::clamp(mod->getSettingValue<int>("palette-size-main"), 2, 250);
    // canales libres del nivel: la paleta nunca puede necesitar más de los que quedan (el 1 no cuenta)
    o.maxChannels = gdx::countFreeChannels(usedChannels());
    return o;
}

gdx::GdExportOptions ExportPopup::makeExportOptions() const {
    auto* mod = Mod::get();
    gdx::GdExportOptions eo;
    eo.longSide = m_sizeU;
    eo.colorChannel = std::clamp(mod->getSettingValue<int>("color-channel"), 1, 1020);
    eo.bleed = std::clamp(mod->getSettingValue<double>("bleed"), 0.0, 2.0);
    eo.flipRotation = mod->getSettingValue<bool>("flip-rotation");
    eo.monoFastPath = m_result.mono && !m_result.colored;          // blanco/negro por los canales especiales 1010/1011
    eo.blackChannel = std::clamp(mod->getSettingValue<int>("black-channel"), 1, 1020);
    eo.whiteChannel = std::clamp(mod->getSettingValue<int>("white-channel"), 1, 1020);
    eo.asciiAdvance = std::clamp(mod->getSettingValue<double>("ascii-advance"), 1.0, 400.0);
    eo.asciiKerning = std::clamp(mod->getSettingValue<int>("ascii-kerning"), -100, 100);
    eo.paletteChannelStart = std::clamp(mod->getSettingValue<int>("palette-first-channel"), 2, 999);
    return eo;
}

void ExportPopup::scheduleRecompute() {
    if (!m_image) return;
    this->unschedule(schedule_selector(ExportPopup::recompute));
    this->scheduleOnce(schedule_selector(ExportPopup::recompute), 0.35f);   // espera a que termines de pulsar
}

void ExportPopup::recompute(float) {
    if (!m_image) return;
    if (m_job) m_job->prog.cancel.store(true);                      // cancela el cálculo anterior

    auto job = std::make_shared<Job>();
    job->id = ++m_jobCounter;
    job->src = m_image;
    job->method = effectiveMethod();
    job->opt = makeOptions();
    job->startMs = gdx::nowMs();
    m_job = job;
    this->setStatus("Calculating... 0%");

    std::thread([job]() {
        try {
            job->result = gdx::convert(*job->src, job->method, job->opt, &job->prog);
        } catch (...) {
            job->result = gdx::Result{};
            job->result.note = "Internal error while calculating";
        }
        job->done.store(true, std::memory_order_release);
    }).detach();
}

void ExportPopup::tick(float) {
    if (!m_job) return;
    if (m_job->done.load(std::memory_order_acquire)) {
        auto job = m_job;
        m_job = nullptr;
        if (job->id == m_jobCounter) this->applyResult(job);
        return;
    }
    // aviso de seguridad: si un cálculo pasa de 90 s se cancela y se avisa, en vez de quedarse en un % para siempre
    if (gdx::nowMs() - m_job->startMs > 90000.0) {
        m_job->prog.cancel.store(true);
        m_job = nullptr;
        this->setStatus("Taking too long", "try fewer objects", "Calculation canceled after 90 s. Try another method or lower the object count.");
        return;
    }
    char buf[48];
    std::snprintf(buf, sizeof buf, "Calculating... %d%%", int(m_job->prog.fraction.load() * 100.f));
    this->setStatus(buf);
}

void ExportPopup::applyResult(std::shared_ptr<Job> job) {
    m_result = std::move(job->result);
    m_hasResult = !m_result.shapes.empty() || !m_result.textRows.empty();
    if (!m_hasResult) {
        this->setStatus("No objects were generated", "", m_result.note);
        return;
    }
    // tamaño aproximado de la cadena de objetos (sin comprimir): sirve para comparar métodos
    size_t kb = gdx::toGdString(m_result, makeExportOptions()).size() / 1024;
    size_t objs = m_result.shapes.size() + m_result.textRows.size();
    char a1[96], b1[96];
    std::snprintf(a1, sizeof a1, "%zu objects | ~%zu KB", objs, kb);
    if (m_result.psnr >= 0) std::snprintf(b1, sizeof b1, "%.1f dB | %.1f s", m_result.psnr, m_result.ms / 1000.0);
    else std::snprintf(b1, sizeof b1, "%.1f s", m_result.ms / 1000.0);
    this->setStatus(a1, b1, m_result.note);
    this->buildPreview();
    m_dumpReason = "result calculated";
    this->dumpUi();                                   // (solo si el ajuste esta activo) refresca el txt con el estado y el resultado
}

void ExportPopup::buildPreview() {
    if (m_preview) { m_preview->removeFromParent(); m_preview = nullptr; }
    int w = m_result.rasterW, h = m_result.rasterH;
    if (w <= 0 || h <= 0) return;
    gdx::Image img;
    if (!m_result.previewImg.empty()) {
        img = m_result.previewImg;                                  // ASCII: bloques de tinta
    } else {
        int k = m_result.mono ? std::clamp(300 / std::max(w, h), 1, 3) : 1;   // las líneas finas necesitan más resolución
        img = gdx::renderShapesScaled(m_result.shapes, w, h, k);
    }
    w = img.w; h = img.h;
    std::vector<uint8_t> buf(size_t(w) * h * 4);
    for (size_t i = 0; i < size_t(w) * h; ++i) {                    // composita sobre gris oscuro
        const uint8_t* p = &img.rgba[i * 4];
        float a = p[3] / 255.f;
        for (int k = 0; k < 3; ++k) buf[i * 4 + k] = uint8_t(p[k] * a + 40 * (1.f - a));
        buf[i * 4 + 3] = 255;
    }
    auto tex = new CCTexture2D();
    if (!tex->initWithData(buf.data(), kCCTexture2DPixelFormat_RGBA8888, w, h, CCSize(float(w), float(h)))) {
        delete tex;
        return;
    }
    tex->autorelease();
    m_preview = CCSprite::createWithTexture(tex);
    if (!m_preview) return;
    float aspect = m_result.aspect > 0 ? m_result.aspect : 1.f;
    float tw = aspect >= 1.f ? kPrevSize : kPrevSize * aspect;
    float th = aspect >= 1.f ? kPrevSize / aspect : kPrevSize;
    m_preview->setScaleX(tw / w);
    m_preview->setScaleY(th / h);
    m_preview->setPosition({kPrevSize / 2, kPrevSize / 2});
    m_previewHolder->addChild(m_preview);
}

// --------------------------------------------------------------- colocar ----

CCPoint ExportPopup::placementOrigin() const {
    // Con un único objeto seleccionado, la imagen se centra en él;
    // si no, en el centro de la vista actual del editor.
    if (m_editor) {
        auto sel = m_editor->getSelectedObjects();
        if (sel && sel->count() == 1) {
            auto obj = static_cast<GameObject*>(sel->objectAtIndex(0));
            return {obj->getPositionX(), obj->getPositionY()};
        }
    }
    auto lel = LevelEditorLayer::get();
    auto win = CCDirector::get()->getWinSize();
    if (lel && lel->m_objectLayer) {
        return lel->m_objectLayer->convertToNodeSpace(ccp(win.width / 2, win.height / 2));
    }
    return {0, 0};
}

// Canales que el nivel ya usa: los de cualquier objeto (color base o de detalle) y los que ya tienen un
// color definido en los ajustes de color. Así cada imagen nueva coge canales libres y no pisa colores anteriores.
std::set<int> ExportPopup::usedChannels() const {
    std::set<int> used;
    auto lel = LevelEditorLayer::get();
    if (!lel) return used;
    if (s_claimedFor != lel) { s_claimed.clear(); s_claimedFor = lel; }      // otro editor: se olvidan las reservas
    used.insert(s_claimed.begin(), s_claimed.end());
    if (lel->m_objects) {
        for (auto* obj : CCArrayExt<GameObject*>(lel->m_objects)) {
            if (!obj) continue;
            if (obj->m_baseColor) used.insert(obj->m_baseColor->m_colorID);
            if (obj->m_detailColor) used.insert(obj->m_detailColor->m_colorID);
        }
    }
    std::vector<GJEffectManager*> managers;
    if (lel->m_effectManager) managers.push_back(lel->m_effectManager);
    if (lel->m_levelSettings && lel->m_levelSettings->m_effectManager &&
        lel->m_levelSettings->m_effectManager != lel->m_effectManager) {
        managers.push_back(lel->m_levelSettings->m_effectManager);
    }
    for (auto* mgr : managers) {
        auto* actions = mgr->getAllColorActions();
        if (!actions) continue;
        for (auto* a : CCArrayExt<ColorAction*>(actions)) {
            if (!a) continue;
            used.insert(a->m_colorID);                                   // canal con color definido (en cualquier rango, tambien >= 1020)
            if (a->m_copyID > 0) used.insert(a->m_copyID);               // canal que otro canal copia: tambien esta en uso
        }
    }
    return used;
}

// Define cada canal de la paleta directamente en los ajustes de color del nivel (lo que el selector
// "Base Color" del editor lee). Solo usa GJEffectManager::getColorAction y los campos de ColorAction.
void ExportPopup::applyChannelColorsDirect(gdx::GdExportOptions const& eo, gdx::ChannelMap const& cmap) {
    auto lel = LevelEditorLayer::get();
    if (!lel) return;
    std::vector<GJEffectManager*> managers;
    if (lel->m_effectManager) managers.push_back(lel->m_effectManager);
    if (lel->m_levelSettings && lel->m_levelSettings->m_effectManager &&
        lel->m_levelSettings->m_effectManager != lel->m_effectManager) {
        managers.push_back(lel->m_levelSettings->m_effectManager);
    }
    for (size_t i = 0; i < cmap.colors.size(); ++i) {
        int channel = gdx::channelForIndex(eo, int(i));               // 2..999 y 1020..9999, nunca el 1
        ccColor3B c = {cmap.colors[i][0], cmap.colors[i][1], cmap.colors[i][2]};
        for (auto* mgr : managers) {
            auto* action = mgr->getColorAction(channel);
            if (!action) continue;
            action->m_color = c;
            action->m_fromColor = c;
            action->m_toColor = c;
            action->m_currentOpacity = 1.f;
            action->m_fromOpacity = 1.f;
            action->m_toOpacity = 1.f;
            action->m_blending = false;
            action->m_playerColor = -1;                               // sin color de jugador
            action->m_copyID = 0;                                     // no copia otro canal
        }
    }
    for (auto* mgr : managers) mgr->processColors();                  // recalcula los colores activos
}

void ExportPopup::onPlace(CCObject*) {
    if (m_job) {
        FLAlertLayer::create("Image Exporter", "Still calculating. Wait for it to finish.", "OK")->show();
        return;
    }
    if (!m_hasResult) {
        FLAlertLayer::create("Image Exporter", "Choose an image first.", "OK")->show();
        return;
    }
    auto lel = LevelEditorLayer::get();
    if (!lel) return;

    auto* mod = Mod::get();
    gdx::GdExportOptions eo = makeExportOptions();
    CCPoint origin = placementOrigin();
    eo.originX = origin.x;
    eo.originY = origin.y;

    // Canales de color: se eligen entre los que el nivel NO usa (nunca el 1) para no pisar colores anteriores.
    gdx::ChannelMap cmap = gdx::buildChannelMap(m_result);
    if (m_result.colored) {
        std::set<int> used = usedChannels();
        int need = int(cmap.colors.size());
        std::vector<int> chosen = gdx::pickFreeChannels(used, eo.paletteChannelStart, need);
        if (int(chosen.size()) < need) {
            char buf[200];
            std::snprintf(buf, sizeof buf, "Not enough free channels: %d needed, %d left.\nUse fewer colors in the settings or free up channels.",
                          need, int(chosen.size()));
            FLAlertLayer::create("Image Exporter", buf, "OK")->show();
            return;
        }
        eo.channels = chosen;
        for (int c : chosen) s_claimed.insert(c);                    // reservados para que la siguiente imagen use otros distintos
        {
            int nHigh = int(std::count_if(chosen.begin(), chosen.end(), [](int c) { return c >= gdx::kChanHighMin; }));
            log::info("Channels assigned: {} (range {}-{}, {} from the 1020+ range); channels already in use before: {}",
                      chosen.size(), *std::min_element(chosen.begin(), chosen.end()), *std::max_element(chosen.begin(), chosen.end()), nHigh, used.size());
        }
        this->applyChannelColorsDirect(eo, cmap);                    // define los colores ANTES de crear los objetos
    }

    std::string str = gdx::toGdString(m_result, eo);
    if (str.empty()) {
        FLAlertLayer::create("Image Exporter", "There are no objects to place.", "OK")->show();
        return;
    }
    size_t count = m_result.shapes.size() + m_result.textRows.size();
    CCArray* created = lel->createObjectsFromString(str.c_str(), true, true);
    if (created && created->count() > 0 && mod->getSettingValue<bool>("select-after") && m_editor) {
        m_editor->selectObjects(created, true);
    }

    char msg[128];
    if (m_result.colored && !eo.channels.empty()) {
        int lo = *std::min_element(eo.channels.begin(), eo.channels.end()), hi = *std::max_element(eo.channels.begin(), eo.channels.end());
        if (lo <= gdx::kChanLowMax && hi >= gdx::kChanHighMin) std::snprintf(msg, sizeof msg, "Image placed: %zu objects | channels %d-%d and %d-%d", count, lo, gdx::kChanLowMax, gdx::kChanHighMin, hi);
        else std::snprintf(msg, sizeof msg, "Image placed: %zu objects | channels %d-%d", count, lo, hi);
    } else {
        std::snprintf(msg, sizeof msg, "Image placed: %zu objects", count);
    }
    Notification::create(msg, NotificationIcon::Success, 2.5f)->show();
    this->onClose(nullptr);
}

void ExportPopup::onInfo(CCObject*) {
    if (auto* info = InfoPopup::create()) info->show();
    this->dumpUiAfter("info box opened", 1.0f);
}
