#pragma once
#include <Geode/Geode.hpp>
#include <atomic>
#include <memory>
#include <set>
#include <string>

#include "engine.hpp"
#include "gdexport.hpp"

using namespace geode::prelude;

// Ventana del exportador: elegir imagen, método, presupuesto de objetos y tamaño,
// ver una vista previa del resultado y colocarlo en el editor.
// Debajo de los 8 métodos normales hay una sección aparte de estilos monocromos.
class ExportPopup : public geode::Popup {
public:
    static ExportPopup* create(EditorUI* editor);
    ~ExportPopup();

protected:
    // Trabajo de conversión que corre en un hilo aparte. El hilo solo toca el Job
    // (que vive en un shared_ptr); la ventana lo consulta desde el hilo principal.
    struct Job {
        gdx::Progress prog;
        std::shared_ptr<const gdx::Image> src;
        gdx::Method method = gdx::Method::BSP;
        gdx::Options opt;
        int id = 0;
        gdx::Result result;
        std::atomic<bool> done{false};
    };

    bool init(EditorUI* editor);

    // acciones de los botones
    void onPick(CCObject*);
    void onPlace(CCObject*);
    void onInfo(CCObject*);
    void onMethodPrev(CCObject*) { stepMethod(-1); }
    void onMethodNext(CCObject*) { stepMethod(+1); }
    void onBudgetDec(CCObject*) { stepBudget(-1); }
    void onBudgetInc(CCObject*) { stepBudget(+1); }
    void onSizeDec(CCObject*) { stepSize(-1); }
    void onSizeInc(CCObject*) { stepSize(+1); }
    void onMonoPrev(CCObject*) { stepMono(-1); }
    void onMonoNext(CCObject*) { stepMono(+1); }
    void onColorToggle(CCObject*);          // check de los estilos ligeros: colores en canales
    void onMainChannels(CCObject*) { setMainMode(m_mainMode == 1 ? 0 : 1); }
    void onMainGrayHsv(CCObject*) { setMainMode(m_mainMode == 2 ? 0 : 2); }
    void onMainGrayCh(CCObject*) { setMainMode(m_mainMode == 3 ? 0 : 3); }

    void stepMethod(int dir);
    void stepMono(int dir);
    void stepBudget(int dir);
    void stepSize(int dir);
    void setMainMode(int mode);                // 0 HSV color (por defecto), 1 canales de color, 2 gris HSV, 3 gris canales
    gdx::Method effectiveMethod() const;       // el estilo monocromo, si hay uno activo; si no, el normal
    void refreshLabels();

    void loadImageBytes(std::vector<uint8_t> const& bytes);
    void scheduleRecompute();
    void recompute(float = 0.f);
    void tick(float);
    void applyResult(std::shared_ptr<Job> job);
    void buildPreview();
    void setStatus(std::string const& s, std::string const& note = "");
    gdx::Options makeOptions() const;
    gdx::GdExportOptions makeExportOptions() const;
    CCPoint placementOrigin() const;
    std::set<int> usedChannels() const;                              // canales que el nivel ya usa (objetos o colores definidos)
    void applyChannelColorsDirect(gdx::GdExportOptions const& eo, gdx::ChannelMap const& cmap);   // define los canales en el selector de color del nivel
    bool colorModeOn() const { return m_colorCheck && m_mono > 0; }

    EditorUI* m_editor = nullptr;
    std::shared_ptr<const gdx::Image> m_image;
    std::shared_ptr<Job> m_job;
    int m_jobCounter = 0;
    gdx::Result m_result;
    bool m_hasResult = false;

    int m_method = 2;       // método normal (0..7); BSP por defecto
    int m_mono = 0;         // 0 = ninguno (se usa el normal); 1..10 = estilo monocromo
    bool m_colorCheck = false;   // estilos ligeros: colores de la imagen en canales de color (sin HSV)
    int m_mainMode = 0;          // métodos 1-8: 0 HSV color, 1 canales de color, 2 gris HSV, 3 gris canales
    int m_budget = 10000;   // máximo permitido
    int m_sizeIdx = 3;

    CCLabelBMFont* m_methodLabel = nullptr;
    CCLabelBMFont* m_descLabel = nullptr;
    CCLabelBMFont* m_budgetLabel = nullptr;
    CCLabelBMFont* m_sizeLabel = nullptr;
    CCLabelBMFont* m_monoLabel = nullptr;
    CCLabelBMFont* m_monoDescLabel = nullptr;
    CCLabelBMFont* m_colorLabel = nullptr;
    CCMenuItemToggler* m_colorToggle = nullptr;
    CCMenuItemToggler* m_chkChannels = nullptr;
    CCMenuItemToggler* m_chkGrayHsv = nullptr;
    CCMenuItemToggler* m_chkGrayCh = nullptr;
    CCLabelBMFont* m_statusLabel = nullptr;
    CCLabelBMFont* m_noteLabel = nullptr;
    CCNode* m_previewHolder = nullptr;
    CCSprite* m_preview = nullptr;
};
