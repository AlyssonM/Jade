#ifndef AMALGAMATED_BUILD
#include "../button_events.h"
#include "../jade_assert.h"
#include "../random.h"
#include "../ui.h"

#include <math.h>

static const char CHAR_BACKSPACE = '|';
static const char PIN_CHARS[] = { '0', '1', '2', '3', '4', '5', '6', '7', '8', '9', CHAR_BACKSPACE };
static const uint32_t NUM_PIN_CHARS = sizeof(PIN_CHARS) / sizeof(PIN_CHARS[0]);
static const uint32_t NUM_PIN_VALUES = NUM_PIN_CHARS - 1; // ie. not including backspace

static inline char get_pin_value(size_t index)
{
    JADE_ASSERT(index < NUM_PIN_CHARS);
    return PIN_CHARS[index];
}

static void pin_digit_button_handler(void* handler_arg, esp_event_base_t base, int32_t id, void* event_data);

static void reinitialise_current_pin_digit(pin_insert_t* pin_insert)
{
    JADE_ASSERT(pin_insert);

    switch (pin_insert->initial_state) {
    case ZERO:
        pin_insert->current_selected_value = 0;
        break;
    case POSITION:
        pin_insert->current_selected_value = pin_insert->selected_digit;
        break;
    default:
        pin_insert->current_selected_value = get_uniform_random_byte(NUM_PIN_VALUES);
        break;
    }
}

static void update_digit_node(pin_insert_t* pin_insert, uint8_t i)
{
    JADE_ASSERT(pin_insert);
    JADE_ASSERT(i < PIN_SIZE);

    char strdigit[] = { '\0', '\0' };
    switch (pin_insert->digit_status[i]) {
    case EMPTY:
        gui_set_color(pin_insert->pin_digit_nodes[i].fill_node, TFT_BLACK);
        gui_set_borders(pin_insert->pin_digit_nodes[i].fill_node, TFT_LIGHTGREY, 2, GUI_BORDER_ALL);
        gui_update_text(pin_insert->pin_digit_nodes[i].up_arrow_node, "");
        gui_update_text(pin_insert->pin_digit_nodes[i].down_arrow_node, "");
        gui_set_active(pin_insert->pin_digit_nodes[i].up_arrow_node->parent, false);
        gui_set_active(pin_insert->pin_digit_nodes[i].digit_node->parent, false);
        gui_set_active(pin_insert->pin_digit_nodes[i].down_arrow_node->parent, false);
        break;
    case SELECTED:
        gui_set_color(pin_insert->pin_digit_nodes[i].fill_node, gui_get_highlight_color());
        gui_set_borders(pin_insert->pin_digit_nodes[i].fill_node, gui_get_highlight_color(), 2, GUI_BORDER_ALL);
        gui_update_text(pin_insert->pin_digit_nodes[i].up_arrow_node, "K");
        gui_update_text(pin_insert->pin_digit_nodes[i].down_arrow_node, "L");
        gui_set_active(pin_insert->pin_digit_nodes[i].up_arrow_node->parent, true);
        gui_set_active(pin_insert->pin_digit_nodes[i].digit_node->parent, true);
        gui_set_active(pin_insert->pin_digit_nodes[i].down_arrow_node->parent, true);
        strdigit[0] = PIN_CHARS[pin_insert->current_selected_value];
        break;
    case SET:
        gui_set_color(pin_insert->pin_digit_nodes[i].fill_node, TFT_BLACK);
        gui_set_borders(pin_insert->pin_digit_nodes[i].fill_node, gui_get_highlight_color(), 2, GUI_BORDER_ALL);
        gui_update_text(pin_insert->pin_digit_nodes[i].up_arrow_node, "");
        gui_update_text(pin_insert->pin_digit_nodes[i].down_arrow_node, "");
        gui_set_active(pin_insert->pin_digit_nodes[i].up_arrow_node->parent, false);
        gui_set_active(pin_insert->pin_digit_nodes[i].digit_node->parent, false);
        gui_set_active(pin_insert->pin_digit_nodes[i].down_arrow_node->parent, false);
        strdigit[0] = pin_insert->pin_digits_shown ? PIN_CHARS[pin_insert->pin[i]] : '*';
        break;
    }
    gui_update_text(pin_insert->pin_digit_nodes[i].digit_node, strdigit);
    gui_repaint(pin_insert->pin_digit_nodes[i].fill_node);
}

static void update_pin_text_display(pin_insert_t* pin_insert)
{
    if (!pin_insert || !pin_insert->pin_text_node) {
        return;
    }

    char pin_str[(PIN_SIZE * 2) + 1];
    size_t pos = 0;
    for (size_t i = 0; i < pin_insert->selected_digit; ++i) {
        if (pos > 0) {
            pin_str[pos++] = ' ';
        }
        pin_str[pos++] = pin_insert->pin_digits_shown ? PIN_CHARS[pin_insert->pin[i]] : '*';
    }
    pin_str[pos] = '\0';
    gui_update_text(pin_insert->pin_text_node, pin_str);
}

static bool keypad_backspace(pin_insert_t* pin_insert)
{
    JADE_ASSERT(pin_insert);

    if (pin_insert->selected_digit == 0) {
        return false;
    }

    pin_insert->selected_digit--;
    pin_insert->pin[pin_insert->selected_digit] = 0xFF;
    pin_insert->digit_status[pin_insert->selected_digit] = EMPTY;
    return true;
}

static void keypad_pin_button_handler(void* handler_arg, esp_event_base_t base, int32_t id, void* event_data)
{
    pin_insert_t* pin_insert = (pin_insert_t*)handler_arg;
    if (!pin_insert) {
        return;
    }

    bool updated = false;
    if (id > BTN_KEYBOARD_ASCII_OFFSET) {
        const size_t digit = id - BTN_KEYBOARD_ASCII_OFFSET;
        if (pin_insert->selected_digit < PIN_SIZE) {
            pin_insert->pin[pin_insert->selected_digit] = digit;
            pin_insert->digit_status[pin_insert->selected_digit] = SET;
            pin_insert->selected_digit++;
            updated = true;
        }
    } else if (id == BTN_KEYBOARD_BACKSPACE) {
        if (!keypad_backspace(pin_insert)) {
            // Pressed backspace on empty pin - signal abandon
            esp_event_post(GUI_EVENT, BTN_BACK, NULL, 0, 50 / portTICK_PERIOD_MS);
            return;
        }
        updated = true;
    } else if (id == BTN_KEYBOARD_ENTER) {
        if ((pin_insert->pin_digits_shown && pin_insert->selected_digit > 0)
            || (!pin_insert->pin_digits_shown && pin_insert->selected_digit == PIN_SIZE)) {
            esp_event_post(GUI_EVENT, GUI_FRONT_CLICK_EVENT, NULL, 0, 50 / portTICK_PERIOD_MS);
        }
    }

    if (updated) {
        update_pin_text_display(pin_insert);
    }
}

void make_keypad_pin_insert_activity(pin_insert_t* pin_insert, const char* title, const char* message)
{
    JADE_ASSERT(pin_insert);
    JADE_ASSERT(title);

    pin_insert->activity = gui_make_activity();
    gui_view_node_t* parent = add_title_bar(pin_insert->activity, title, NULL, 0, &pin_insert->title);
    gui_view_node_t* node;

    gui_view_node_t* vsplit;

    if (message) {
        gui_make_vsplit(&vsplit, GUI_SPLIT_RELATIVE, 3, 20, 15, 65); // msg, pin, keypad
        gui_set_parent(vsplit, parent);

        gui_make_text(&node, message, TFT_WHITE);
        gui_set_align(node, GUI_ALIGN_CENTER, GUI_ALIGN_MIDDLE);
        gui_set_parent(node, vsplit);
    } else {
        gui_make_vsplit(&vsplit, GUI_SPLIT_RELATIVE, 2, 20, 80); // pin, keypad
        gui_set_parent(vsplit, parent);
    }

    gui_make_text_font(&pin_insert->pin_text_node, "", TFT_WHITE, DEJAVU24_FONT);
    gui_set_align(pin_insert->pin_text_node, GUI_ALIGN_CENTER, GUI_ALIGN_MIDDLE);
    gui_set_parent(pin_insert->pin_text_node, vsplit);

    gui_view_node_t* keypad_grid;
    gui_make_vsplit(&keypad_grid, GUI_SPLIT_RELATIVE, 4, 25, 25, 25, 25);
    gui_set_parent(keypad_grid, vsplit);

    gui_view_node_t* btn;
    gui_view_node_t* label_node;

    // Row 1: 1, 2, 3
    gui_view_node_t* row1;
    gui_make_hsplit(&row1, GUI_SPLIT_RELATIVE, 3, 33, 34, 33);
    gui_set_parent(row1, keypad_grid);

    gui_make_button(&btn, TFT_BLACK, gui_get_highlight_color(), BTN_KEYBOARD_ASCII_OFFSET + 1, NULL);
    gui_set_parent(btn, row1);
    gui_make_text_font(&label_node, "1", TFT_WHITE, DEJAVU24_FONT);
    gui_set_align(label_node, GUI_ALIGN_CENTER, GUI_ALIGN_MIDDLE);
    gui_set_parent(label_node, btn);

    gui_make_button(&btn, TFT_BLACK, gui_get_highlight_color(), BTN_KEYBOARD_ASCII_OFFSET + 2, NULL);
    gui_set_parent(btn, row1);
    gui_make_text_font(&label_node, "2", TFT_WHITE, DEJAVU24_FONT);
    gui_set_align(label_node, GUI_ALIGN_CENTER, GUI_ALIGN_MIDDLE);
    gui_set_parent(label_node, btn);

    gui_make_button(&btn, TFT_BLACK, gui_get_highlight_color(), BTN_KEYBOARD_ASCII_OFFSET + 3, NULL);
    gui_set_parent(btn, row1);
    gui_make_text_font(&label_node, "3", TFT_WHITE, DEJAVU24_FONT);
    gui_set_align(label_node, GUI_ALIGN_CENTER, GUI_ALIGN_MIDDLE);
    gui_set_parent(label_node, btn);

    // Row 2: 4, 5, 6
    gui_view_node_t* row2;
    gui_make_hsplit(&row2, GUI_SPLIT_RELATIVE, 3, 33, 34, 33);
    gui_set_parent(row2, keypad_grid);

    gui_make_button(&btn, TFT_BLACK, gui_get_highlight_color(), BTN_KEYBOARD_ASCII_OFFSET + 4, NULL);
    gui_set_parent(btn, row2);
    gui_make_text_font(&label_node, "4", TFT_WHITE, DEJAVU24_FONT);
    gui_set_align(label_node, GUI_ALIGN_CENTER, GUI_ALIGN_MIDDLE);
    gui_set_parent(label_node, btn);

    gui_make_button(&btn, TFT_BLACK, gui_get_highlight_color(), BTN_KEYBOARD_ASCII_OFFSET + 5, NULL);
    gui_set_parent(btn, row2);
    gui_make_text_font(&label_node, "5", TFT_WHITE, DEJAVU24_FONT);
    gui_set_align(label_node, GUI_ALIGN_CENTER, GUI_ALIGN_MIDDLE);
    gui_set_parent(label_node, btn);

    gui_make_button(&btn, TFT_BLACK, gui_get_highlight_color(), BTN_KEYBOARD_ASCII_OFFSET + 6, NULL);
    gui_set_parent(btn, row2);
    gui_make_text_font(&label_node, "6", TFT_WHITE, DEJAVU24_FONT);
    gui_set_align(label_node, GUI_ALIGN_CENTER, GUI_ALIGN_MIDDLE);
    gui_set_parent(label_node, btn);

    // Row 3: 7, 8, 9
    gui_view_node_t* row3;
    gui_make_hsplit(&row3, GUI_SPLIT_RELATIVE, 3, 33, 34, 33);
    gui_set_parent(row3, keypad_grid);

    gui_make_button(&btn, TFT_BLACK, gui_get_highlight_color(), BTN_KEYBOARD_ASCII_OFFSET + 7, NULL);
    gui_set_parent(btn, row3);
    gui_make_text_font(&label_node, "7", TFT_WHITE, DEJAVU24_FONT);
    gui_set_align(label_node, GUI_ALIGN_CENTER, GUI_ALIGN_MIDDLE);
    gui_set_parent(label_node, btn);

    gui_make_button(&btn, TFT_BLACK, gui_get_highlight_color(), BTN_KEYBOARD_ASCII_OFFSET + 8, NULL);
    gui_set_parent(btn, row3);
    gui_make_text_font(&label_node, "8", TFT_WHITE, DEJAVU24_FONT);
    gui_set_align(label_node, GUI_ALIGN_CENTER, GUI_ALIGN_MIDDLE);
    gui_set_parent(label_node, btn);

    gui_make_button(&btn, TFT_BLACK, gui_get_highlight_color(), BTN_KEYBOARD_ASCII_OFFSET + 9, NULL);
    gui_set_parent(btn, row3);
    gui_make_text_font(&label_node, "9", TFT_WHITE, DEJAVU24_FONT);
    gui_set_align(label_node, GUI_ALIGN_CENTER, GUI_ALIGN_MIDDLE);
    gui_set_parent(label_node, btn);

    // Row 4: confirm, 0, backspace
    gui_view_node_t* row4;
    gui_make_hsplit(&row4, GUI_SPLIT_RELATIVE, 3, 33, 34, 33);
    gui_set_parent(row4, keypad_grid);

    gui_make_button(&btn, TFT_BLACK, gui_get_highlight_color(), BTN_KEYBOARD_ENTER, NULL);
    gui_set_parent(btn, row4);
    gui_make_text_font(&label_node, "S", TFT_WHITE, VARIOUS_SYMBOLS_FONT);
    gui_set_align(label_node, GUI_ALIGN_CENTER, GUI_ALIGN_MIDDLE);
    gui_set_parent(label_node, btn);

    gui_make_button(&btn, TFT_BLACK, gui_get_highlight_color(), BTN_KEYBOARD_ASCII_OFFSET + 0, NULL);
    gui_set_parent(btn, row4);
    gui_make_text_font(&label_node, "0", TFT_WHITE, DEJAVU24_FONT);
    gui_set_align(label_node, GUI_ALIGN_CENTER, GUI_ALIGN_MIDDLE);
    gui_set_parent(label_node, btn);

    gui_make_button(&btn, TFT_BLACK, gui_get_highlight_color(), BTN_KEYBOARD_BACKSPACE, NULL);
    gui_set_parent(btn, row4);
    gui_make_text_font(&label_node, "|", TFT_WHITE, DEFAULT_FONT);
    gui_set_align(label_node, GUI_ALIGN_CENTER, GUI_ALIGN_MIDDLE);
    gui_set_parent(label_node, btn);

    gui_activity_register_event(pin_insert->activity, GUI_BUTTON_EVENT, BTN_KEYBOARD_ENTER, keypad_pin_button_handler, pin_insert);
    gui_activity_register_event(pin_insert->activity, GUI_BUTTON_EVENT, BTN_KEYBOARD_BACKSPACE, keypad_pin_button_handler, pin_insert);
    for (size_t i = 0; i <= 9; ++i) {
        gui_activity_register_event(pin_insert->activity, GUI_BUTTON_EVENT, BTN_KEYBOARD_ASCII_OFFSET + i, keypad_pin_button_handler, pin_insert);
    }

    // Initialise PIN state and display
    reset_pin(pin_insert, NULL);
}



static void pin_digit_button_handler(void* handler_arg, esp_event_base_t base, int32_t id, void* event_data)
{
    pin_insert_t* pin_insert = (pin_insert_t*)handler_arg;
    if (!pin_insert) {
        return;
    }

    switch (id) {
    case BTN_PIN_DIGIT_UP:
        esp_event_post(GUI_EVENT, GUI_WHEEL_RIGHT_EVENT, NULL, 0, 50 / portTICK_PERIOD_MS);
        break;
    case BTN_PIN_DIGIT_DOWN:
        esp_event_post(GUI_EVENT, GUI_WHEEL_LEFT_EVENT, NULL, 0, 50 / portTICK_PERIOD_MS);
        break;
    case BTN_PIN_DIGIT_SELECT:
        esp_event_post(GUI_EVENT, GUI_FRONT_CLICK_EVENT, NULL, 0, 50 / portTICK_PERIOD_MS);
        break;
    default:
        break;
    }
}


static bool next_selected_digit(pin_insert_t* pin_insert)
{
    JADE_ASSERT(pin_insert);
    JADE_ASSERT(pin_insert->selected_digit < PIN_SIZE);

    // make sure the '<' is not selected
    JADE_ASSERT(pin_insert->current_selected_value < 10);

    // copy the value
    pin_insert->pin[pin_insert->selected_digit] = pin_insert->current_selected_value;

    // set the status and update the ui
    pin_insert->digit_status[pin_insert->selected_digit] = SET;
    update_digit_node(pin_insert, pin_insert->selected_digit);
    ++pin_insert->selected_digit;

    // reached the last digit - cannot select next, return false
    if (pin_insert->selected_digit >= PIN_SIZE) {
        return false;
    }

    // set the status and update the ui
    pin_insert->digit_status[pin_insert->selected_digit] = SELECTED;

    reinitialise_current_pin_digit(pin_insert);
    update_digit_node(pin_insert, pin_insert->selected_digit);

    return true;
}

static bool prev_selected_digit(pin_insert_t* pin_insert)
{
    JADE_ASSERT(pin_insert);
    JADE_ASSERT(pin_insert->selected_digit < PIN_SIZE);

    // at the first digit - cannot select previous, return false
    if (pin_insert->selected_digit == 0) {
        return false;
    }

    // set the status and update the ui
    pin_insert->digit_status[pin_insert->selected_digit] = EMPTY;
    update_digit_node(pin_insert, pin_insert->selected_digit);

    --pin_insert->selected_digit;
    reinitialise_current_pin_digit(pin_insert);

    // set the status and update the ui
    pin_insert->digit_status[pin_insert->selected_digit] = SELECTED;
    update_digit_node(pin_insert, pin_insert->selected_digit);

    return true;
}

// Returns true if pin entry completes and pin_insert->pin is valid,
// and false if pin entry abandoned and pin_insert->pin is not to be used.
bool run_keypad_pin_entry_loop(pin_insert_t* pin_insert)
{
    JADE_ASSERT(pin_insert);
    JADE_ASSERT(pin_insert->activity);

    int32_t ev_id;
    while (true) {
        // wait for a GUI event
        gui_activity_wait_event(pin_insert->activity, GUI_EVENT, ESP_EVENT_ANY_ID, NULL, &ev_id, NULL, 0);

        if (ev_id == gui_get_click_event()) {
            if (pin_insert->pin_digits_shown) {
                if (pin_insert->selected_digit > 0) return true;
            } else {
                JADE_ASSERT(pin_insert->selected_digit == PIN_SIZE);
                return true;
            }
        } else if (ev_id == BTN_BACK) {
            if (!keypad_backspace(pin_insert)) {
                // No digits to delete - treat as cancel
                return false;
            }
            update_pin_text_display(pin_insert);
        }
    }
}



void reset_pin(pin_insert_t* pin_insert, const char* title)
{
    JADE_ASSERT(pin_insert);
    // title is optional

    // Select and re-randomise first digit
    pin_insert->selected_digit = 0;
    reinitialise_current_pin_digit(pin_insert);

    // Mark all digits as unset
    for (size_t i = 0; i < PIN_SIZE; ++i) {
        pin_insert->pin[i] = 0xFF;
        // In keypad mode we don't use per-digit nodes, so leave all EMPTY
        pin_insert->digit_status[i] = EMPTY;
        if (!pin_insert->pin_text_node) {
            update_digit_node(pin_insert, i);
        }
    }

    // Update title if passed
    if (title) {
        gui_update_text(pin_insert->title, title);
    }
}

size_t get_pin_as_number(const pin_insert_t* pin_insert)
{
    JADE_ASSERT(pin_insert);

    size_t val = 0;
    const uint8_t ndigs = pin_insert->pin_digits_shown ? pin_insert->selected_digit : PIN_SIZE;
    for (uint8_t i = 0; i < ndigs; ++i) {
        JADE_ASSERT(pin_insert->digit_status[i] == SET);
        JADE_ASSERT(pin_insert->pin[i] < NUM_PIN_VALUES);

        const size_t digit = pin_insert->pin[i];
        const uint8_t exponent = ndigs - i - 1;
        val += (digit * pow(10, exponent));
    }

    return val;
}
#endif // AMALGAMATED_BUILD
