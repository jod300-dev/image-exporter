#include "ExportPopup.hpp"

#include <Geode/ui/Notification.hpp>
#include <Geode/utils/file.hpp>
#include <Geode/utils/async.hpp>
#include <thread>

#define STBI_NO_STDIO
#include "stb_image.h"

namespace {
    constexpr int kMaxObjects = 10000;           // tope duro pedido: 10k objetos
    constexpr int kMinObjects = 500;
    constexpr int kBudgetStep = 500;
    constexpr float kPopupW = 400.f, kPopupH = 428.f;
    constexpr float kPrevSize = 150.f;
    const int kSizes[] = {150, 300, 450, 600, 900, 1200, 1800, 2400, 3000, 4500, 6000};   // lado largo, unidades GD
    constexpr int kSizeCount = int(sizeof(kSizes) / sizeof(kSizes[0]));
    constexpr int kMonoCount = int(gdx::Method::COUNT) - gdx::kNormalMethodCount;          // estilos monocromos

    int qualityFromSetting() {
        auto q = Mod::get()->getSettingValue<std::string>("quality");
        if (q == "Rapido") return 0;
        if (q == "Alto") return 2;
        return 1;
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

bool ExportPopup::init(EditorUI* editor) {
    if (!Popup::init(kPopupW, kPopupH)) return false;
    m_editor = editor;
    this->setTitle("Image Exporter");

    // ajustes recordados de la última vez
    auto* mod = Mod::get();
    m_method = std::clamp(mod->getSavedValue<int>("method", 2), 0, gdx::kNormalMethodCount - 1);
    m_mono = std::clamp(mod->getSavedValue<int>("mono", 0), 0, kMonoCount);
    m_colorCheck = mod->getSavedValue<bool>("color-channels", false);
    m_mainMode = std::clamp(mod->getSavedValue<int>("main-mode", 0), 0, 3);
    m_budget = std::clamp(mod->getSavedValue<int>("budget", kMaxObjects), kMinObjects, kMaxObjects);
    m_sizeIdx = std::clamp(mod->getSavedValue<int>("size-idx", 3), 0, kSizeCount - 1);

    auto addArrow = [&](bool flip, SEL_MenuHandler cb, float x, float y) {
        auto spr = CCSprite::createWithSpriteFrameName("GJ_arrow_03_001.png");
        spr->setScale(0.55f);
        spr->setFlipX(flip);
        auto btn = CCMenuItemSpriteExtra::create(spr, this, cb);
        m_buttonMenu->addChildAtPosition(btn, Anchor::TopLeft, ccp(x, -y));
    };

    // --- sección normal, columna izquierda: método, descripción, objetos, tamaño ---
    const float cx = 115.f;
    addArrow(false, menu_selector(ExportPopup::onMethodPrev), 18.f, 50.f);
    addArrow(true,  menu_selector(ExportPopup::onMethodNext), 212.f, 50.f);
    m_methodLabel = CCLabelBMFont::create("", "bigFont.fnt");
    m_mainLayer->addChildAtPosition(m_methodLabel, Anchor::TopLeft, ccp(cx, -50.f));

    m_descLabel = CCLabelBMFont::create("", "chatFont.fnt", 260.f, kCCTextAlignmentCenter);
    m_descLabel->setScale(0.8f);
    m_mainLayer->addChildAtPosition(m_descLabel, Anchor::TopLeft, ccp(cx, -100.f));

    addArrow(false, menu_selector(ExportPopup::onBudgetDec), 18.f, 150.f);
    addArrow(true,  menu_selector(ExportPopup::onBudgetInc), 212.f, 150.f);
    m_budgetLabel = CCLabelBMFont::create("", "bigFont.fnt");
    m_mainLayer->addChildAtPosition(m_budgetLabel, Anchor::TopLeft, ccp(cx, -150.f));

    addArrow(false, menu_selector(ExportPopup::onSizeDec), 18.f, 185.f);
    addArrow(true,  menu_selector(ExportPopup::onSizeInc), 212.f, 185.f);
    m_sizeLabel = CCLabelBMFont::create("", "bigFont.fnt");
    m_mainLayer->addChildAtPosition(m_sizeLabel, Anchor::TopLeft, ccp(cx, -185.f));

    // --- columna derecha: vista previa ---
    m_previewHolder = CCNode::create();
    m_previewHolder->setContentSize({kPrevSize, kPrevSize});
    m_previewHolder->setAnchorPoint({0.5f, 0.5f});
    auto bg = CCLayerColor::create(ccc4(0, 0, 0, 120), kPrevSize, kPrevSize);
    m_previewHolder->addChild(bg);
    auto hint = CCLabelBMFont::create("Vista previa", "chatFont.fnt");
    hint->setOpacity(120);
    hint->setPosition({kPrevSize / 2, kPrevSize / 2});
    m_previewHolder->addChild(hint);
    m_mainLayer->addChildAtPosition(m_previewHolder, Anchor::TopLeft, ccp(312.f, -118.f));

    // --- checks de los métodos principales (1-8): el HSV a color es el predeterminado ---
    auto addCheck = [&](CCMenuItemToggler*& slot, SEL_MenuHandler cb, bool on, float x, char const* text) {
        slot = CCMenuItemToggler::createWithStandardSprites(this, cb, 0.5f);
        slot->toggle(on);
        m_buttonMenu->addChildAtPosition(slot, Anchor::TopLeft, ccp(x, -212.f));
        auto lbl = CCLabelBMFont::create(text, "bigFont.fnt");
        lbl->setScale(0.3f);
        lbl->setAnchorPoint({0.f, 0.5f});
        m_mainLayer->addChildAtPosition(lbl, Anchor::TopLeft, ccp(x + 14.f, -212.f));
    };
    addCheck(m_chkChannels, menu_selector(ExportPopup::onMainChannels), m_mainMode == 1, 30.f, "Canales de color");
    addCheck(m_chkGrayHsv,  menu_selector(ExportPopup::onMainGrayHsv),  m_mainMode == 2, 160.f, "Gris (HSV)");
    addCheck(m_chkGrayCh,   menu_selector(ExportPopup::onMainGrayCh),   m_mainMode == 3, 270.f, "Gris (canales)");

    // --- sección de estilos monocromos, debajo de la normal ---
    auto line = CCLayerColor::create(ccc4(255, 255, 255, 60), 360.f, 1.f);
    m_mainLayer->addChildAtPosition(line, Anchor::TopLeft, ccp(20.f, -234.f));
    auto head = CCLabelBMFont::create("Estilos ligeros (sin HSV)", "bigFont.fnt");
    head->setScale(0.4f);
    head->setColor({255, 215, 90});
    m_mainLayer->addChildAtPosition(head, Anchor::TopLeft, ccp(kPopupW / 2, -250.f));

    addArrow(false, menu_selector(ExportPopup::onMonoPrev), 40.f, 276.f);
    addArrow(true,  menu_selector(ExportPopup::onMonoNext), 360.f, 276.f);
    m_monoLabel = CCLabelBMFont::create("", "bigFont.fnt");
    m_mainLayer->addChildAtPosition(m_monoLabel, Anchor::TopLeft, ccp(kPopupW / 2, -276.f));
    m_monoDescLabel = CCLabelBMFont::create("", "chatFont.fnt", 340.f, kCCTextAlignmentCenter);
    m_monoDescLabel->setScale(0.8f);
    m_mainLayer->addChildAtPosition(m_monoDescLabel, Anchor::TopLeft, ccp(kPopupW / 2, -302.f));

    // --- check: colores de la imagen en canales de color (solo estilos ligeros) ---
    m_colorToggle = CCMenuItemToggler::createWithStandardSprites(this, menu_selector(ExportPopup::onColorToggle), 0.55f);
    m_colorToggle->toggle(m_colorCheck);
    m_buttonMenu->addChildAtPosition(m_colorToggle, Anchor::TopLeft, ccp(70.f, -326.f));
    m_colorLabel = CCLabelBMFont::create("Colores en canales (sin HSV)", "bigFont.fnt");
    m_colorLabel->setScale(0.42f);
    m_colorLabel->setAnchorPoint({0.f, 0.5f});
    m_mainLayer->addChildAtPosition(m_colorLabel, Anchor::TopLeft, ccp(90.f, -326.f));

    // --- estado ---
    m_statusLabel = CCLabelBMFont::create("Elige una imagen para empezar", "chatFont.fnt");
    m_mainLayer->addChildAtPosition(m_statusLabel, Anchor::TopLeft, ccp(kPopupW / 2, -350.f));
    m_noteLabel = CCLabelBMFont::create("", "chatFont.fnt", 380.f, kCCTextAlignmentCenter);
    m_noteLabel->setScale(0.7f);
    m_noteLabel->setColor({255, 200, 80});
    m_mainLayer->addChildAtPosition(m_noteLabel, Anchor::TopLeft, ccp(kPopupW / 2, -368.f));

    // --- botones ---
    auto pickSpr = ButtonSprite::create("Elegir imagen");
    pickSpr->setScale(0.8f);
    auto pickBtn = CCMenuItemSpriteExtra::create(pickSpr, this, menu_selector(ExportPopup::onPick));
    m_buttonMenu->addChildAtPosition(pickBtn, Anchor::TopLeft, ccp(115.f, -402.f));

    auto placeSpr = ButtonSprite::create("Colocar");
    placeSpr->setScale(0.8f);
    auto placeBtn = CCMenuItemSpriteExtra::create(placeSpr, this, menu_selector(ExportPopup::onPlace));
    m_buttonMenu->addChildAtPosition(placeBtn, Anchor::TopLeft, ccp(285.f, -402.f));

    auto infoBtn = CCMenuItemSpriteExtra::create(
        CCSprite::createWithSpriteFrameName("GJ_infoIcon_001.png"), this, menu_selector(ExportPopup::onInfo));
    m_buttonMenu->addChildAtPosition(infoBtn, Anchor::TopRight, ccp(-14.f, -14.f));

    this->schedule(schedule_selector(ExportPopup::tick), 0.1f);
    this->refreshLabels();
    return true;
}

// ------------------------------------------------------------- etiquetas ---

gdx::Method ExportPopup::effectiveMethod() const {
    return m_mono > 0 ? gdx::Method(gdx::kNormalMethodCount + m_mono - 1) : gdx::Method(m_method);
}

void ExportPopup::refreshLabels() {
    auto const& info = gdx::methodInfo(gdx::Method(m_method));
    m_methodLabel->setString(info.name);
    m_methodLabel->limitLabelWidth(170.f, 0.5f, 0.2f);
    m_descLabel->setString(info.desc);

    // si hay un estilo monocromo activo, sustituye al método normal (que se atenúa)
    bool monoOn = m_mono > 0;
    m_methodLabel->setOpacity(monoOn ? 100 : 255);
    m_descLabel->setOpacity(monoOn ? 100 : 255);
    if (monoOn) {
        auto const& mi = gdx::methodInfo(gdx::Method(gdx::kNormalMethodCount + m_mono - 1));
        m_monoLabel->setString(mi.name);
        m_monoDescLabel->setString(mi.desc);
    } else {
        m_monoLabel->setString("(ninguno: usa el de arriba)");
        m_monoDescLabel->setString("Elige un estilo para usar solo tinta negra/blanca por canal, sin HSV y con pocos objetos.");
    }
    m_monoLabel->setOpacity(monoOn ? 255 : 150);
    m_colorLabel->setOpacity(monoOn ? 255 : 110);       // el check solo afecta a los estilos ligeros
    m_monoLabel->limitLabelWidth(280.f, 0.5f, 0.2f);

    char buf[96];
    std::snprintf(buf, sizeof buf, "Objetos max: %d", m_budget);
    m_budgetLabel->setString(buf);
    m_budgetLabel->limitLabelWidth(170.f, 0.5f, 0.2f);

    std::snprintf(buf, sizeof buf, "Lado largo: %d u", kSizes[m_sizeIdx]);
    m_sizeLabel->setString(buf);
    m_sizeLabel->limitLabelWidth(170.f, 0.5f, 0.2f);

    auto* mod = Mod::get();
    mod->setSavedValue<int>("method", m_method);
    mod->setSavedValue<int>("mono", m_mono);
    mod->setSavedValue<bool>("color-channels", m_colorCheck);
    mod->setSavedValue<int>("main-mode", m_mainMode);
    mod->setSavedValue<int>("budget", m_budget);
    mod->setSavedValue<int>("size-idx", m_sizeIdx);
}

void ExportPopup::setStatus(std::string const& s, std::string const& note) {
    m_statusLabel->setString(s.c_str());
    m_statusLabel->limitLabelWidth(380.f, 0.8f, 0.3f);
    m_noteLabel->setString(note.c_str());
}

void ExportPopup::stepMethod(int dir) {
    int n = gdx::kNormalMethodCount;
    m_method = (m_method + dir + n) % n;
    m_mono = 0;                                   // elegir un método normal desactiva el estilo monocromo
    this->refreshLabels();
    this->scheduleRecompute();
}

void ExportPopup::stepMono(int dir) {
    int n = kMonoCount + 1;                       // 0 = ninguno
    m_mono = (m_mono + dir + n) % n;
    this->refreshLabels();
    this->scheduleRecompute();
}

// Los tres checks de los métodos principales funcionan como una selección única: ninguno = HSV a color.
void ExportPopup::setMainMode(int mode) {
    m_mainMode = mode;
    m_chkChannels->toggle(mode == 1);
    m_chkGrayHsv->toggle(mode == 2);
    m_chkGrayCh->toggle(mode == 3);
    this->refreshLabels();
    this->scheduleRecompute();
}

void ExportPopup::onColorToggle(CCObject*) {
    m_colorCheck = !m_colorCheck;                 // en el callback el toggler aún no ha cambiado su estado
    this->refreshLabels();
    if (m_mono > 0) this->scheduleRecompute();    // solo recalcula si hay un estilo ligero activo
}

void ExportPopup::stepBudget(int dir) {
    m_budget = std::clamp(m_budget + dir * kBudgetStep, kMinObjects, kMaxObjects);
    this->refreshLabels();
    this->scheduleRecompute();
}

void ExportPopup::stepSize(int dir) {
    m_sizeIdx = std::clamp(m_sizeIdx + dir, 0, kSizeCount - 1);
    this->refreshLabels();   // el tamaño solo afecta a la colocación, no hace falta recalcular
}

// ------------------------------------------------------------ cargar imagen --

void ExportPopup::onPick(CCObject*) {
    auto options = file::FilePickOptions{
        std::nullopt,
        {{"Imagenes", {"*.png", "*.jpg", "*.jpeg", "*.bmp", "*.tga", "*.gif"}}}
    };
    async::spawn(
        file::pick(file::PickMode::OpenFile, options),
        [weak = WeakRef<ExportPopup>(this)](auto result) {
            auto self = weak.lock();
            if (!self) return;
            if (!result.isOk()) {
                self->setStatus("No se pudo abrir el selector de archivos");
                return;
            }
            auto opt = result.unwrap();
            if (!opt) return;                                    // cancelado
            auto bytes = utils::file::readBinary(opt.value());
            if (!bytes.isOk()) {
                self->setStatus("No se pudo leer el archivo");
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
        this->setStatus("No se pudo leer la imagen", "Formatos: PNG, JPG, BMP, TGA, GIF");
        return;
    }
    if (int64_t(w) * h > 100000000) {
        stbi_image_free(d);
        this->setStatus("Imagen demasiado grande (max. 100 megapixeles)");
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
    o.mainChannels = m_mainMode == 1 || m_mainMode == 3;
    o.grayscale = m_mainMode >= 2;
    o.mainPaletteSize = std::clamp(mod->getSettingValue<int>("palette-size-main"), 2, 250);
    // canales libres del nivel: la paleta nunca puede necesitar más de los que quedan (el 1 no cuenta)
    o.maxChannels = gdx::countFreeChannels(usedChannels());
    return o;
}

gdx::GdExportOptions ExportPopup::makeExportOptions() const {
    auto* mod = Mod::get();
    gdx::GdExportOptions eo;
    eo.longSide = kSizes[m_sizeIdx];
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
    char buf[48];
    std::snprintf(buf, sizeof buf, "Calculando... %d%%", int(m_job->prog.fraction.load() * 100.f));
    this->setStatus(buf);
}

void ExportPopup::applyResult(std::shared_ptr<Job> job) {
    m_result = std::move(job->result);
    m_hasResult = !m_result.shapes.empty() || !m_result.textRows.empty();
    if (!m_hasResult) {
        this->setStatus("No se genero ningun objeto", m_result.note);
        return;
    }
    // tamaño aproximado de la cadena de objetos (sin comprimir): sirve para comparar métodos
    size_t kb = gdx::toGdString(m_result, makeExportOptions()).size() / 1024;
    size_t objs = m_result.shapes.size() + m_result.textRows.size();
    char buf[200];
    if (m_result.psnr >= 0)
        std::snprintf(buf, sizeof buf, "%zu objetos de %d  |  %.1f dB  |  ~%zu KB  |  %.1f s",
                      objs, m_budget, m_result.psnr, kb, m_result.ms / 1000.0);
    else
        std::snprintf(buf, sizeof buf, "%zu objetos de %d  |  ~%zu KB  |  %.1f s",
                      objs, m_budget, kb, m_result.ms / 1000.0);
    this->setStatus(buf, m_result.note);
    this->buildPreview();
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
        FLAlertLayer::create("Image Exporter", "Todavia se esta calculando. Espera a que termine.", "OK")->show();
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
            std::snprintf(buf, sizeof buf, "No hay canales libres suficientes: hacen falta %d y quedan %d.\nUsa menos colores en los ajustes o libera canales.",
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
    FLAlertLayer::create(
        "Image Exporter",
        "Importa una imagen al editor como <cg>rectangulos de color</c> (max. <cy>10.000</c> objetos).\n"
        "<cy>Consejos:</c>\n"
        "- <cg>3 BSP</c>: mejor para fotos.\n"
        "- <cg>5 Regiones</c>: ilustraciones y pixel art (muy pocos objetos).\n"
        "- <cg>8 Hibrido</c>: maxima calidad, tarda un poco mas.\n"
        "<cy>Estilos monocromos</c> (seccion de abajo): tinta negra/blanca por canal, <cg>sin HSV</c> y pocos objetos. "
        "Con <cg>Colores en canales</c> los colores de la imagen se reparten en canales de color libres (<cy>nunca el 1</c>, y sin pisar los que ya usas), sin HSV. Los metodos 1-8 tienen sus propios checks: <cg>Canales de color</c>, <cg>Gris (HSV)</c> y <cg>Gris (canales)</c>.\n"
        "El <cg>ASCII</c> usa objetos de texto: elige una fuente monoespaciada en el nivel y calibra el ancho de caracter en los ajustes.\n"
        "Con <cy>un objeto seleccionado</c> la imagen se centra en el; si no, en el centro de la vista.\n"
        "La importacion no se puede deshacer: <cr>guarda antes</c>. Quedan seleccionados para moverlos.",
        "OK"
    )->show();
}
