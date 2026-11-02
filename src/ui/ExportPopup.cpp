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
    constexpr float kPopupW = 400.f, kPopupH = 380.f;
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

    // --- sección de estilos monocromos, debajo de la normal ---
    auto line = CCLayerColor::create(ccc4(255, 255, 255, 60), 360.f, 1.f);
    m_mainLayer->addChildAtPosition(line, Anchor::TopLeft, ccp(20.f, -214.f));
    auto head = CCLabelBMFont::create("Estilos monocromos (ligeros, sin HSV)", "bigFont.fnt");
    head->setScale(0.4f);
    head->setColor({255, 215, 90});
    m_mainLayer->addChildAtPosition(head, Anchor::TopLeft, ccp(kPopupW / 2, -230.f));

    addArrow(false, menu_selector(ExportPopup::onMonoPrev), 40.f, 258.f);
    addArrow(true,  menu_selector(ExportPopup::onMonoNext), 360.f, 258.f);
    m_monoLabel = CCLabelBMFont::create("", "bigFont.fnt");
    m_mainLayer->addChildAtPosition(m_monoLabel, Anchor::TopLeft, ccp(kPopupW / 2, -258.f));
    m_monoDescLabel = CCLabelBMFont::create("", "chatFont.fnt", 340.f, kCCTextAlignmentCenter);
    m_monoDescLabel->setScale(0.8f);
    m_mainLayer->addChildAtPosition(m_monoDescLabel, Anchor::TopLeft, ccp(kPopupW / 2, -284.f));

    // --- estado ---
    m_statusLabel = CCLabelBMFont::create("Elige una imagen para empezar", "chatFont.fnt");
    m_mainLayer->addChildAtPosition(m_statusLabel, Anchor::TopLeft, ccp(kPopupW / 2, -310.f));
    m_noteLabel = CCLabelBMFont::create("", "chatFont.fnt", 380.f, kCCTextAlignmentCenter);
    m_noteLabel->setScale(0.7f);
    m_noteLabel->setColor({255, 200, 80});
    m_mainLayer->addChildAtPosition(m_noteLabel, Anchor::TopLeft, ccp(kPopupW / 2, -328.f));

    // --- botones ---
    auto pickSpr = ButtonSprite::create("Elegir imagen");
    pickSpr->setScale(0.8f);
    auto pickBtn = CCMenuItemSpriteExtra::create(pickSpr, this, menu_selector(ExportPopup::onPick));
    m_buttonMenu->addChildAtPosition(pickBtn, Anchor::TopLeft, ccp(115.f, -356.f));

    auto placeSpr = ButtonSprite::create("Colocar");
    placeSpr->setScale(0.8f);
    auto placeBtn = CCMenuItemSpriteExtra::create(placeSpr, this, menu_selector(ExportPopup::onPlace));
    m_buttonMenu->addChildAtPosition(placeBtn, Anchor::TopLeft, ccp(285.f, -356.f));

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
    return o;
}

gdx::GdExportOptions ExportPopup::makeExportOptions() const {
    auto* mod = Mod::get();
    gdx::GdExportOptions eo;
    eo.longSide = kSizes[m_sizeIdx];
    eo.colorChannel = std::clamp(mod->getSettingValue<int>("color-channel"), 1, 1020);
    eo.bleed = std::clamp(mod->getSettingValue<double>("bleed"), 0.0, 2.0);
    eo.flipRotation = mod->getSettingValue<bool>("flip-rotation");
    eo.monoFastPath = m_result.mono;                                // negro/blanco por canal, sin claves de HSV
    eo.blackChannel = std::clamp(mod->getSettingValue<int>("black-channel"), 1, 1020);
    eo.whiteChannel = std::clamp(mod->getSettingValue<int>("white-channel"), 1, 1020);
    eo.asciiAdvance = std::clamp(mod->getSettingValue<double>("ascii-advance"), 1.0, 400.0);
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
    char msg[64];
    std::snprintf(msg, sizeof msg, "Imagen colocada: %zu objetos", count);
    Notification::create(msg, NotificationIcon::Success, 2.f)->show();
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
        "El <cg>ASCII</c> usa objetos de texto: elige una fuente monoespaciada en el nivel y calibra el ancho de caracter en los ajustes.\n"
        "Con <cy>un objeto seleccionado</c> la imagen se centra en el; si no, en el centro de la vista.\n"
        "La importacion no se puede deshacer: <cr>guarda antes</c>. Quedan seleccionados para moverlos.",
        "OK"
    )->show();
}
