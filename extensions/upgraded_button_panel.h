/**
 * @file upgraded_button_panel.h
 * GUI: UpgradedButtonPanel view module API
 */

#pragma once

#include <gui/view.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Button panel module descriptor */
typedef struct UpgradedButtonPanel UpgradedButtonPanel;

/** Callback type to call for handling selecting upgraded_button_panel items */
typedef void (*ButtonItemCallback)(void* context, uint32_t index, InputType type);

/** Allocate new upgraded_button_panel module.
 *
 * @return     UpgradedButtonPanel instance
 */
UpgradedButtonPanel* upgraded_button_panel_alloc(void);

/** Free upgraded_button_panel module.
 *
 * @param      upgraded_button_panel  UpgradedButtonPanel instance
 */
void upgraded_button_panel_free(UpgradedButtonPanel* upgraded_button_panel);

/** Free items from upgraded_button_panel module. Preallocated matrix stays unchanged.
 *
 * @param      upgraded_button_panel  UpgradedButtonPanel instance
 */
void upgraded_button_panel_reset(UpgradedButtonPanel* upgraded_button_panel);

/** Reserve space for adding items.
 *
 * One does not simply use upgraded_button_panel_add_item() without this function. It
 * should be allocated space for it first.
 *
 * @param      upgraded_button_panel  UpgradedButtonPanel instance
 * @param      reserve_x     number of columns in button_panel
 * @param      reserve_y     number of rows in button_panel
 */
void upgraded_button_panel_reserve(
    UpgradedButtonPanel* upgraded_button_panel,
    size_t reserve_x,
    size_t reserve_y);

/** Add item to upgraded_button_panel module.
 *
 * Have to set element in bounds of allocated size by X and by Y.
 *
 * @param      upgraded_button_panel        UpgradedButtonPanel instance
 * @param      index               value to pass to callback
 * @param      matrix_place_x      coordinates by x-axis on virtual grid, it
 *                                 is only used for navigation
 * @param      matrix_place_y      coordinates by y-axis on virtual grid, it
 *                                 is only used for naviagation
 * @param      x                   x-coordinate to draw icon on
 * @param      y                   y-coordinate to draw icon on
 * @param      icon_name           name of the icon to draw
 * @param      icon_name_selected  name of the icon to draw when current
 *                                 element is selected
 * @param      callback            function to call when specific element is
 *                                 selected (pressed Ok on selected item)
 * @param      callback_context    context to pass to callback
 */
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
    void* callback_context);

/** Get upgraded_button_panel view.
 *
 * @param      upgraded_button_panel  UpgradedButtonPanel instance
 *
 * @return     acquired view
 */
View* upgraded_button_panel_get_view(UpgradedButtonPanel* upgraded_button_panel);

/** Add label to upgraded_button_panel module.
 *
 * @param      upgraded_button_panel  UpgradedButtonPanel instance
 * @param      x             x-coordinate to place label
 * @param      y             y-coordinate to place label
 * @param      font          font to write label with
 * @param      label_str     string label to write
 */
void upgraded_button_panel_add_label(
    UpgradedButtonPanel* upgraded_button_panel,
    uint16_t x,
    uint16_t y,
    Font font,
    const char* label_str);

/** Add a non-button icon to upgraded_button_panel module.
 *
 * @param      upgraded_button_panel  UpgradedButtonPanel instance
 * @param      x             x-coordinate to place icon
 * @param      y             y-coordinate to place icon
 * @param      icon_name     name of the icon to draw
 */
void upgraded_button_panel_add_icon(
    UpgradedButtonPanel* upgraded_button_panel,
    uint16_t x,
    uint16_t y,
    const Icon* icon_name);

#ifdef __cplusplus
}
#endif
