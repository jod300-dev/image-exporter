#pragma once
#include <Geode/Geode.hpp>
#include <atomic>
#include <memory>
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

    void stepMethod(int dir);
    void stepMono(int dir);
    void stepBudget(int dir);
    void stepSize(int dir);
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

    EditorUI* m_editor = nullptr;
    std::shared_ptr<const gdx::Image> m_image;
    std::shared_ptr<Job> m_job;
    int m_jobCounter = 0;
    gdx::Result m_result;
    bool m_hasResult = false;

    int m_method = 2;       // método normal (0..7); BSP por defecto
    int m_mono = 0;         // 0 = ninguno (se usa el normal); 1..10 = estilo monocromo
    int m_budget = 10000;   // máximo permitido
    int m_sizeIdx = 3;

    CCLabelBMFont* m_methodLabel = nullptr;
    CCLabelBMFont* m_descLabel = nullptr;
    CCLabelBMFont* m_budgetLabel = nullptr;
    CCLabelBMFont* m_sizeLabel = nullptr;
    CCLabelBMFont* m_monoLabel = nullptr;
    CCLabelBMFont* m_monoDescLabel = nullptr;
    CCLabelBMFont* m_statusLabel = nullptr;
    CCLabelBMFont* m_noteLabel = nullptr;
    CCNode* m_previewHolder = nullptr;
    CCSprite* m_preview = nullptr;
};
