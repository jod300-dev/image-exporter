#pragma once
#include <Geode/Geode.hpp>
#include <atomic>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include "engine.hpp"
#include "gdexport.hpp"

using namespace geode::prelude;

// Ventana del exportador (430x268, cabe en los 320 de alto de la pantalla de GD):
//   izquierda: vista previa + estado        derecha: pestañas Principales/Ligeros, método, sliders, opciones de color
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

    // --- acciones ---
    void onPick(CCObject*);
    void onPlace(CCObject*);
    void onInfo(CCObject*);
    void onSettings(CCObject*);
    void onTabMain(CCObject*) { setTab(0); }
    void onTabLight(CCObject*) { setTab(1); }
    void onPrev(CCObject*) { stepMethod(-1); }
    void onNext(CCObject*) { stepMethod(+1); }
    void onBudgetSlider(CCObject* sender);
    void onSizeSlider(CCObject* sender);
    void onColorToggle(CCObject*);                                       // pestaña Ligeros: colores en canales
    void onMainChannels(CCObject*) { setMainMode(m_mainMode == 1 ? 0 : 1); }
    void onMainGrayHsv(CCObject*) { setMainMode(m_mainMode == 2 ? 0 : 2); }
    void onMainGrayCh(CCObject*) { setMainMode(m_mainMode == 3 ? 0 : 3); }

    void setTab(int tab);
    void setMainMode(int mode);       // 0 HSV color (por defecto), 1 canales de color, 2 gris HSV, 3 gris canales
    void stepMethod(int dir);
    gdx::Method effectiveMethod() const;
    void refreshLabels();

    void loadImageBytes(std::vector<uint8_t> const& bytes);
    void scheduleRecompute();
    void recompute(float = 0.f);
    void tick(float);
    void applyResult(std::shared_ptr<Job> job);
    void buildPreview();
    void setStatus(std::string const& a, std::string const& b = "", std::string const& note = "");
    gdx::Options makeOptions() const;
    gdx::GdExportOptions makeExportOptions() const;
    CCPoint placementOrigin() const;
    std::set<int> usedChannels() const;                                  // canales que el nivel ya usa (objetos o colores definidos)
    void applyChannelColorsDirect(gdx::GdExportOptions const& eo, gdx::ChannelMap const& cmap);
    bool colorModeOn() const { return m_tab == 1 && m_colorCheck; }

    EditorUI* m_editor = nullptr;
    std::shared_ptr<const gdx::Image> m_image;
    std::shared_ptr<Job> m_job;
    int m_jobCounter = 0;
    gdx::Result m_result;
    bool m_hasResult = false;

    int m_tab = 0;               // 0 principales (1-8), 1 ligeros (9-18)
    int m_method = 2;            // método principal (0..7); BSP por defecto
    int m_style = 0;             // estilo ligero (0..9)
    int m_mainMode = 0;          // principales: 0 HSV color, 1 canales de color, 2 gris HSV, 3 gris canales
    bool m_colorCheck = false;   // ligeros: colores de la imagen en canales de color (sin HSV)
    int m_budget = 10000;        // máximo permitido
    int m_sizeU = 600;           // lado largo en unidades de GD

    CCMenuItemToggler* m_tabMain = nullptr;
    CCMenuItemToggler* m_tabLight = nullptr;
    CCLabelBMFont* m_methodLabel = nullptr;
    CCLabelBMFont* m_descLabel = nullptr;
    CCLabelBMFont* m_budgetValue = nullptr;
    CCLabelBMFont* m_sizeValue = nullptr;
    Slider* m_budgetSlider = nullptr;
    Slider* m_sizeSlider = nullptr;
    CCMenuItemToggler* m_chkChannels = nullptr;
    CCMenuItemToggler* m_chkGrayHsv = nullptr;
    CCMenuItemToggler* m_chkGrayCh = nullptr;
    CCMenuItemToggler* m_chkColor = nullptr;
    std::vector<CCNode*> m_mainOpts;                                      // controles de la fila de color de Principales
    std::vector<CCNode*> m_lightOpts;                                     // ... y de Ligeros
    CCLabelBMFont* m_statA = nullptr;
    CCLabelBMFont* m_statB = nullptr;
    CCLabelBMFont* m_noteLabel = nullptr;
    CCNode* m_previewHolder = nullptr;
    CCSprite* m_preview = nullptr;
};
