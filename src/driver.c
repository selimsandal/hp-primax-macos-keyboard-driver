#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <stdbool.h>
#include <pthread.h>
#include <string.h>
#include <Carbon/Carbon.h>
#include <ApplicationServices/ApplicationServices.h>
#include <mach/mach_time.h>
#include <stdint.h>
#include "hidapi.h"

#define VENDOR_ID 0x0461 // primax
#define PRODUCT_ID 0x4e90
#define BUF_SIZE 3
#define MAX_KEYS 256
#define SPIN_DURATION 50000 // 50 microseconds

__attribute__((aligned(64))) const static int keys[] = {
        kVK_ANSI_A,
        kVK_ANSI_B,
        kVK_ANSI_C,
        kVK_ANSI_D,
        kVK_ANSI_E,
        kVK_ANSI_F,
        kVK_ANSI_G,
        kVK_ANSI_H,
        kVK_ANSI_I,
        kVK_ANSI_J,
        kVK_ANSI_K,
        kVK_ANSI_L,
        kVK_ANSI_M,
        kVK_ANSI_N,
        kVK_ANSI_O,
        kVK_ANSI_P,
        kVK_ANSI_Q,
        kVK_ANSI_R,
        kVK_ANSI_S,
        kVK_ANSI_T,
        kVK_ANSI_U,
        kVK_ANSI_V,
        kVK_ANSI_W,
        kVK_ANSI_X,
        kVK_ANSI_Y,
        kVK_ANSI_Z,
        kVK_ANSI_1,
        kVK_ANSI_2,
        kVK_ANSI_3,
        kVK_ANSI_4,
        kVK_ANSI_5,
        kVK_ANSI_6,
        kVK_ANSI_7,
        kVK_ANSI_8,
        kVK_ANSI_9,
        kVK_ANSI_0,
        kVK_Return,
        kVK_Escape,
        kVK_Delete,
        kVK_Tab,
        kVK_Space,
        kVK_ANSI_Minus,
        kVK_ANSI_Equal,
        kVK_ANSI_LeftBracket,
        kVK_ANSI_RightBracket,
        -1, // empty
        kVK_ANSI_Backslash,
        kVK_ANSI_Semicolon,
        kVK_ANSI_Quote,
        kVK_ISO_Section, // 102ND
        kVK_ANSI_Comma,
        kVK_ANSI_Period,
        kVK_ANSI_Slash,
        kVK_CapsLock,
        kVK_F1,
        kVK_F2,
        kVK_F3,
        kVK_F4,
        kVK_F5,
        kVK_F6,
        kVK_F7,
        kVK_F8,
        kVK_F9,
        kVK_F10,
        kVK_F11,
        kVK_F12,
        -1, // print screen
        -1, // scroll lock
        -1, // pause
        kVK_Help, // insert/help
        kVK_Home,
        kVK_PageUp,
        kVK_ForwardDelete,
        kVK_End,
        kVK_PageDown,
        kVK_RightArrow,
        kVK_LeftArrow,
        kVK_DownArrow,
        kVK_UpArrow,
        kVK_ANSI_KeypadClear, // num lock
        kVK_ANSI_KeypadDivide,
        kVK_ANSI_KeypadMultiply,
        kVK_ANSI_KeypadMinus,
        kVK_ANSI_KeypadPlus,
        kVK_ANSI_KeypadEnter,
        kVK_ANSI_Keypad1,
        kVK_ANSI_Keypad2,
        kVK_ANSI_Keypad3,
        kVK_ANSI_Keypad4,
        kVK_ANSI_Keypad5,
        kVK_ANSI_Keypad6,
        kVK_ANSI_Keypad7,
        kVK_ANSI_Keypad8,
        kVK_ANSI_Keypad9,
        kVK_ANSI_Keypad0,
        kVK_ANSI_KeypadDecimal,
        kVK_ANSI_Grave // < >
};

static volatile sig_atomic_t run = 1;
static uint64_t key_states[4] = {0};
static unsigned char last_modifiers = 0;
static bool debug_mode = false;

#define DEBUG_PRINT(fmt, ...) \
    do { if (debug_mode) fprintf(stderr, fmt, ##__VA_ARGS__); } while (0)

void sig_int_handle(int signum) {
    run = 0;
}

__attribute__((always_inline)) inline void post_keyboard_event(CGKeyCode keyCode, bool keyDown, CGEventFlags flags) {
    CGEventRef event = CGEventCreateKeyboardEvent(NULL, keyCode, keyDown);
    CGEventSetFlags(event, flags);
    CGEventPost(kCGHIDEventTap, event);
    CFRelease(event);
}

__attribute__((always_inline)) inline CGEventFlags get_modifier_flags(unsigned char modifiers) {
    return ((modifiers & 0x01) ? kCGEventFlagMaskControl : 0) |
           ((modifiers & 0x02) ? kCGEventFlagMaskShift : 0) |
           ((modifiers & 0x04) ? kCGEventFlagMaskCommand : 0) |
           ((modifiers & 0x08) ? kCGEventFlagMaskAlternate : 0);
}

__attribute__((always_inline)) inline void handle_modifier_changes(unsigned char new_modifiers) {
    unsigned char changed_modifiers = last_modifiers ^ new_modifiers;

    if (changed_modifiers & 0x01) post_keyboard_event(kVK_Control, new_modifiers & 0x01, get_modifier_flags(new_modifiers));
    if (changed_modifiers & 0x02) post_keyboard_event(kVK_Shift, new_modifiers & 0x02, get_modifier_flags(new_modifiers));
    if (changed_modifiers & 0x04) post_keyboard_event(kVK_Command, new_modifiers & 0x04, get_modifier_flags(new_modifiers));
    if (changed_modifiers & 0x08) post_keyboard_event(kVK_Option, new_modifiers & 0x08, get_modifier_flags(new_modifiers));

    last_modifiers = new_modifiers;
}

__attribute__((always_inline)) inline void handle_key_event(unsigned char modifiers, unsigned char key, bool keyDown) {
    if (key == 0 || key > MAX_KEYS) return;

    int key_code = keys[key - 4];
    if (key_code == -1) {
        DEBUG_PRINT("Unmapped key: %d\n", key);
        return;
    }

    CGEventFlags flags = get_modifier_flags(modifiers);

    uint64_t mask = 1ULL << (key % 64);
    uint64_t *state = &key_states[key / 64];
    bool current_state = (*state & mask) != 0;

    if (keyDown != current_state) {
        post_keyboard_event(key_code, keyDown, flags);
        *state ^= mask;  // Toggle the bit
        DEBUG_PRINT("Key event: %d (mapped to %d), %s\n", key, key_code, keyDown ? "pressed" : "released");
    }
}

void *read_input(void *arg) {
    hid_device *handle = (hid_device *)arg;
    unsigned char buf[BUF_SIZE] = {0};
    unsigned char last_keys[MAX_KEYS] = {0};
    uint64_t start, end, elapsed;
    mach_timebase_info_data_t timebase;
    mach_timebase_info(&timebase);

    while (run) {
        start = mach_absolute_time();
        int res = hid_read(handle, buf, sizeof(buf));
        
        if (res > 0) {
            unsigned char modifiers = buf[0];
            unsigned char keys_pressed = buf[2];

            DEBUG_PRINT("Raw input: modifiers=0x%02X, key=0x%02X\n", modifiers, keys_pressed);

            handle_modifier_changes(modifiers);

            for (int i = 0; i < MAX_KEYS; i++) {
                if (last_keys[i] && last_keys[i] != keys_pressed) {
                    handle_key_event(modifiers, last_keys[i], false);
                }
            }

            if (keys_pressed) {
                handle_key_event(modifiers, keys_pressed, true);
            }

            last_keys[keys_pressed] = keys_pressed;
        }

        // Spin-wait for the remaining time
        do {
            end = mach_absolute_time();
            elapsed = end - start;
        } while (elapsed * timebase.numer / timebase.denom < SPIN_DURATION);
    }

    return NULL;
}

void print_usage(const char* program_name) {
    printf("Usage: %s [--debug]\n", program_name);
    printf("  --debug    Enable debug output\n");
}

int main(int argc, char *argv[]) {
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--debug") == 0) {
            debug_mode = true;
        } else {
            print_usage(argv[0]);
            return 1;
        }
    }

    signal(SIGINT, sig_int_handle);

    if (hid_init() != 0) {
        fprintf(stderr, "Failed to initialize HIDAPI\n");
        return 1;
    }

    hid_device *handle = hid_open(VENDOR_ID, PRODUCT_ID, NULL);
    if (!handle) {
        fprintf(stderr, "Failed to open HID device\n");
        hid_exit();
        return 1;
    }

    hid_set_nonblocking(handle, 1);

    printf("Keyboard driver started. Press Ctrl+C to exit.\n");
    if (debug_mode) {
        printf("Debug mode is ON\n");
    }

    pthread_t input_thread;
    if (pthread_create(&input_thread, NULL, read_input, handle) != 0) {
        fprintf(stderr, "Failed to create input thread\n");
        hid_close(handle);
        hid_exit();
        return 1;
    }

    pthread_join(input_thread, NULL);

    for (int i = 0; i < MAX_KEYS; i++) {
        if (key_states[i / 64] & (1ULL << (i % 64))) {
            handle_key_event(0, i, false);
        }
    }

    handle_modifier_changes(0);

    hid_close(handle);
    hid_exit();
    printf("Keyboard driver stopped.\n");
    return 0;
}