// Dark neon "bento" look for the FLTK control panel.
//
// FLTK 1.3 has no blur or soft shadows, so the theme is built from flat
// colours, thin one pixel borders and two small custom box types.
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
class Fl_Value_Slider;
class Fl_Widget;

namespace UiTheme
{
// Fl_Color is 0xRRGGBB00 for true colours
constexpr Fl_Color rgb(unsigned r, unsigned g, unsigned b)
{
    return static_cast<Fl_Color>((r << 24) | (g << 16) | (b << 8));
}

constexpr Fl_Color kPanel = rgb(0x07, 0x0B, 0x14);        // behind the cards
constexpr Fl_Color kCard = rgb(0x0E, 0x16, 0x26);         // card (bento cell) fill
constexpr Fl_Color kCardEdge = rgb(0x1B, 0x3A, 0x4F);     // card border
constexpr Fl_Color kSurface = rgb(0x12, 0x1D, 0x30);      // toggle that is off, slider, list
constexpr Fl_Color kSurfaceHi = rgb(0x1B, 0x2C, 0x45);    // push button
constexpr Fl_Color kEdge = rgb(0x24, 0x5E, 0x78);         // widget border
constexpr Fl_Color kAccent = rgb(0x00, 0xE5, 0xFF);       // neon cyan: on, selected
constexpr Fl_Color kAccentDim = rgb(0x0A, 0x9F, 0xB8);    // pressed, slider fill
constexpr Fl_Color kAccent2 = rgb(0xFF, 0x2B, 0xD6);      // magenta, used sparingly
constexpr Fl_Color kAccent2Hi = rgb(0xFF, 0x8A, 0xE8);    // magenta while pressed
constexpr Fl_Color kText = rgb(0xD7, 0xF3, 0xFF);         // main text
constexpr Fl_Color kTextDim = rgb(0x9F, 0xB8, 0xC8);      // text of a toggle that is off
constexpr Fl_Color kTextMuted = rgb(0x6F, 0x8C, 0xA3);    // captions
constexpr Fl_Color kTextOnAccent = rgb(0x04, 0x12, 0x1A); // text on a neon fill

constexpr Fl_Fontsize kFontSize = 11;
constexpr Fl_Fontsize kTitleSize = 10;

// Sets the global FLTK colours (so the file dialogs match), the tooltip
// colours and the custom box types. Call it before making the widgets.
// Calling it again does nothing.
void apply();

// The custom box types. Only valid after apply().
Fl_Boxtype cardBox();   // card with a short neon bar on its top edge
Fl_Boxtype widgetBox(); // flat fill with a one pixel border

// Makes a card and leaves it open: add the widgets, then call end() on it.
// The title is drawn in the top left corner; leave kCardTitleHeight for it.
constexpr int kCardTitleHeight = 20;
Fl_Group *beginCard(int x, int y, int w, int h, const char *title);

void stylePanel(Fl_Group *panel);
void styleButton(Fl_Button *button); // push button
void styleToggle(Fl_Button *button); // toggle or radio: neon when on
void styleAccentButton(Fl_Button *button);
void styleSlider(Fl_Value_Slider *slider);
void styleBrowser(Fl_Browser *browser);
void styleCaption(Fl_Widget *widget, Fl_Color color, Fl_Fontsize size);
} // namespace UiTheme
