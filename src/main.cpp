#include <Geode/Geode.hpp>
#include <Geode/modify/EditorUI.hpp>
#include "ui/ExportPopup.hpp"

using namespace geode::prelude;

// Añade un botón a la barra "Edit" del editor que abre el exportador de imágenes.
// (Mismo punto de enganche que usa el mod "Art importer".)
class $modify(ImgExportEditorUI, EditorUI) {
    void onOpenImageExporter(CCObject*) {
        ExportPopup::create(this)->show();
    }

    void createMoveMenu() {
        EditorUI::createMoveMenu();

        CCMenuItemSpriteExtra* btn = this->getSpriteButton(
            "gdImgExportBtn.png"_spr,
            menu_selector(ImgExportEditorUI::onOpenImageExporter),
            nullptr, 0.9f
        );
        btn->setID("image-export-button"_spr);
        m_editButtonBar->m_buttonArray->addObject(btn);

        int rows = GameManager::sharedState()->getIntGameVariable("0049");
        int cols = GameManager::sharedState()->getIntGameVariable("0050");
        m_editButtonBar->reloadItems(rows, cols);
    }
};
