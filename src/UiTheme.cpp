#include "UiTheme.h"

#pragma warning(push)
#pragma warning(disable : 4312)
#pragma warning(disable : 4311)
#include <FL/Fl.H>
#include <FL/Fl_Box.H>
#include <FL/Fl_Browser.H>
#include <FL/Fl_Button.H>
#include <FL/Fl_Group.H>
#include <FL/Fl_Tooltip.H>
#include <FL/Fl_Value_Input.H>
#include <FL/Fl_Value_Slider.H>
#include <FL/fl_draw.H>
#pragma warning(pop)

namespace
{
// Fl_Button draws a pressed button with fl_down(box()), which is box() | 1,
// so the widget box has to sit on an even number with its twin right after.
constexpr int kFirstBox = FL_FREE_BOXTYPE + (FL_FREE_BOXTYPE & 1);
constexpr Fl_Boxtype kWidgetBox = static_cast<Fl_Boxtype>(kFirstBox);
constexpr Fl_Boxtype kWidgetDownBox = static_cast<Fl_Boxtype>(kFirstBox + 1);
constexpr Fl_Boxtype kCardBox = static_cast<Fl_Boxtype>(kFirstBox + 2);

unsigned char channel(Fl_Color c, int shift)
{
    return static_cast<unsigned char>((c >> shift) & 0xFFu);
}

void setGlobalColor(void (*setter)(unsigned char, unsigned char, unsigned char), Fl_Color c)
{
    setter(channel(c, 24), channel(c, 16), channel(c, 8));
}

// A filled rectangle with its corners rounded off.
void roundedRect(int x, int y, int w, int h, int radius, Fl_Color c)
{
    const int r = (w < 2 * radius || h < 2 * radius) ? 0 : radius;
    fl_color(c);
    if (r == 0)
    {
        fl_rectf(x, y, w, h);
        return;
    }
    fl_rectf(x + r, y, w - 2 * r, h);
    fl_rectf(x, y + r, w, h - 2 * r);
    const int d = 2 * r;
    fl_pie(x, y, d, d, 90.0, 180.0);
    fl_pie(x + w - d - 1, y, d, d, 0.0, 90.0);
    fl_pie(x, y + h - d - 1, d, d, 180.0, 270.0);
    fl_pie(x + w - d - 1, y + h - d - 1, d, d, 270.0, 360.0);
}

// rounded fill inside a thin dark outline
void drawWidgetBox(int x, int y, int w, int h, Fl_Color c)
{
    if (w <= 0 || h <= 0)
        return;
    roundedRect(x, y, w, h, 4, UiTheme::kEdge);
    roundedRect(x + 1, y + 1, w - 2, h - 2, 3, c);
}

// panel: rounded body with a small disclosure triangle where the title starts
void drawCardBox(int x, int y, int w, int h, Fl_Color c)
{
    if (w <= 0 || h <= 0)
        return;
    roundedRect(x, y, w, h, 5, c);
    fl_color(UiTheme::kTextMuted);
    fl_polygon(x + 7, y + 8, x + 15, y + 8, x + 11, y + 13);
}

void styleLabel(Fl_Widget *widget, Fl_Color color)
{
    widget->labelfont(FL_HELVETICA);
    widget->labelsize(UiTheme::kFontSize);
    widget->labelcolor(color);
}
} // namespace

void UiTheme::apply()
{
    static bool applied = false;
    if (applied)
        return;
    applied = true;

    // these also stop FLTK from loading the Windows system colours later
    setGlobalColor(Fl::background, kCard);
    setGlobalColor(Fl::background2, kSurface);
    setGlobalColor(Fl::foreground, kText);

    Fl::set_boxtype(kWidgetBox, drawWidgetBox, 1, 1, 2, 2);
    Fl::set_boxtype(kWidgetDownBox, drawWidgetBox, 1, 1, 2, 2);
    Fl::set_boxtype(kCardBox, drawCardBox, 1, 1, 2, 2);

    Fl_Tooltip::color(kSurfaceHi);
    Fl_Tooltip::textcolor(kText);
    Fl_Tooltip::size(kFontSize);
}

Fl_Boxtype UiTheme::cardBox()
{
    return kCardBox;
}

Fl_Boxtype UiTheme::widgetBox()
{
    return kWidgetBox;
}

Fl_Group *UiTheme::beginCard(int x, int y, int w, int h, const char *title)
{
    Fl_Group *card = new Fl_Group(x, y, w, h);
    card->box(kCardBox);
    card->color(kCard);

    Fl_Box *caption = new Fl_Box(x + 20, y + 4, w - 26, 14, title);
    caption->box(FL_NO_BOX);
    caption->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
    caption->labelfont(FL_HELVETICA);
    caption->labelsize(kTitleSize);
    caption->labelcolor(kText);
    return card;
}

void UiTheme::stylePanel(Fl_Group *panel)
{
    panel->box(FL_FLAT_BOX);
    panel->color(kPanel);
}

void UiTheme::styleButton(Fl_Button *button)
{
    button->box(kWidgetBox);
    button->down_box(kWidgetDownBox);
    button->color(kSurfaceHi);
    button->selection_color(kAccentDim);
    styleLabel(button, kText);
}

void UiTheme::styleToggle(Fl_Button *button)
{
    button->box(kWidgetBox);
    button->down_box(kWidgetDownBox);
    button->color(kSurfaceHi);
    // Fl_Button picks a contrasting label colour by itself when it is on,
    // which gives white text on the blue fill
    button->selection_color(kAccent);
    styleLabel(button, kTextDim);
}

void UiTheme::styleAccentButton(Fl_Button *button)
{
    button->box(kWidgetBox);
    button->down_box(kWidgetDownBox);
    button->color(kAccent2);
    button->selection_color(kAccent2Hi);
    button->labelfont(FL_HELVETICA);
    button->labelsize(12);
    button->labelcolor(kTextOnAccent);
}

void UiTheme::styleSlider(Fl_Value_Slider *slider)
{
    // a fill slider draws a bar from the left edge up to the value
    slider->type(FL_HOR_FILL_SLIDER);
    slider->box(kWidgetBox);
    slider->slider(FL_FLAT_BOX);
    slider->color(kSurface);
    slider->selection_color(kAccent);
    slider->textfont(FL_HELVETICA);
    slider->textsize(kFontSize);
    slider->textcolor(kText);
    slider->align(FL_ALIGN_LEFT);
    styleLabel(slider, kTextMuted);
}

void UiTheme::styleBrowser(Fl_Browser *browser)
{
    browser->box(kWidgetBox);
    browser->color(kSurface);
    browser->selection_color(kAccent);
    browser->textfont(FL_HELVETICA);
    browser->textsize(12);
    browser->textcolor(kText);
    browser->align(FL_ALIGN_TOP_LEFT);
    browser->labelfont(FL_HELVETICA);
    browser->labelsize(kTitleSize);
    browser->labelcolor(kTextMuted);

    browser->scrollbar.box(FL_FLAT_BOX);
    browser->scrollbar.slider(kWidgetBox);
    browser->scrollbar.color(kPanel);
    browser->scrollbar.selection_color(kSurfaceHi);
    browser->scrollbar.labelcolor(kText);
    browser->hscrollbar.box(FL_FLAT_BOX);
    browser->hscrollbar.slider(kWidgetBox);
    browser->hscrollbar.color(kPanel);
    browser->hscrollbar.selection_color(kSurfaceHi);
    browser->hscrollbar.labelcolor(kText);
}

void UiTheme::styleNumberField(Fl_Value_Input *field, Fl_Color labelColor)
{
    field->box(kWidgetBox);
    field->color(kSurface);
    field->selection_color(kAccent);
    field->textfont(FL_HELVETICA);
    field->textsize(kFontSize);
    field->textcolor(kText);
    field->cursor_color(kText);
    field->align(FL_ALIGN_LEFT);
    field->labelfont(FL_HELVETICA_BOLD);
    field->labelsize(kFontSize);
    field->labelcolor(labelColor);
}

void UiTheme::styleCaption(Fl_Widget *widget, Fl_Color color, Fl_Fontsize size)
{
    widget->box(FL_NO_BOX);
    widget->labelfont(FL_HELVETICA_BOLD);
    widget->labelsize(size);
    widget->labelcolor(color);
}
