#include "ExportPopup.hpp"
#include "UiDump.hpp"

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

    int qualityFromSetting() {
        auto q = Mod::get()->getSettingValue<std::string>("quality");
        if (q == "Rápido" || q == "Rapido") return 0;
        if (q == "Normal") return 1;
        if (q == "Máximo" || q == "Maximo") return 3;
        return 2;                                  // Alto (por defecto)
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
    auto hint = CCLabelBMFont::create("Vista previa", "chatFont.fnt");
    hint->setOpacity(120);
    hint->setPosition({kPrevSize / 2, kPrevSize / 2});
    m_previewHolder->addChild(hint);
    put(m_previewHolder, 84.f, 104.f);

    m_statA = label("Elige una imagen", "chatFont.fnt", 0.85f, 84.f, 186.f);
    m_statB = label("", "chatFont.fnt", 0.75f, 84.f, 199.f);
    m_noteLabel = CCLabelBMFont::create("", "chatFont.fnt", 600.f, kCCTextAlignmentCenter);   // 600: nunca ajusta solo; se parte a mano
    m_noteLabel->setScale(0.6f);
    m_noteLabel->setColor({255, 200, 80});
    m_noteLabel->setAnchorPoint({0.5f, 1.f});                  // anclada arriba: si hay mas lineas crece hacia abajo sin pisar el estado
    put(m_noteLabel, 84.f, 210.f);

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
    m_tabMain = makeTab("Principales", menu_selector(ExportPopup::onTabMain));
    m_tabLight = makeTab("Ligeros", menu_selector(ExportPopup::onTabLight));
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
    m_descLabel = CCLabelBMFont::create("", "chatFont.fnt", 600.f, kCCTextAlignmentCenter);   // se parte a mano (ver wrapText)
    m_descLabel->setScale(0.7f);
    m_descLabel->setAnchorPoint({0.5f, 1.f});                  // anclada arriba: 2 o 3 lineas crecen hacia abajo
    put(m_descLabel, 292.f, 92.f);

    // sliders: objetos y lado largo
    label("Objetos", "bigFont.fnt", 0.34f, 168.f, 136.f, 0.f);
    m_budgetValue = label("", "bigFont.fnt", 0.34f, 418.f, 136.f, 1.f);
    m_budgetSlider = Slider::create(this, menu_selector(ExportPopup::onBudgetSlider), 0.78f);
    m_budgetSlider->setValue(budgetToSlider(m_budget));
    put(m_budgetSlider, 292.f, 157.f);

    label("Lado largo", "bigFont.fnt", 0.34f, 168.f, 182.f, 0.f);
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
    addCheck(m_chkChannels, m_mainOpts, menu_selector(ExportPopup::onMainChannels), m_mainMode == 1, 174.f, "Canales");
    addCheck(m_chkGrayHsv,  m_mainOpts, menu_selector(ExportPopup::onMainGrayHsv),  m_mainMode == 2, 256.f, "Gris HSV");
    addCheck(m_chkGrayCh,   m_mainOpts, menu_selector(ExportPopup::onMainGrayCh),   m_mainMode == 3, 338.f, "Gris canales");
    addCheck(m_chkColor,    m_lightOpts, menu_selector(ExportPopup::onColorToggle), m_colorCheck, 174.f, "Colores en canales (sin HSV)");

    // botones de acción
    // Botones mas bajos (escala del sprite 0.8 con el texto a 0.52: se lee igual) centrados bajo la columna derecha.
    auto pickSpr = ButtonSprite::create("Imagen", "bigFont.fnt", "GJ_button_01.png", 0.52f);
    pickSpr->setScale(0.8f);
    auto pickBtn = CCMenuItemSpriteExtra::create(pickSpr, this, menu_selector(ExportPopup::onPick));
    putBtn(pickBtn, 252.f, 260.f);
    auto placeSpr = ButtonSprite::create("Colocar", "bigFont.fnt", "GJ_button_02.png", 0.52f);
    placeSpr->setScale(0.8f);
    auto placeBtn = CCMenuItemSpriteExtra::create(placeSpr, this, menu_selector(ExportPopup::onPlace));
    putBtn(placeBtn, 327.f, 260.f);
    // Fila del titulo: Ajustes e info a la derecha, dentro del marco y sin tocar las pestanas.
    auto infoBtn = CCMenuItemSpriteExtra::create(CCSprite::createWithSpriteFrameName("GJ_infoIcon_001.png"), this, menu_selector(ExportPopup::onInfo));
    putBtn(infoBtn, 408.f, 21.f);
    // Ajustes: misma fila, mismo alto y mismo estilo de texto que Imagen / Colocar, centrado bajo la vista previa.
    auto setSpr = ButtonSprite::create("Ajustes", "bigFont.fnt", "GJ_button_04.png", 0.52f);
    setSpr->setScale(0.8f);
    auto setBtn = CCMenuItemSpriteExtra::create(setSpr, this, menu_selector(ExportPopup::onSettings));
    putBtn(setBtn, 84.f, 260.f);

    this->schedule(schedule_selector(ExportPopup::tick), 0.1f);
    this->setTab(m_tab);                       // aplica pestaña, etiquetas y visibilidad
    // Diagnostico de la interfaz (ajuste "dump-ui"): 1 s despues, cuando ya termino la animacion de entrada y las cajas son las finales
    if (mod->getSettingValue<bool>("dump-ui")) this->scheduleOnce(schedule_selector(ExportPopup::dumpUi), 1.0f);
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
    m_descLabel->setString(wrapText(info.desc, 0.7f, 244.f).c_str());

    char buf[64];
    std::snprintf(buf, sizeof buf, "%d", m_budget);
    m_budgetValue->setString(buf);
    std::snprintf(buf, sizeof buf, "%d u (%.1f bloques)", m_sizeU, m_sizeU / 30.0);
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

void ExportPopup::setStatus(std::string const& a, std::string const& b, std::string const& note) {
    m_statA->setString(a.c_str());
    m_statA->limitLabelWidth(140.f, 0.85f, 0.4f);
    m_statB->setString(b.c_str());
    m_statB->limitLabelWidth(140.f, 0.75f, 0.4f);
    m_noteLabel->setString(wrapText(note, 0.6f, 140.f).c_str());
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
    h << "=== Image Exporter :: diagnóstico de interfaz ===\n";
    h << "fecha: " << tbuf << "\n";
    h << "mod: " << mod->getVersion().toVString() << "   geode: " << Loader::get()->getVersion().toVString()
      << "   gd: " << Loader::get()->getGameVersion() << "   plataforma: " << GEODE_PLATFORM_NAME << "\n";

    auto* dir = CCDirector::get();
    CCSize win = dir->getWinSize(), winPx = dir->getWinSizeInPixels();
    h << "winSize: " << win.width << "x" << win.height << "   winSizePixels: " << winPx.width << "x" << winPx.height
      << "   contentScaleFactor: " << dir->getContentScaleFactor() << "\n";
    if (auto* view = dir->getOpenGLView()) {
        CCSize fs = view->getFrameSize();
        h << "ventana del juego (frame): " << fs.width << "x" << fs.height << "\n";
    }

    auto info = [&](char const* name, CCNode* n) {
        if (!n) { h << name << ": (null)\n"; return; }
        CCSize s = n->getContentSize(); CCPoint p = n->getPosition(), a = n->getAnchorPoint();
        h << name << ": tam=" << s.width << "x" << s.height << " pos=(" << p.x << "," << p.y << ") ancla=(" << a.x << "," << a.y
          << ") escala=" << n->getScaleX() << "," << n->getScaleY() << "\n";
    };
    h << "popup nominal: " << kPopupW << "x" << kPopupH << "\n";
    info("m_mainLayer", m_mainLayer);
    info("m_buttonMenu", m_buttonMenu);

    h << "\n=== ESTADO DEL MOD ===\n";
    h << "pestaña=" << m_tab << " método=" << m_method << " estilo=" << m_style << " modoPrincipal=" << m_mainMode
      << " coloresCanales=" << int(m_colorCheck) << " objetos=" << m_budget << " ladoLargo=" << m_sizeU << "u\n";
    h << "método efectivo: " << gdx::methodInfo(effectiveMethod()).name << "\n";
    h << "ajustes: calidad=" << mod->getSettingValue<std::string>("quality") << " max-raster=" << mod->getSettingValue<int>("max-raster")
      << " umbral-alfa=" << mod->getSettingValue<int>("alpha-threshold") << " bleed=" << mod->getSettingValue<double>("bleed")
      << " invertir-giro=" << int(mod->getSettingValue<bool>("flip-rotation")) << "\n";
    auto text = [&](char const* name, CCLabelBMFont* l) { h << name << ": \"" << (l && l->getString() ? l->getString() : "") << "\"\n"; };
    text("etiqueta método", m_methodLabel);
    text("etiqueta descripcion", m_descLabel);
    text("estado A", m_statA);
    text("estado B", m_statB);
    text("nota", m_noteLabel);

    h << "\n=== IMAGEN Y RESULTADO ===\n";
    if (m_image) h << "imagen original: " << m_image->w << "x" << m_image->h << "\n"; else h << "imagen original: (ninguna)\n";
    if (m_hasResult) {
        int outside = 0, rotated = 0; double worst = 0;
        for (auto const& s : m_result.shapes) {
            double rad = s.rot * 3.14159265358979323846 / 180.0, cs = std::cos(rad), sn = std::sin(rad), hx = s.w * 0.5, hy = s.h * 0.5;
            double ex = std::abs(hx * cs) + std::abs(hy * sn), ey = std::abs(hx * sn) + std::abs(hy * cs);
            double ov = std::max({0.0, ex - s.cx, s.cx + ex - m_result.rasterW, ey - s.cy, s.cy + ey - m_result.rasterH});
            if (std::fmod(std::abs(s.rot), 90.0) > 1e-3) ++rotated;
            if (ov > 0.05) { ++outside; worst = std::max(worst, ov); }
        }
        h << "formas=" << m_result.shapes.size() << " filasTexto=" << m_result.textRows.size() << " raster=" << m_result.rasterW << "x"
          << m_result.rasterH << " aspecto=" << m_result.aspect << " psnr=" << m_result.psnr << " ms=" << m_result.ms << "\n";
        h << "tieneTransparencia=" << int(m_result.hadAlpha) << " mono=" << int(m_result.mono) << " conCanales=" << int(m_result.colored)
          << " girados=" << rotated << "\n";
        h << "formas que SOBRESALEN de la imagen: " << outside << " (peor: " << worst << " px del raster)\n";
        h << "nota del motor: \"" << m_result.note << "\"\n";
    } else {
        h << "(todavía no hay resultado)\n";
    }
    return h.str();
}

// Escribe <carpeta de guardado del mod>/ui-dump.txt. Solo actua con el ajuste "Guardar diagnóstico de la interfaz" activo.
void ExportPopup::dumpUi(float) {
    auto* mod = Mod::get();
    if (!mod->getSettingValue<bool>("dump-ui")) return;
    std::string text;
    try {
        text = uidump::dumpTree(this, m_mainLayer, dumpHeader());
    } catch (...) {
        text = "Error interno al generar el diagnóstico.\n";
    }
    auto path = mod->getSaveDir() / "ui-dump.txt";
    auto res = utils::file::writeString(path, text);
    std::string shown = utils::string::pathToString(path);
    if (res.isOk()) log::info("Diagnóstico de interfaz guardado en {}", shown);
    else log::error("No se pudo guardar el diagnóstico de interfaz en {}", shown);
    if (!m_dumpNotified) {
        m_dumpNotified = true;
        if (res.isOk()) Notification::create("Diagnóstico guardado en:\n" + shown, NotificationIcon::Success, 6.f)->show();
        else Notification::create("No se pudo guardar el diagnóstico:\n" + shown, NotificationIcon::Error, 6.f)->show();
    }
}

// ------------------------------------------------------------ cargar imagen --

void ExportPopup::onPick(CCObject*) {
    auto options = file::FilePickOptions{
        std::nullopt,
        {{"Imágenes", {"*.png", "*.jpg", "*.jpeg", "*.bmp", "*.tga", "*.gif"}}}
    };
    async::spawn(
        file::pick(file::PickMode::OpenFile, options),
        [weak = WeakRef<ExportPopup>(this)](auto result) {
            auto self = weak.lock();
            if (!self) return;
            if (!result.isOk()) {
                self->setStatus("No se pudo abrir", "el selector de archivos");
                return;
            }
            auto opt = result.unwrap();
            if (!opt) return;                                    // cancelado
            auto bytes = utils::file::readBinary(opt.value());
            if (!bytes.isOk()) {
                self->setStatus("No se pudo leer", "el archivo");
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
        this->setStatus("No se pudo leer la imagen", "", "Formatos: PNG, JPG, BMP, TGA, GIF");
        return;
    }
    if (int64_t(w) * h > 100000000) {
        stbi_image_free(d);
        this->setStatus("Imagen demasiado grande", "(máx. 100 megapíxeles)");
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
    this->setStatus("Calculando... 0%");

    std::thread([job]() {
        try {
            job->result = gdx::convert(*job->src, job->method, job->opt, &job->prog);
        } catch (...) {
            job->result = gdx::Result{};
            job->result.note = "Error interno al calcular";
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
        this->setStatus("Tarda demasiado", "prueba con menos objetos", "Cálculo cancelado tras 90 s. Prueba otro método o baja el número de objetos.");
        return;
    }
    char buf[48];
    std::snprintf(buf, sizeof buf, "Calculando... %d%%", int(m_job->prog.fraction.load() * 100.f));
    this->setStatus(buf);
}

void ExportPopup::applyResult(std::shared_ptr<Job> job) {
    m_result = std::move(job->result);
    m_hasResult = !m_result.shapes.empty() || !m_result.textRows.empty();
    if (!m_hasResult) {
        this->setStatus("No se generó ningún objeto", "", m_result.note);
        return;
    }
    // tamaño aproximado de la cadena de objetos (sin comprimir): sirve para comparar métodos
    size_t kb = gdx::toGdString(m_result, makeExportOptions()).size() / 1024;
    size_t objs = m_result.shapes.size() + m_result.textRows.size();
    char a1[96], b1[96];
    std::snprintf(a1, sizeof a1, "%zu objetos | ~%zu KB", objs, kb);
    if (m_result.psnr >= 0) std::snprintf(b1, sizeof b1, "%.1f dB | %.1f s", m_result.psnr, m_result.ms / 1000.0);
    else std::snprintf(b1, sizeof b1, "%.1f s", m_result.ms / 1000.0);
    this->setStatus(a1, b1, m_result.note);
    this->buildPreview();
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
        for (auto* a : CCArrayExt<ColorAction*>(actions)) if (a) used.insert(a->m_colorID);
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
        int channel = gdx::channelForIndex(eo, int(i));               // 2..999, nunca el 1
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
        FLAlertLayer::create("Image Exporter", "Todavía se está calculando. Espera a que termine.", "OK")->show();
        return;
    }
    if (!m_hasResult) {
        FLAlertLayer::create("Image Exporter", "Primero elige una imagen.", "OK")->show();
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
            std::snprintf(buf, sizeof buf, "No hay suficientes canales libres: hacen falta %d y quedan %d.\nUsa menos colores en los ajustes o libera canales.",
                          need, int(chosen.size()));
            FLAlertLayer::create("Image Exporter", buf, "OK")->show();
            return;
        }
        eo.channels = chosen;
        this->applyChannelColorsDirect(eo, cmap);                    // define los colores ANTES de crear los objetos
    }

    std::string str = gdx::toGdString(m_result, eo);
    if (str.empty()) {
        FLAlertLayer::create("Image Exporter", "No hay objetos que colocar.", "OK")->show();
        return;
    }
    size_t count = m_result.shapes.size() + m_result.textRows.size();
    CCArray* created = lel->createObjectsFromString(str.c_str(), true, true);
    if (created && created->count() > 0 && mod->getSettingValue<bool>("select-after") && m_editor) {
        m_editor->selectObjects(created, true);
    }

    char msg[96];
    if (m_result.colored && !eo.channels.empty()) {
        int lo = *std::min_element(eo.channels.begin(), eo.channels.end()), hi = *std::max_element(eo.channels.begin(), eo.channels.end());
        std::snprintf(msg, sizeof msg, "Imagen colocada: %zu objetos | canales %d-%d", count, lo, hi);
    } else {
        std::snprintf(msg, sizeof msg, "Imagen colocada: %zu objetos", count);
    }
    Notification::create(msg, NotificationIcon::Success, 2.5f)->show();
    this->onClose(nullptr);
}

void ExportPopup::onInfo(CCObject*) {
    // Ventana con scroll y alto fijo (240 < 320 de la pantalla de GD): el texto largo se desplaza dentro y el boton OK siempre se ve.
    FLAlertLayer::create(
        nullptr,
        "Image Exporter",
        "<cy>Principales</c> (1-8): rectángulos de color, máx. <cy>10.000</c> objetos.\n"
        "- <cg>BSP</c> para fotos, <cg>Regiones</c> para ilustraciones y pixel art, <cg>Híbrido</c> para la máxima calidad.\n"
        "- Sin marcar = HSV a color. <cg>Canales</c> reparte los colores en canales libres (nunca el 1); <cg>Gris</c> usa escala de grises.\n\n"
        "<cy>Ligeros</c> (9-18): menos objetos, sin HSV. Con <cg>Colores en canales</c> salen a color.\n"
        "- El <cg>ASCII</c> usa texto: pon una fuente monoespaciada en el nivel y calibra el ancho de carácter en <cy>Ajustes</c>.\n\n"
        "Con <cy>un objeto seleccionado</c> la imagen se centra en él; si no, en la vista.\n"
        "No se puede deshacer: <cr>guarda antes</c>.",
        "OK",
        nullptr,
        360.f,
        true,
        240.f,
        0.8f
    )->show();
}
