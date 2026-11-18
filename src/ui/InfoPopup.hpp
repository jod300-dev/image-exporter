#pragma once
#include <Geode/Geode.hpp>
#include <string>
#include <vector>

using namespace geode::prelude;

// Cuadro de informacion propio (el FLAlertLayer de GD se salia de la pantalla y recortaba el texto).
// Todo el texto es ASCII (la fuente de GD no tiene tildes ni enie), esta partido a mano en lineas sueltas
// y cabe entero en 400x250, sin scroll, con el boton OK siempre a la vista.
class InfoPopup : public geode::Popup {
public:
    static InfoPopup* create() {
        auto ret = new InfoPopup();
        if (ret->init()) {
            ret->autorelease();
            return ret;
        }
        delete ret;
        return nullptr;
    }

protected:
    static constexpr float kW = 400.f, kH = 250.f;
    static constexpr float kMargin = 22.f;                 // margen izquierdo/derecho del texto
    static constexpr float kScale = 0.62f;                 // escala del texto (chatFont)
    static constexpr float kPitch = 11.5f;                 // separacion entre lineas, fija (no depende de la fuente)
    static constexpr float kParaGap = 5.f;                 // aire extra entre parrafos
    static constexpr float kTop = 44.f;                    // y de la primera linea, medida desde arriba

    bool init() {
        if (!Popup::init(kW, kH)) return false;
        this->setTitle("Image Exporter");

        struct Para { char const* text; ccColor3B color; };
        ccColor3B const white{255, 255, 255}, yellow{255, 255, 80}, red{255, 110, 110};
        std::vector<Para> paras = {
            {"Main (1-8): color rectangles, max. 10,000 objects.", yellow},
            {"BSP for photos, Regions for illustrations and pixel art, Hybrid for maximum quality.", white},
            {"Unchecked = HSV color. Channels spreads the colors over free channels (2-999, then 1020 onward; never 1). Gray uses a grayscale.", white},
            {"Light (9-18): fewer objects and no HSV. With Colors in channels they come out in color.", yellow},
            {"ASCII uses text: set a monospaced font in the level and calibrate the character width in Settings.", white},
            {"With one object selected the image is centered on it; otherwise, on the view.", white},
            {"This cannot be undone: save first.", red},
        };

        // etiqueta invisible que sirve para medir el ancho real de cada linea candidata
        auto measure = CCLabelBMFont::create("", "chatFont.fnt");
        float const maxW = kW - 2.f * kMargin;
        float y = kTop;
        for (auto const& p : paras) {
            std::vector<std::string> lines;
            std::string line, word;
            std::string const text = p.text;
            for (size_t i = 0; i <= text.size(); ++i) {
                if (i < text.size() && text[i] != ' ') { word += text[i]; continue; }
                if (word.empty()) continue;
                std::string cand = line.empty() ? word : line + " " + word;
                measure->setString(cand.c_str());
                if (!line.empty() && measure->getContentSize().width * kScale > maxW) { lines.push_back(line); line = word; }
                else line = cand;
                word.clear();
            }
            if (!line.empty()) lines.push_back(line);

            for (auto const& ln : lines) {
                auto l = CCLabelBMFont::create(ln.c_str(), "chatFont.fnt");
                l->setScale(kScale);
                l->setAnchorPoint({0.f, 1.f});               // alineado a la izquierda, anclado arriba
                l->setColor(p.color);
                m_mainLayer->addChildAtPosition(l, Anchor::TopLeft, ccp(kMargin, -y));
                y += kPitch;
            }
            y += kParaGap;
        }

        auto okSpr = ButtonSprite::create("OK", "goldFont.fnt", "GJ_button_01.png", 0.8f);
        okSpr->setScale(0.85f);
        auto okBtn = CCMenuItemSpriteExtra::create(okSpr, this, menu_selector(InfoPopup::onOk));
        m_buttonMenu->addChildAtPosition(okBtn, Anchor::Bottom, ccp(0.f, 24.f));
        return true;
    }

    void onOk(CCObject*) { this->onClose(nullptr); }
};
