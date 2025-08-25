/*
 * Based on Obdev's AVRUSB code and under the same license.
 * Modified for Digispark by Digistump
 */
#ifndef __DigiKeyboard_h__
#define __DigiKeyboard_h__

#include <Arduino.h>
#include <avr/pgmspace.h>
#include <avr/interrupt.h>
#include <util/delay.h>
#include <string.h>

#include "usbdrv.h"
#include "keylayouts.h"

typedef uint8_t byte;

#define TEST_STRING "abcdefghijklmnopqrstuvwxyz ABCDEFGHIJKLMNOPQRSTUVWXYZ 1234567890 !\"#$%&'()*+,-./:;<=>?@[\\]^_`{|}~"

static uchar idleRate;           // in 4 ms units

/* Boot-protocol compliant keyboard report descriptor:
 *  - 1 byte modifiers
 *  - 1 byte reserved
 *  - 6 keycodes
 *  - optional LED output report (we ignore in firmware)
 */
const PROGMEM uchar usbHidReportDescriptor[USB_CFG_HID_REPORT_DESCRIPTOR_LENGTH] = {
    0x05, 0x01,        // USAGE_PAGE (Generic Desktop)
    0x09, 0x06,        // USAGE (Keyboard)
    0xa1, 0x01,        // COLLECTION (Application)
    0x05, 0x07,        //   USAGE_PAGE (Keyboard/Keypad)

    // Modifiers (1 byte)
    0x19, 0xe0,
    0x29, 0xe7,
    0x15, 0x00,
    0x25, 0x01,
    0x75, 0x01,
    0x95, 0x08,
    0x81, 0x02,        // INPUT (Data,Var,Abs)

    // Reserved byte
    0x75, 0x08,
    0x95, 0x01,
    0x81, 0x01,        // INPUT (Const,Var,Abs)

    // Key array (6 bytes)
    0x75, 0x08,
    0x95, 0x06,
    0x15, 0x00,
    0x25, 0x65,        // LOGICAL_MAX (101)
    0x05, 0x07,
    0x19, 0x00,
    0x29, 0x65,
    0x81, 0x00,        // INPUT (Data,Array,Abs)

    // LED output report (5 bits) + padding (3 bits)
    0x75, 0x01,
    0x95, 0x05,
    0x05, 0x08,
    0x19, 0x01,
    0x29, 0x05,
    0x91, 0x02,        // OUTPUT (Data,Var,Abs)
    0x75, 0x03,
    0x95, 0x01,
    0x91, 0x01,        // OUTPUT (Const,Var,Abs)

    0xc0               // END_COLLECTION
};

#define MOD_CONTROL_LEFT    MODIFIERKEY_LEFT_CTRL
#define MOD_SHIFT_LEFT      MODIFIERKEY_LEFT_SHIFT
#define MOD_ALT_LEFT        MODIFIERKEY_LEFT_ALT
#define MOD_GUI_LEFT        MODIFIERKEY_LEFT_GUI
#define MOD_CONTROL_RIGHT   MODIFIERKEY_RIGHT_CTRL
#define MOD_SHIFT_RIGHT     MODIFIERKEY_RIGHT_SHIFT
#define MOD_ALT_RIGHT       MODIFIERKEY_RIGHT_ALT
#define MOD_GUI_RIGHT       MODIFIERKEY_RIGHT_GUI

class DigiKeyboardDevice: public Print {
public:
    DigiKeyboardDevice() {
        noInterrupts();
        usbDeviceDisconnect();
        _delay_ms(250);
        usbDeviceConnect();

        usbInit();
        interrupts();

        memset(reportBuffer, 0, sizeof(reportBuffer));
        usbSetInterrupt(reportBuffer, sizeof(reportBuffer));
    }

    void update() {
        usbPoll();
    }

    void delay(long milli) {
        unsigned long last = millis();
        while (milli > 0) {
            unsigned long now = millis();
            milli -= now - last;
            last = now;
            update();
        }
    }

    void sendKeyStroke(byte keyStroke) {
        sendKeyStroke(keyStroke, 0);
    }

    void enableLEDFeedback() {
        sUseFeedbackLed = true;
        pinMode(LED_BUILTIN, OUTPUT);
    }

    void disableLEDFeedback() {
        sUseFeedbackLed = false;
    }

    void sendKeyStroke(byte keyStroke, byte modifiers) {
        sendKeyStroke(keyStroke, modifiers, false);
    }

    void sendKeyStroke(byte keyStroke, byte modifiers, bool aUseFeedbackLed) {
        if (aUseFeedbackLed) {
            digitalWrite(LED_BUILTIN, HIGH);
        }
        sendKeyPress(keyStroke, modifiers);
        sendKeyPress(0, 0); // release
        if (aUseFeedbackLed) {
            digitalWrite(LED_BUILTIN, LOW);
        }
    }

    void sendKeyPress(byte keyPress) {
        sendKeyPress(keyPress, 0);
    }

    void sendKeyPress(byte keyPress, byte modifiers) {
        while (!usbInterruptIsReady()) {
            usbPoll();
            _delay_ms(5);
        }
        memset(reportBuffer, 0, sizeof(reportBuffer));
        reportBuffer[0] = modifiers; // modifiers
        reportBuffer[1] = 0;         // reserved
        reportBuffer[2] = keyPress;  // first key
        // rest remain zero
        usbSetInterrupt(reportBuffer, sizeof(reportBuffer));
    }

    uint8_t keycode_to_modifier(uint8_t keycode) {
        uint8_t modifier = 0;
#ifdef SHIFT_MASK
        if (keycode & SHIFT_MASK)
            modifier |= MODIFIERKEY_SHIFT;
#endif
#ifdef ALTGR_MASK
        if (keycode & ALTGR_MASK)
            modifier |= MODIFIERKEY_RIGHT_ALT;
#endif
#ifdef RCTRL_MASK
        if (keycode & RCTRL_MASK)
            modifier |= MODIFIERKEY_RIGHT_CTRL;
#endif
        return modifier;
    }

    uint8_t keycode_to_key(uint8_t keycode) {
        uint8_t key = keycode & KEYCODE_MASK_SCANCODE;
        if (key == KEY_NON_US_BS_MAPPING) {
            key = (uint8_t) KEY_NON_US_BS;
        }
        return key;
    }

    size_t write(uint8_t chr) {
        uint8_t data = 0;
        if (chr == '\b') {
            data = (uint8_t) KEY_BACKSPACE;
        } else if (chr == '\t') {
            data = (uint8_t) KEY_TAB;
        } else if (chr == '\n' || chr == '\r') {
            data = (uint8_t) KEY_ENTER;
        } else if (chr >= 0x20) {
            data = pgm_read_byte_near(keycodes_ascii + (chr - 0x20));
        }
        if (data) {
            sendKeyStroke(keycode_to_key(data), keycode_to_modifier(data), sUseFeedbackLed);
        }
        return 1;
    }

    bool sUseFeedbackLed = false;
    uchar reportBuffer[8];    // boot keyboard: 8-byte report
    using Print::write;
};

DigiKeyboardDevice DigiKeyboard = DigiKeyboardDevice();

#ifdef __cplusplus
extern "C" {
#endif
uchar usbFunctionSetup(uchar data[8]) {
    usbRequest_t *rq = (usbRequest_t*) ((void*) data);
    usbMsgPtr = DigiKeyboard.reportBuffer;
    if ((rq->bmRequestType & USBRQ_TYPE_MASK) == USBRQ_TYPE_CLASS) {
        if (rq->bRequest == USBRQ_HID_GET_REPORT) {
            return 0;
        } else if (rq->bRequest == USBRQ_HID_GET_IDLE) {
            return 0;
        } else if (rq->bRequest == USBRQ_HID_SET_IDLE) {
            idleRate = rq->wValue.bytes[1];
        }
    }
    return 0;
}
#ifdef __cplusplus
} // extern "C"
#endif

#endif // __DigiKeyboard_h__
