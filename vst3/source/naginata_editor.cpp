#include "naginata_editor.h"

#include "vstgui/lib/cviewcontainer.h"
#include "vstgui/lib/controls/ccontrol.h"
#include "vstgui/lib/cfont.h"
#include "vstgui/lib/cdrawcontext.h"
#include "vstgui/uidescription/uiattributes.h"

#include <algorithm>
#include <cmath>
#include <ctime>
#include <functional>
#include <string>
#include <vector>

// Trial expiry in unix seconds, set by the build (0 = full version). Same default as naginata_vst3.cpp.
#ifndef NAGINATA_TRIAL_EXPIRY
#define NAGINATA_TRIAL_EXPIRY 0
#endif

namespace naginata {
namespace {

using namespace VSTGUI;

// Palette: black base, samurai pink for Unit I, green for Unit II, cream text.
const CColor kBg(11, 11, 12);
const CColor kKnobBody(26, 26, 28);
const CColor kPanelLine(52, 52, 56);
const CColor kCream(242, 236, 224);
const CColor kDim(96, 96, 104);
const CColor kPink(217, 74, 124);
const CColor kGreen(58, 166, 117);

constexpr double kPiD = 3.14159265358979323846;

CFontRef font(double size, int32_t style) {
    return makeOwned<CFontDesc>("Lato", size, style);
}

// Knob: value shown as ticks, dark body, cream pointer. Drag up or down to change.
class NgntKnob : public CControl {
public:
    CLASS_METHODS(NgntKnob, CControl)

    // inverted: the parameter runs the other way to the knob (up shows the parameter at 0).
    NgntKnob(const CRect& r, IControlListener* l, int32_t tag, CColor accent, const char* label,
             bool inverted = false)
    : CControl(r, l, tag), accent_(accent), label_(label), inverted_(inverted) {}

    // Knob position 0..1, from the parameter value
    double shown() const { return inverted_ ? 1.0 - getValueNormalized() : getValueNormalized(); }

    void draw(CDrawContext* ctx) override {
        const CRect r = getViewSize();
        const double cx = (r.left + r.right) / 2.0;
        const double cy = r.top + 52.0;
        const double R = 34.0;
        const double v = shown();

        ctx->setLineWidth(2);
        for (int i = 0; i <= 10; ++i) {
            const double a = (-135.0 + 270.0 * i / 10.0) * kPiD / 180.0;
            const double s = std::sin(a), c = std::cos(a);
            ctx->setFrameColor(i / 10.0 <= v + 1e-6 ? accent_ : kDim);
            ctx->drawLine(CPoint(cx + (R + 5) * s, cy - (R + 5) * c),
                          CPoint(cx + (R + 10) * s, cy - (R + 10) * c));
        }

        const CRect body(cx - R, cy - R, cx + R, cy + R);
        ctx->setFillColor(kKnobBody);
        ctx->drawEllipse(body, kDrawFilled);
        ctx->setFrameColor(kPanelLine);
        ctx->setLineWidth(2);
        ctx->drawEllipse(body, kDrawStroked);

        const double a = (-135.0 + 270.0 * v) * kPiD / 180.0;
        const double s = std::sin(a), c = std::cos(a);
        ctx->setFrameColor(kCream);
        ctx->setLineWidth(3);
        ctx->drawLine(CPoint(cx + 0.2 * R * s, cy - 0.2 * R * c),
                      CPoint(cx + 0.85 * R * s, cy - 0.85 * R * c));

        ctx->setFont(font(12.0, kBoldFace));
        ctx->setFontColor(kCream);
        ctx->drawString(label_, CRect(r.left, r.bottom - 20, r.right, r.bottom), kCenterText);
    }

    CMouseEventResult onMouseDown(CPoint& where, const CButtonState& buttons) override {
        if (!buttons.isLeftButton()) return kMouseEventNotHandled;
        dragging_ = true;
        startY_ = where.y;
        startV_ = float(shown());
        beginEdit();
        return kMouseEventHandled;
    }
    CMouseEventResult onMouseMoved(CPoint& where, const CButtonState&) override {
        if (!dragging_) return kMouseEventNotHandled;
        const float shownV = std::clamp(startV_ + float((startY_ - where.y) / 200.0), 0.f, 1.f);
        setValueNormalized(inverted_ ? 1.f - shownV : shownV);
        valueChanged();
        invalid();
        return kMouseEventHandled;
    }
    CMouseEventResult onMouseUp(CPoint&, const CButtonState&) override {
        if (!dragging_) return kMouseEventNotHandled;
        dragging_ = false;
        endEdit();
        return kMouseEventHandled;
    }

private:
    CColor accent_;
    const char* label_;
    bool inverted_ = false;
    bool dragging_ = false;
    CCoord startY_ = 0;
    float startV_ = 0;
};

// Multi-position switch: one segment per position, the active one filled with the accent.
class NgntSwitch : public CControl {
public:
    CLASS_METHODS(NgntSwitch, CControl)

    NgntSwitch(const CRect& r, IControlListener* l, int32_t tag, CColor accent,
               std::vector<std::string> labels)
    : CControl(r, l, tag), accent_(accent), labels_(std::move(labels)) {}

    void draw(CDrawContext* ctx) override {
        const CRect r = getViewSize();
        const int n = int(labels_.size());
        const double seg = r.getWidth() / n;
        const int active = int(std::lround(getValueNormalized() * (n - 1)));
        ctx->setFont(font(11.0, kBoldFace));
        for (int i = 0; i < n; ++i) {
            const CRect s(r.left + i * seg, r.top, r.left + (i + 1) * seg, r.bottom);
            if (i == active) {
                ctx->setFillColor(accent_);
                ctx->drawRect(s, kDrawFilled);
                ctx->setFontColor(kBg);
            } else {
                ctx->setFontColor(kDim);
            }
            ctx->setFrameColor(kPanelLine);
            ctx->setLineWidth(1.5);
            ctx->drawRect(s, kDrawStroked);
            ctx->drawString(labels_[i].c_str(), s, kCenterText);
        }
    }

    CMouseEventResult onMouseDown(CPoint& where, const CButtonState& buttons) override {
        if (!buttons.isLeftButton()) return kMouseEventNotHandled;
        const CRect r = getViewSize();
        const int n = int(labels_.size());
        const double seg = r.getWidth() / n;
        const int idx = std::clamp(int((where.x - r.left) / seg), 0, n - 1);
        beginEdit();
        setValueNormalized(n > 1 ? float(idx) / float(n - 1) : 0.f);
        valueChanged();
        endEdit();
        invalid();
        return kMouseEventHandled;
    }

private:
    CColor accent_;
    std::vector<std::string> labels_;
};

// On/off engage switch with an LED.
class NgntToggle : public CControl {
public:
    CLASS_METHODS(NgntToggle, CControl)

    NgntToggle(const CRect& r, IControlListener* l, int32_t tag, CColor accent)
    : CControl(r, l, tag), accent_(accent) {}

    void draw(CDrawContext* ctx) override {
        const CRect r = getViewSize();
        const bool on = getValueNormalized() > 0.5f;
        const double cy = (r.top + r.bottom) / 2.0;
        ctx->setFrameColor(on ? accent_ : kPanelLine);
        ctx->setLineWidth(2);
        ctx->drawRect(r, kDrawStroked);
        ctx->setFillColor(on ? accent_ : kDim);
        ctx->drawEllipse(CRect(r.left + 10, cy - 6, r.left + 22, cy + 6), kDrawFilled);
        ctx->setFont(font(12.0, kBoldFace));
        ctx->setFontColor(on ? kCream : kDim);
        ctx->drawString("ENGAGE", CRect(r.left + 26, r.top, r.right, r.bottom), kCenterText);
    }

    CMouseEventResult onMouseDown(CPoint&, const CButtonState& buttons) override {
        if (!buttons.isLeftButton()) return kMouseEventNotHandled;
        const bool on = getValueNormalized() > 0.5f;
        beginEdit();
        setValueNormalized(on ? 0.f : 1.f);
        valueChanged();
        endEdit();
        invalid();
        return kMouseEventHandled;
    }

private:
    CColor accent_;
};

// Small text button for the preset bar. Calls back into the editor on click.
class NgntButton : public CView {
public:
    CLASS_METHODS(NgntButton, CView)

    NgntButton(const CRect& r, const char* text, std::function<void()> onClick)
    : CView(r), text_(text), onClick_(std::move(onClick)) {}

    void draw(CDrawContext* ctx) override {
        const CRect r = getViewSize();
        ctx->setFrameColor(kPanelLine);
        ctx->setLineWidth(1.5);
        ctx->drawRect(r, kDrawStroked);
        ctx->setFont(font(13.0, kBoldFace));
        ctx->setFontColor(kCream);
        ctx->drawString(text_, r, kCenterText);
    }

    CMouseEventResult onMouseDown(CPoint&, const CButtonState& buttons) override {
        if (!buttons.isLeftButton()) return kMouseEventNotHandled;
        if (onClick_) onClick_();
        return kMouseEventHandled;
    }

private:
    const char* text_;
    std::function<void()> onClick_;
};

// Shows the current preset name. The text is read through a callback each time it is drawn.
class NgntLabel : public CView {
public:
    CLASS_METHODS(NgntLabel, CView)

    NgntLabel(const CRect& r, std::function<std::string()> text) : CView(r), text_(std::move(text)) {}

    void draw(CDrawContext* ctx) override {
        const CRect r = getViewSize();
        ctx->setFrameColor(kPanelLine);
        ctx->setLineWidth(1.5);
        ctx->drawRect(r, kDrawStroked);
        const std::string s = text_ ? text_() : std::string();
        ctx->setFont(font(13.0, kBoldFace));
        ctx->setFontColor(kPink);
        ctx->drawString(s.c_str(), r, kCenterText);
    }

private:
    std::function<std::string()> text_;
};

// Static background: wordmark, the two unit panels and their captions.
class NgntBackground : public CView {
public:
    CLASS_METHODS(NgntBackground, CView)

    using CView::CView;

    void draw(CDrawContext* ctx) override {
        const CRect r = getViewSize();
        ctx->setFillColor(kBg);
        ctx->drawRect(r, kDrawFilled);

        ctx->setFont(font(52.0, kBoldFace));
        ctx->setFontColor(kCream);
        ctx->drawString("NAGINATA", CRect(30, 18, 420, 78), kLeftText);
        ctx->setFont(font(11.0, 0));
        ctx->setFontColor(kDim);
        ctx->drawString("DRIVE AND FOLD", CRect(32, 80, 420, 100), kLeftText);
        ctx->setFrameColor(kPink);
        ctx->setLineWidth(3);
        ctx->drawLine(CPoint(32, 106), CPoint(260, 106));

        panel(ctx, CRect(20, 120, 520, 680), kPink, "NAGI");
        panel(ctx, CRect(540, 120, 1040, 680), kGreen, "NATA");

        ctx->setFont(font(10.0, 0));
        ctx->setFontColor(kDim);
        ctx->drawString("MID", CRect(70, 500, 300, 516), kLeftText);
        ctx->drawString("SUM", CRect(560, 500, 900, 516), kLeftText);
        ctx->drawString("SYMMETRY", CRect(560, 580, 900, 596), kLeftText);
        ctx->drawString("PRESET", CRect(700, 4, 900, 18), kLeftText);

        // Trial builds only: days left, counted from the expiry set at build time.
        if (NAGINATA_TRIAL_EXPIRY != 0) {
            const long long secondsLeft = static_cast<long long>(NAGINATA_TRIAL_EXPIRY) -
                                          static_cast<long long>(std::time(nullptr));
            const long long daysLeft = std::max(0LL, secondsLeft / 86400);
            const std::string text = "TRIAL: " + std::to_string(daysLeft) + " DAYS LEFT";
            ctx->setFont(font(10.0, kBoldFace));
            ctx->setFontColor(kPink);
            ctx->drawString(text.c_str(), CRect(700, 60, 1036, 76), kRightText);
        }
    }

private:
    void panel(CDrawContext* ctx, const CRect& rect, CColor accent, const char* title) {
        ctx->setFrameColor(accent);
        ctx->setLineWidth(2);
        ctx->drawRect(rect, kDrawStroked);
        ctx->setFont(font(18.0, kBoldFace));
        ctx->setFontColor(accent);
        ctx->drawString(title, CRect(rect.left + 20, rect.top + 14, rect.left + 300, rect.top + 42),
                        kLeftText);
    }
};

void addKnob(CViewContainer* root, IControlListener* l, double x, double y, int32_t tag,
             CColor accent, const char* label, bool inverted = false) {
    // 110 px wide, so a label up to 110 px fits; neighbouring knobs are 110 px apart.
    root->addView(new NgntKnob(CRect(x - 7, y, x + 103, y + 120), l, tag, accent, label, inverted));
}

}  // namespace

NgntEditor::NgntEditor(Steinberg::Vst::EditController* controller)
: AspectRatioVST3Editor(controller, "view", "ngnt.uidesc"), controller_(controller) {
    setMinZoomFactor(0.6);   // smallest window: 636 x 444, same proportions as 1060 x 740
    refreshPresets();
}

CView* NgntEditor::createView(const UIAttributes& attrs, const IUIDescription* desc) {
    const std::string* name = attrs.getAttributeValue(IUIDescription::kCustomViewName);
    if (name && *name == "NgntRoot") return buildRoot();
    return VST3Editor::createView(attrs, desc);
}

// Presets --------------------------------------------------------------------------------------

void NgntEditor::refreshPresets() {
    presets_ = presets::factoryPresets();
    for (auto& p : presets::loadUserPresets()) presets_.push_back(std::move(p));
    if (index_ < 0 || index_ >= int(presets_.size())) index_ = 0;
}

void NgntEditor::applyPreset(int index) {
    if (presets_.empty()) return;
    index_ = std::clamp(index, 0, int(presets_.size()) - 1);
    const auto& values = presets_[index_].values;
    for (int32_t tag = 0; tag < presets::kParamCount; ++tag) {
        controller_->beginEdit(tag);
        controller_->performEdit(tag, values[tag]);
        controller_->endEdit(tag);
    }
    if (nameLabel_) nameLabel_->invalid();
}

void NgntEditor::stepPreset(int delta) {
    if (presets_.empty()) return;
    const int n = int(presets_.size());
    applyPreset((index_ + delta % n + n) % n);
}

void NgntEditor::saveUserPreset() {
    presets::Preset p;
    int userCount = 0;
    for (const auto& existing : presets_) {
        if (existing.name.rfind("User ", 0) == 0) ++userCount;
    }
    p.name = "User " + std::to_string(userCount + 1);
    for (int32_t tag = 0; tag < presets::kParamCount; ++tag) {
        p.values.push_back(controller_->getParamNormalized(tag));
    }
    if (!presets::saveUserPreset(p)) return;
    refreshPresets();
    for (int i = 0; i < int(presets_.size()); ++i) {
        if (presets_[i].name == p.name) index_ = i;
    }
    if (nameLabel_) nameLabel_->invalid();
}

std::string NgntEditor::presetName() const {
    return presets_.empty() ? std::string() : presets_[index_].name;
}

// Layout ---------------------------------------------------------------------------------------

CView* NgntEditor::buildRoot() {
    auto* root = new CViewContainer(CRect(0, 0, 1060, 740));
    root->addView(new NgntBackground(CRect(0, 0, 1060, 740)));
    IControlListener* l = this;
    const CColor pink = kPink, green = kGreen;

    // Preset bar: step back, name, step forward, save.
    root->addView(new NgntButton(CRect(700, 20, 740, 56), "<", [this] { stepPreset(-1); }));
    nameLabel_ = new NgntLabel(CRect(746, 20, 926, 56), [this] { return presetName(); });
    root->addView(nameLabel_);
    root->addView(new NgntButton(CRect(932, 20, 972, 56), ">", [this] { stepPreset(1); }));
    root->addView(new NgntButton(CRect(980, 20, 1036, 56), "SAVE", [this] { saveUserPreset(); }));

    // Unit I (fuzz): knobs in two rows, Mid switch, Engage.
    addKnob(root, l, 70, 190, 1, pink, "LEVEL");
    addKnob(root, l, 180, 190, 2, pink, "TONE");
    addKnob(root, l, 290, 190, 3, pink, "GAIN");
    addKnob(root, l, 400, 190, 4, pink, "WET");
    addKnob(root, l, 70, 350, 6, pink, "HIGH PASS");
    addKnob(root, l, 180, 350, 7, pink, "LOW PASS");
    addKnob(root, l, 290, 350, 17, pink, "HARMONICS");
    addKnob(root, l, 400, 350, 18, pink, "HARM. DEPTH");
    root->addView(new NgntSwitch(CRect(70, 520, 370, 560), l, 5, pink, {"OFF", "BOOST", "SCOOP"}));
    root->addView(new NgntToggle(CRect(380, 520, 500, 560), l, 0, pink));

    // Unit II (wavefolder): knobs in two rows, Sum switch, Symmetry switch, Engage.
    // FOLD is the clean parameter run backwards: up means more fold.
    addKnob(root, l, 560, 190, 9, green, "FOLD", true);
    addKnob(root, l, 670, 190, 10, green, "+ FOLD");
    addKnob(root, l, 780, 190, 11, green, "- FOLD");
    addKnob(root, l, 890, 190, 14, green, "WET");
    addKnob(root, l, 670, 350, 12, green, "+ RANGE");
    addKnob(root, l, 780, 350, 13, green, "- RANGE");
    root->addView(new NgntSwitch(CRect(560, 520, 860, 560), l, 15, green,
                                 {"LIMITER", "NEUTRAL", "DISTORTION"}));
    root->addView(new NgntToggle(CRect(900, 520, 1020, 560), l, 8, green));
    root->addView(new NgntSwitch(CRect(560, 600, 760, 640), l, 16, green, {"ASYMMETRIC", "SYMMETRIC"}));
    return root;
}

}  // namespace naginata
