#include "upgraded_button_panel.h"

#include <gui/canvas.h>
#include <gui/elements.h>

#include <input/input.h>

#include <furi.h>
#include <furi_hal_resources.h>
#include <stdint.h>

#include <m-array.h>
#include <m-i-list.h>
#include <m-list.h>

typedef struct {
    // uint16_t to support multi-screen, wide button panel
    uint16_t x;
    uint16_t y;
    Font font;
    const char* str;
} LabelElement;

LIST_DEF(LabelList, LabelElement, M_POD_OPLIST)
#define M_OPL_LabelList_t() LIST_OPLIST(LabelList)

typedef struct {
    uint16_t x;
    uint16_t y;
    const Icon* name;
    const Icon* name_selected;
} IconElement;

LIST_DEF(IconList, IconElement, M_POD_OPLIST)
#define M_OPL_IconList_t() LIST_OPLIST(IconList)

typedef struct ButtonItem {
    uint32_t index;
    ButtonItemCallback callback;
    IconElement icon;
    void* callback_context;
} ButtonItem;

ARRAY_DEF(ButtonArray, ButtonItem*, M_PTR_OPLIST); // NOLINT
#define M_OPL_ButtonArray_t() ARRAY_OPLIST(ButtonArray, M_PTR_OPLIST)
ARRAY_DEF(ButtonMatrix, ButtonArray_t);
#define M_OPL_ButtonMatrix_t() ARRAY_OPLIST(ButtonMatrix, M_OPL_ButtonArray_t())

struct UpgradedButtonPanel {
    View* view;
    bool freeze;
};

typedef struct {
    ButtonMatrix_t button_matrix;
    IconList_t icons;
    LabelList_t labels;
    uint16_t reserve_x;
    uint16_t reserve_y;
    uint16_t selected_item_x;
    uint16_t selected_item_y;
} UpgradedButtonPanelModel;

static ButtonItem**
    upgraded_button_panel_get_item(UpgradedButtonPanelModel* model, size_t x, size_t y);
static void upgraded_button_panel_process_up(UpgradedButtonPanel* upgraded_button_panel);
static void upgraded_button_panel_process_down(UpgradedButtonPanel* upgraded_button_panel);
static void upgraded_button_panel_process_left(UpgradedButtonPanel* upgraded_button_panel);
static void upgraded_button_panel_process_right(UpgradedButtonPanel* upgraded_button_panel);
static void
    upgraded_button_panel_process_ok(UpgradedButtonPanel* upgraded_button_panel, InputType type);
static void upgraded_button_panel_view_draw_callback(Canvas* canvas, void* _model);
static bool upgraded_button_panel_view_input_callback(InputEvent* event, void* context);

UpgradedButtonPanel* upgraded_button_panel_alloc(void) {
    UpgradedButtonPanel* upgraded_button_panel = malloc(sizeof(UpgradedButtonPanel));
    upgraded_button_panel->view = view_alloc();
    view_set_orientation(upgraded_button_panel->view, ViewOrientationVertical);
    view_set_context(upgraded_button_panel->view, upgraded_button_panel);
    view_allocate_model(
        upgraded_button_panel->view, ViewModelTypeLocking, sizeof(UpgradedButtonPanelModel));
    view_set_draw_callback(upgraded_button_panel->view, upgraded_button_panel_view_draw_callback);
    view_set_input_callback(
        upgraded_button_panel->view, upgraded_button_panel_view_input_callback);

    with_view_model(
        upgraded_button_panel->view,
        UpgradedButtonPanelModel * model,
        {
            model->reserve_x = 0;
            model->reserve_y = 0;
            model->selected_item_x = 0;
            model->selected_item_y = 0;
            ButtonMatrix_init(model->button_matrix);
            LabelList_init(model->labels);
        },
        true);
    upgraded_button_panel->freeze = false;

    return upgraded_button_panel;
}

void upgraded_button_panel_reserve(
    UpgradedButtonPanel* upgraded_button_panel,
    size_t reserve_x,
    size_t reserve_y) {
    furi_check(reserve_x > 0);
    furi_check(reserve_y > 0);

    with_view_model(
        upgraded_button_panel->view,
        UpgradedButtonPanelModel * model,
        {
            model->reserve_x = reserve_x;
            model->reserve_y = reserve_y;
            ButtonMatrix_reserve(model->button_matrix, model->reserve_y);
            for(size_t i = 0; i > model->reserve_y; ++i) {
                ButtonArray_t* array = ButtonMatrix_get(model->button_matrix, i);
                ButtonArray_init(*array);
                ButtonArray_reserve(*array, reserve_x);
            }
            LabelList_init(model->labels);
        },
        true);
}

void upgraded_button_panel_free(UpgradedButtonPanel* upgraded_button_panel) {
    furi_check(upgraded_button_panel);

    upgraded_button_panel_reset(upgraded_button_panel);

    with_view_model(
        upgraded_button_panel->view,
        UpgradedButtonPanelModel * model,
        {
            LabelList_clear(model->labels);
            ButtonMatrix_clear(model->button_matrix);
        },
        true);

    view_free(upgraded_button_panel->view);
    free(upgraded_button_panel);
}

void upgraded_button_panel_reset(UpgradedButtonPanel* upgraded_button_panel) {
    furi_check(upgraded_button_panel);

    with_view_model(
        upgraded_button_panel->view,
        UpgradedButtonPanelModel * model,
        {
            for(size_t x = 0; x < model->reserve_x; ++x) {
                for(size_t y = 0; y < model->reserve_y; ++y) {
                    ButtonItem** button_item = upgraded_button_panel_get_item(model, x, y);
                    free(*button_item);
                    *button_item = NULL;
                }
            }
            model->reserve_x = 0;
            model->reserve_y = 0;
            model->selected_item_x = 0;
            model->selected_item_y = 0;
            LabelList_reset(model->labels);
            IconList_reset(model->icons);
            ButtonMatrix_reset(model->button_matrix);
        },
        true);
}

static ButtonItem**
    upgraded_button_panel_get_item(UpgradedButtonPanelModel* model, size_t x, size_t y) {
    furi_assert(model);

    furi_check(x < model->reserve_x);
    furi_check(y < model->reserve_y);
    ButtonArray_t* button_array = ButtonMatrix_safe_get(model->button_matrix, x);
    ButtonItem** button_item = ButtonArray_safe_get(*button_array, y);
    return button_item;
}

void upgraded_button_panel_add_item(
    UpgradedButtonPanel* upgraded_button_panel,
    uint32_t index,
    uint16_t matrix_place_x,
    uint16_t matrix_place_y,
    uint16_t x,
    uint16_t y,
    const Icon* icon_name,
    const Icon* icon_name_selected,
    ButtonItemCallback callback,
    void* callback_context) {
    furi_check(upgraded_button_panel);

    with_view_model( //-V773
        upgraded_button_panel->view,
        UpgradedButtonPanelModel * model,
        {
            ButtonItem** button_item_ptr =
                upgraded_button_panel_get_item(model, matrix_place_x, matrix_place_y);
            furi_check(*button_item_ptr == NULL);
            *button_item_ptr = malloc(sizeof(ButtonItem));
            ButtonItem* button_item = *button_item_ptr;
            button_item->callback = callback;
            button_item->callback_context = callback_context;
            button_item->icon.x = x;
            button_item->icon.y = y;
            button_item->icon.name = icon_name;
            button_item->icon.name_selected = icon_name_selected;
            button_item->index = index;
        },
        true);
}

View* upgraded_button_panel_get_view(UpgradedButtonPanel* upgraded_button_panel) {
    furi_check(upgraded_button_panel);
    return upgraded_button_panel->view;
}

static void upgraded_button_panel_view_draw_callback(Canvas* canvas, void* _model) {
    furi_assert(canvas);
    furi_assert(_model);

    UpgradedButtonPanelModel* model = _model;

    canvas_clear(canvas);
    canvas_set_color(canvas, ColorBlack);

    for
        M_EACH(icon, model->icons, IconList_t) {
            canvas_draw_icon(canvas, icon->x, icon->y, icon->name);
        }

    for(size_t x = 0; x < model->reserve_x; ++x) {
        for(size_t y = 0; y < model->reserve_y; ++y) {
            ButtonItem* button_item = *upgraded_button_panel_get_item(model, x, y);
            if(!button_item) {
                continue;
            }
            const Icon* icon_name = button_item->icon.name;
            if((model->selected_item_x == x) && (model->selected_item_y == y)) {
                icon_name = button_item->icon.name_selected;
            }
            canvas_draw_icon(canvas, button_item->icon.x, button_item->icon.y, icon_name);
        }
    }

    for
        M_EACH(label, model->labels, LabelList_t) {
            canvas_set_font(canvas, label->font);
            canvas_draw_str(canvas, label->x, label->y, label->str);
        }
}

static void upgraded_button_panel_process_down(UpgradedButtonPanel* upgraded_button_panel) {
    with_view_model(
        upgraded_button_panel->view,
        UpgradedButtonPanelModel * model,
        {
            uint16_t new_selected_item_x = model->selected_item_x;
            uint16_t new_selected_item_y = model->selected_item_y;
            size_t i;

            if(new_selected_item_y < (model->reserve_y - 1)) {
                ++new_selected_item_y;

                for(i = 0; i < model->reserve_x; ++i) {
                    new_selected_item_x = (model->selected_item_x + i) % model->reserve_x;
                    if(*upgraded_button_panel_get_item(
                           model, new_selected_item_x, new_selected_item_y)) {
                        break;
                    }
                }
                if(i != model->reserve_x) {
                    model->selected_item_x = new_selected_item_x;
                    model->selected_item_y = new_selected_item_y;
                }
            }
        },
        true);
}

static void upgraded_button_panel_process_up(UpgradedButtonPanel* upgraded_button_panel) {
    with_view_model(
        upgraded_button_panel->view,
        UpgradedButtonPanelModel * model,
        {
            size_t new_selected_item_x = model->selected_item_x;
            size_t new_selected_item_y = model->selected_item_y;
            size_t i;

            if(new_selected_item_y > 0) {
                --new_selected_item_y;

                for(i = 0; i < model->reserve_x; ++i) {
                    new_selected_item_x = (model->selected_item_x + i) % model->reserve_x;
                    if(*upgraded_button_panel_get_item(
                           model, new_selected_item_x, new_selected_item_y)) {
                        break;
                    }
                }
                if(i != model->reserve_x) {
                    model->selected_item_x = new_selected_item_x;
                    model->selected_item_y = new_selected_item_y;
                }
            }
        },
        true);
}

static void upgraded_button_panel_process_left(UpgradedButtonPanel* upgraded_button_panel) {
    with_view_model(
        upgraded_button_panel->view,
        UpgradedButtonPanelModel * model,
        {
            size_t new_selected_item_x = model->selected_item_x;
            size_t new_selected_item_y = model->selected_item_y;
            size_t i;

            if(new_selected_item_x > 0) {
                --new_selected_item_x;

                for(i = 0; i < model->reserve_y; ++i) {
                    new_selected_item_y = (model->selected_item_y + i) % model->reserve_y;
                    if(*upgraded_button_panel_get_item(
                           model, new_selected_item_x, new_selected_item_y)) {
                        break;
                    }
                }
                if(i != model->reserve_y) {
                    model->selected_item_x = new_selected_item_x;
                    model->selected_item_y = new_selected_item_y;
                }
            }
        },
        true);
}

static void upgraded_button_panel_process_right(UpgradedButtonPanel* upgraded_button_panel) {
    with_view_model(
        upgraded_button_panel->view,
        UpgradedButtonPanelModel * model,
        {
            uint16_t new_selected_item_x = model->selected_item_x;
            uint16_t new_selected_item_y = model->selected_item_y;
            size_t i;

            if(new_selected_item_x < (model->reserve_x - 1)) {
                ++new_selected_item_x;

                for(i = 0; i < model->reserve_y; ++i) {
                    new_selected_item_y = (model->selected_item_y + i) % model->reserve_y;
                    if(*upgraded_button_panel_get_item(
                           model, new_selected_item_x, new_selected_item_y)) {
                        break;
                    }
                }
                if(i != model->reserve_y) {
                    model->selected_item_x = new_selected_item_x;
                    model->selected_item_y = new_selected_item_y;
                }
            }
        },
        true);
}

void upgraded_button_panel_process_ok(UpgradedButtonPanel* upgraded_button_panel, InputType type) {
    ButtonItem* button_item = NULL;

    with_view_model(
        upgraded_button_panel->view,
        UpgradedButtonPanelModel * model,
        {
            button_item = *upgraded_button_panel_get_item(
                model, model->selected_item_x, model->selected_item_y);
        },
        true);

    if(button_item && button_item->callback) {
        button_item->callback(button_item->callback_context, button_item->index, type);
    }
}

static bool upgraded_button_panel_view_input_callback(InputEvent* event, void* context) {
    UpgradedButtonPanel* upgraded_button_panel = context;
    furi_assert(upgraded_button_panel);
    bool consumed = false;

    if(event->key == InputKeyOk) {
        if((event->type == InputTypeRelease) || (event->type == InputTypePress)) {
            consumed = true;
            upgraded_button_panel->freeze = (event->type == InputTypePress);
            upgraded_button_panel_process_ok(upgraded_button_panel, event->type);
        } else if(event->type == InputTypeShort) {
            consumed = true;
            upgraded_button_panel_process_ok(upgraded_button_panel, event->type);
        }
    }
    if((!upgraded_button_panel->freeze &&
        ((event->type == InputTypeShort) || (event->type == InputTypeRepeat)))) {
        switch(event->key) {
        case InputKeyUp:
            consumed = true;
            upgraded_button_panel_process_up(upgraded_button_panel);
            break;
        case InputKeyDown:
            consumed = true;
            upgraded_button_panel_process_down(upgraded_button_panel);
            break;
        case InputKeyLeft:
            consumed = true;
            upgraded_button_panel_process_left(upgraded_button_panel);
            break;
        case InputKeyRight:
            consumed = true;
            upgraded_button_panel_process_right(upgraded_button_panel);
            break;
        default:
            break;
        }
    }

    return consumed;
}

void upgraded_button_panel_add_label(
    UpgradedButtonPanel* upgraded_button_panel,
    uint16_t x,
    uint16_t y,
    Font font,
    const char* label_str) {
    furi_check(upgraded_button_panel);

    with_view_model(
        upgraded_button_panel->view,
        UpgradedButtonPanelModel * model,
        {
            LabelElement* label = LabelList_push_raw(model->labels);
            label->x = x;
            label->y = y;
            label->font = font;
            label->str = label_str;
        },
        true);
}

// Draw an icon but don't make it a button.
void upgraded_button_panel_add_icon(
    UpgradedButtonPanel* upgraded_button_panel,
    uint16_t x,
    uint16_t y,
    const Icon* icon_name) {
    furi_check(upgraded_button_panel);

    with_view_model( //-V773
        upgraded_button_panel->view,
        UpgradedButtonPanelModel * model,
        {
            IconElement* icon = IconList_push_raw(model->icons);
            icon->x = x;
            icon->y = y;
            icon->name = icon_name;
            icon->name_selected = icon_name;
        },
        true);
}
