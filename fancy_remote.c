#include <furi.h>

#include <gui/view_dispatcher.h>

#include <dialogs/dialogs.h>

/* generated by fbt from .png files in images folder */
#include <fancy_remote_icons.h>

//my custom button panel to allow type in callback
#include <extensions/upgraded_button_panel.h>

#include <flipper_format_i.h>

#include <infrared_worker.h>

#include <notification/notification_messages.h>
typedef enum {
    Scene_RemotePanel,
    Scene_count
} Scene;

typedef enum {
    FView_UpgradedButtonPanel
} FView;

typedef enum {
    Button_VolumeUp,
    Button_VolumeDown,
    Button_NavigateLeft,
    Button_NavigateUp,
    Button_NavigateRight,
    Button_NavigateDown,
    Button_Power,
    Button_Confirm
} Button;
const char* buttonNames[] = {
    "Volume_up",
    "Volume_down",
    "Navigate_left",
    "Navigate_up",
    "Navigate_right",
    "Navigate_down",
    "Power",
    "Confirm"};
typedef struct {
    uint32_t frequency;
    float duty_cycle;
    uint32_t* data;
    uint32_t size;
} RawSignal;

typedef struct {
    bool isRaw;
    InfraredMessage message;
    RawSignal raw;
} Signal;

//define an array so they would be mapped
typedef struct {
    SceneManager* scene_manager;
    ViewDispatcher* view_dispatcher;
    UpgradedButtonPanel* buttonPanel;
    InfraredWorker* worker;
    Signal* current;
    NotificationApp* notify;
    DialogsApp* dialogs;
    FuriString* path;
    int currentIndex;
} FancyRemote;

typedef enum {
    Event_ShowRemotePanel,
} Event;

void clearRawData(Signal* signal) {
    if(signal->isRaw) {
        free(signal->raw.data);
        signal->raw.size = 0;
        signal->raw.data = NULL;
    }
}
bool makeParsedBody(Signal* signal, FlipperFormat* ff) {
    FuriString* tmp = furi_string_alloc();
    bool out = true;
    if(!flipper_format_read_string(ff, "protocol", tmp)) {
        out = false;
    }
    InfraredMessage message;
    message.protocol = infrared_get_protocol_by_name(furi_string_get_cstr(tmp));
    furi_string_free(tmp);

    if(!flipper_format_read_hex(ff, "address", (uint8_t*)&message.address, 4)) {
        out = false;
    }
    if(!flipper_format_read_hex(ff, "command", (uint8_t*)&message.command, 4)) {
        out = false;
    }
    message.repeat = true;

    clearRawData(signal);
    signal->isRaw = false;
    signal->message = *&message;

    return out;
}

bool makeRawBody(Signal* signal, FlipperFormat* ff) {
    uint32_t frequency;
    if(!flipper_format_read_uint32(ff, "frequency", &frequency, 1)) {
        return false;
    }
    float duty_cycle;
    if(!flipper_format_read_float(ff, "duty_cycle", &duty_cycle, 1)) {
        return false;
    }
    uint32_t size;
    if(!flipper_format_get_value_count(ff, "data", &size)) {
        return false;
    }
    if(size > 1024) {
        return false;
    }
    uint32_t* data = malloc(sizeof(uint32_t) * size);
    if(!flipper_format_read_uint32(ff, "data", data, size)) {
        free(data);
        return false;
    }
    clearRawData(signal);
    signal->isRaw = true;
    signal->raw.size = size;
    signal->raw.frequency = frequency;
    signal->raw.duty_cycle = duty_cycle;
    signal->raw.data = malloc(sizeof(uint32_t) * size);
    memcpy(signal->raw.data, data, sizeof(uint32_t) * size);
    free(data);
    return true;
}
bool makeBody(Signal* signal, FlipperFormat* ff) {
    FuriString* tmp = furi_string_alloc();
    if(!flipper_format_read_string(ff, "type", tmp)) {
        furi_string_free(tmp);
        return false;
    }
    bool out = false;
    if(furi_string_equal_str(tmp, "parsed")) {
        out = makeParsedBody(signal, ff);
    } else if(furi_string_equal_str(tmp, "raw")) {
        out = makeRawBody(signal, ff);
    }
    furi_string_free(tmp);
    return out;
}
bool makeSignal(void* context, Signal* signal, int index) {
    FancyRemote* app = context;
    Storage* storage = furi_record_open(RECORD_STORAGE);
    FlipperFormat* ff = flipper_format_buffered_file_alloc(storage);
    flipper_format_buffered_file_open_existing(ff, furi_string_get_cstr(app->path));
    FuriString* tmp = furi_string_alloc();
    while(flipper_format_read_string(ff, "name", tmp)) {
        if(furi_string_equal_str(tmp, buttonNames[index])) {
            break;
        }
    }
    furi_string_free(tmp);
    bool out = makeBody(signal, ff);
    if(out) {
        app->currentIndex = index;
    }
    flipper_format_buffered_file_close(ff);
    flipper_format_free(ff);
    furi_record_close(RECORD_STORAGE);
    return out;
}
void sendIrSignal(void* context, uint32_t index, InputType type) {
    FancyRemote* app = context;
    if(type == InputTypePress) {
        if(makeSignal(context, app->current, index)) {
            infrared_worker_tx_set_get_signal_callback(
                app->worker, infrared_worker_tx_get_signal_steady_callback, context);
            bool test = app->current->isRaw;
            if(test) {
                infrared_worker_set_raw_signal(
                    app->worker,
                    app->current->raw.data,
                    app->current->raw.size,
                    app->current->raw.frequency,
                    app->current->raw.duty_cycle);

            } else {
                const InfraredMessage message = app->current->message;
                infrared_worker_set_decoded_signal(app->worker, &message);
            }
            notification_message(app->notify, &sequence_blink_start_magenta);
            infrared_worker_tx_start(app->worker);
        }
    } else if(type == InputTypeRelease) {
        notification_message(app->notify, &sequence_blink_stop);
        infrared_worker_tx_stop(app->worker);
    }
}
//the code to open remotePanel
//scene_manager_next_scene(app->scene_manager, Scene_RemotePanel);

bool fancy_remote_scene_manager_navigation_event_callback(void* context) {
    FancyRemote* app = context;
    return scene_manager_handle_back_event(app->scene_manager);
}

void addVolumeButtons(void* context, int Vx, int Vy, int x, int y) {
    /*
    in origional  volume up is at (38,53), and volume down is at (38,91)
    so relative measurments are: volUp:(+3,+0),volDown(+3,+20)
    */
    FancyRemote* app = context;
    upgraded_button_panel_add_item(
        app->buttonPanel,
        Button_VolumeUp,
        Vx,
        Vy,
        x + 3,
        y,
        &I_volup_24x21,
        &I_volup_hover_24x21,
        *sendIrSignal,
        context);
    upgraded_button_panel_add_item(
        app->buttonPanel,
        Button_VolumeDown,
        Vx,
        Vy + 1,
        x + 3,
        y + 22,
        &I_voldown_24x21,
        &I_voldown_hover_24x21,
        *sendIrSignal,
        context);
}
void addPowerButton(void* context, int Vx, int Vy, int x, int y) {
    /*in origional the button is at (6,16) and the text is at (4,38)
    so relative is: button (+2, +0) and text (+0, +22)*/
    FancyRemote* app = context;
    upgraded_button_panel_add_item(
        app->buttonPanel,
        Button_Power,
        Vx,
        Vy,
        x + 2,
        y,
        &I_power_19x20,
        &I_power_hover_19x20,
        *sendIrSignal,
        context);
}
void addNavigation(void* context, int Vx, int Vy, int x, int y) {
    FancyRemote* app = context;
    /*each button is either 24*18 or 18*24 with the center being 24*24
    define (+0,+0) as the furthest up and to the left of all
    navup (+18,+0), navleft (+0,+18), navdown (+18,+42), navright (+42,+18),center (+18,+18);
    for virtual grid define (+0,+0) as the center
    navup(+0,-1),navleft(-1,+0),navdown(+0,+1),navright(+1,+0),center(+0,+0);
    */
    upgraded_button_panel_add_item(
        app->buttonPanel,
        Button_NavigateUp,
        Vx,
        Vy - 1,
        x + 18,
        y,
        &I_navup_24x18,
        &I_navup_hover_24x18,
        *sendIrSignal,
        context);
    upgraded_button_panel_add_item(
        app->buttonPanel,
        Button_NavigateLeft,
        Vx - 1,
        Vy,
        x,
        y + 18,
        &I_navleft_18x24,
        &I_navleft_hover_18x24,
        *sendIrSignal,
        context);
    upgraded_button_panel_add_item(
        app->buttonPanel,
        Button_NavigateDown,
        Vx,
        Vy + 1,
        x + 18,
        y + 42,
        &I_navdown_24x18,
        &I_navdown_hover_24x18,
        *sendIrSignal,
        context);
    upgraded_button_panel_add_item(
        app->buttonPanel,
        Button_NavigateRight,
        Vx + 1,
        Vy,
        x + 42,
        y + 18,
        &I_navright_18x24,
        &I_navright_hover_18x24,
        *sendIrSignal,
        context);
    upgraded_button_panel_add_item(
        app->buttonPanel,
        Button_Confirm,
        Vx,
        Vy,
        x + 18,
        y + 18,
        &I_navok_24x24,
        &I_navok_hover_24x24,
        *sendIrSignal,
        context);
}
void fancy_remote_scene_on_enter_RemotePanel(void* context) {
    FancyRemote* app = context;
    upgraded_button_panel_reset(app->buttonPanel);
    upgraded_button_panel_reserve(app->buttonPanel, 3, 6);
    /*using 69 puts the bottom of the volume down button at the bottom of the screen
    (screen height of 128, block height of 59, 128-59=69) */
    addPowerButton(context, 1, 0, 20, 0);
    addNavigation(context, 1, 2, 2, 23);
    addVolumeButtons(context, 1, 4, 16, 85);

    view_dispatcher_switch_to_view(app->view_dispatcher, FView_UpgradedButtonPanel);
}

bool fancy_remote_scene_on_event_RemotePanel() {
    return false;
}

void fancy_remote_scene_on_exit_RemotePanel(void* context) {
    FancyRemote* app = context;
    upgraded_button_panel_reset(app->buttonPanel);
}
/*on enter handlers(being declared before use)*/
void (*const fancy_remote_scene_on_enter_handlers[])(void*) = {
    fancy_remote_scene_on_enter_RemotePanel};

bool (*const fancy_remote_scene_on_event_handlers[])(void*, SceneManagerEvent) = {
    fancy_remote_scene_on_event_RemotePanel};
void (*const fancy_remote_scene_on_exit_handlers[])(void*) = {
    fancy_remote_scene_on_exit_RemotePanel};
//bringing it all together is this thing
const SceneManagerHandlers fancy_remote_scene_event_handlers = {
    .on_enter_handlers = fancy_remote_scene_on_enter_handlers,
    .on_event_handlers = fancy_remote_scene_on_event_handlers,
    .on_exit_handlers = fancy_remote_scene_on_exit_handlers,
    .scene_num = Scene_count};

void fancy_remote_scene_manager_init(FancyRemote* app) {
    app->scene_manager = scene_manager_alloc(&fancy_remote_scene_event_handlers, app);
}
bool fancy_remote_scene_manager_custom_event_callback(void* context, uint32_t custom_event) {
    FancyRemote* app = context;
    return scene_manager_handle_custom_event(app->scene_manager, custom_event);
}
void fancy_remote_view_dispatcher_init(FancyRemote* app) {
    app->view_dispatcher = view_dispatcher_alloc();
    app->buttonPanel = upgraded_button_panel_alloc();
    app->currentIndex = -1;

    view_dispatcher_set_event_callback_context(app->view_dispatcher, app);
    view_dispatcher_set_custom_event_callback(
        app->view_dispatcher, fancy_remote_scene_manager_custom_event_callback);

    view_dispatcher_set_navigation_event_callback(
        app->view_dispatcher, fancy_remote_scene_manager_navigation_event_callback);
    view_dispatcher_add_view(
        app->view_dispatcher,
        FView_UpgradedButtonPanel,
        upgraded_button_panel_get_view(app->buttonPanel));
}

FancyRemote* fancy_remote_init() {
    FancyRemote* app = malloc(sizeof(FancyRemote));
    app->worker = infrared_worker_alloc();
    app->current = malloc(sizeof(Signal));
    app->notify = furi_record_open(RECORD_NOTIFICATION);
    app->path = furi_string_alloc_set(EXT_PATH("infrared"));
    app->dialogs = furi_record_open(RECORD_DIALOGS);
    fancy_remote_scene_manager_init(app);
    fancy_remote_view_dispatcher_init(app);
    return app;
}
//frees all data when done
void fancy_remote_free(FancyRemote* app) {
    furi_record_close(RECORD_NOTIFICATION);
    app->notify = NULL;
    furi_string_free(app->path);
    furi_record_close(RECORD_DIALOGS);
    infrared_worker_free(app->worker);
    scene_manager_free(app->scene_manager);
    view_dispatcher_remove_view(app->view_dispatcher, FView_UpgradedButtonPanel);
    view_dispatcher_free(app->view_dispatcher);
    upgraded_button_panel_free(app->buttonPanel);
    free(app);
}
void fancy_remote_select_and_run(FancyRemote* app) {
    DialogsFileBrowserOptions browser_options;
    dialog_file_browser_set_basic_options(&browser_options, ".ir", &I_ir_10px);
    browser_options.base_path = EXT_PATH("infrared");
    const bool done =
        dialog_file_browser_show(app->dialogs, app->path, app->path, &browser_options);
    if(done) {
        view_dispatcher_run(app->view_dispatcher);
    }
}

int32_t fancy_remote_app() {
    FancyRemote* app = fancy_remote_init();
    Gui* gui = furi_record_open(RECORD_GUI);
    view_dispatcher_attach_to_gui(app->view_dispatcher, gui, ViewDispatcherTypeFullscreen);
    scene_manager_next_scene(app->scene_manager, Scene_RemotePanel);
    fancy_remote_select_and_run(app);
    //this is the main loop
    //view_dispatcher_run(app->view_dispatcher);
    //this runs after program ends
    fancy_remote_free(app);
    return 0;
}
