// Naginata editor: a VSTGUI view built in code. The .uidesc only declares one custom root view,
// which createView() replaces with the procedurally drawn pedal panel (1060 x 740).
#pragma once

#include "naginata_presets.h"
#include "public.sdk/source/vst/vsteditcontroller.h"
#include "vstgui/plugin-bindings/vst3editor.h"

#include <string>
#include <vector>

namespace naginata {

// Resizable, keeps the 1060 x 740 proportions and scales the whole panel with the window.
class NgntEditor : public VSTGUI::AspectRatioVST3Editor {
public:
    explicit NgntEditor(Steinberg::Vst::EditController* controller);

    VSTGUI::CView* createView(const VSTGUI::UIAttributes& attrs,
                              const VSTGUI::IUIDescription* desc) override;

private:
    VSTGUI::CView* buildRoot();
    void refreshPresets();
    void applyPreset(int index);
    void stepPreset(int delta);
    void saveUserPreset();
    std::string presetName() const;

    Steinberg::Vst::EditController* controller_;
    std::vector<presets::Preset> presets_;
    int index_ = 0;
    VSTGUI::CView* nameLabel_ = nullptr;
};

}  // namespace naginata
