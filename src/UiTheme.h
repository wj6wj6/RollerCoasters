// The look of the FLTK control panel, modelled on the properties editor of a
// 3D modelling program: dark grey panels with a small disclosure triangle,
// rounded grey buttons, blue for whatever is switched on or selected.
//
// FLTK 1.3 has no blur or soft shadows, so the theme is built from flat
// colours and two small custom box types with rounded corners.
#pragma once

#pragma warning(push)
#pragma warning(disable : 4312)
#pragma warning(disable : 4311)
#include <FL/Enumerations.H>
#pragma warning(pop)

class Fl_Box;
class Fl_Browser;
class Fl_Button;
class Fl_Group;
class Fl_Value_Input;
class Fl_Value_Slider;
class Fl_Widget;

namespace UiTheme
{
// Fl_Color is 0xRRGGBB00 for true colours
constexpr Fl_Color rgb(unsigned r, unsigned g, unsigned b)
{
    return static_cast<Fl_Color>((r << 24) | (g << 16) | (b << 8));
}

constexpr Fl_Color kPanel = rgb(0x30, 0x30, 0x30);        // editor background
constexpr Fl_Color kCard = rgb(0x3D, 0x3D, 0x3D);         // panel body
constexpr Fl_Color kCardEdge = rgb(0x2A, 0x2A, 0x2A);     // outline of panels and widgets
constexpr Fl_Color kSurface = rgb(0x28, 0x28, 0x28);      // number fields, sliders, lists
constexpr Fl_Color kSurfaceHi = rgb(0x54, 0x54, 0x54);    // buttons and toggles that are off
constexpr Fl_Color kEdge = rgb(0x2A, 0x2A, 0x2A);         // widget outline
constexpr Fl_Color kAccent = rgb(0x47, 0x72, 0xB3);       // blue: on, selected, slider fill
constexpr Fl_Color kAccentDim = rgb(0x3A, 0x5F, 0x96);    // pressed
constexpr Fl_Color kAccent2 = rgb(0x47, 0x72, 0xB3);      // the one prominent action button
constexpr Fl_Color kAccent2Hi = rgb(0x5A, 0x88, 0xCC);    // while pressed
constexpr Fl_Color kText = rgb(0xE6, 0xE6, 0xE6);         // main text
constexpr Fl_Color kTextDim = rgb(0xE6, 0xE6, 0xE6);      // text of a toggle that is off
constexpr Fl_Color kTextMuted = rgb(0x9A, 0x9A, 0x9A);    // captions
constexpr Fl_Color kTextOnAccent = rgb(0xFF, 0xFF, 0xFF); // text on a blue fill
constexpr Fl_Color kAxisX = rgb(0xFF, 0x33, 0x52);        // the axis colours of the 3D view
constexpr Fl_Color kAxisY = rgb(0x8B, 0xDC, 0x00);
constexpr Fl_Color kAxisZ = rgb(0x28, 0x90, 0xFF);

constexpr Fl_Fontsize kFontSize = 11;
constexpr Fl_Fontsize kTitleSize = 11;

// Sets the global FLTK colours (so the file dialogs match), the tooltip
// colours and the custom box types. Call it before making the widgets.
// Calling it again does nothing.
void apply();

// The custom box types. Only valid after apply().
Fl_Boxtype cardBox();   // rounded panel with a disclosure triangle in its title row
Fl_Boxtype widgetBox(); // rounded fill with a thin dark outline

// Makes a card and leaves it open: add the widgets, then call end() on it.
// The title is drawn in the top left corner; leave kCardTitleHeight for it.
constexpr int kCardTitleHeight = 20;
Fl_Group *beginCard(int x, int y, int w, int h, const char *title);

void stylePanel(Fl_Group *panel);
void styleButton(Fl_Button *button); // push button
void styleToggle(Fl_Button *button); // toggle or radio: blue when on
void styleAccentButton(Fl_Button *button);
void styleSlider(Fl_Value_Slider *slider);
void styleBrowser(Fl_Browser *browser);
void styleNumberField(Fl_Value_Input *field, Fl_Color labelColor);
void styleCaption(Fl_Widget *widget, Fl_Color color, Fl_Fontsize size);
} // namespace UiTheme
