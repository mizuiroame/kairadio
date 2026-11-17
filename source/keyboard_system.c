#include "keyboard_system.h"

#include <string.h>

#include <switch.h>

static SystemKeyboardResult show(SwkbdType type, const char *title, const char *initial, char *out,
                                 size_t size, size_t max_length)
{
    SwkbdConfig config;
    if (R_FAILED(swkbdCreate(&config, 0)))
        return SYSTEM_KEYBOARD_UNAVAILABLE;
    swkbdConfigMakePresetDefault(&config);
    swkbdConfigSetType(&config, type);
    if (type == SwkbdType_Latin) {
        // Latin only, without the characters that can't be in a file name:
        // other scripts would make the playlist file invisible on Switch 2.
        swkbdConfigSetKeySetDisableBitmask(&config, SwkbdKeyDisableBitmask_At | SwkbdKeyDisableBitmask_Percent |
                                                        SwkbdKeyDisableBitmask_ForwardSlash |
                                                        SwkbdKeyDisableBitmask_Backslash);
    }
    swkbdConfigSetHeaderText(&config, title);
    swkbdConfigSetGuideText(&config, title);
    swkbdConfigSetInitialText(&config, initial ? initial : "");
    swkbdConfigSetStringLenMax(&config, (u32)max_length);

    memset(out, 0, size);
    Result rc = swkbdShow(&config, out, size);
    swkbdClose(&config);
    if (R_SUCCEEDED(rc))
        return SYSTEM_KEYBOARD_OK;
    // libnx reports the user's cancel as a bad library applet exit.
    if (rc == MAKERESULT(Module_Libnx, LibnxError_LibAppletBadExit))
        return SYSTEM_KEYBOARD_CANCELLED;
    return SYSTEM_KEYBOARD_UNAVAILABLE;
}

SystemKeyboardResult systemKeyboardInput(const char *title, const char *initial, char *out,
                                         size_t size, size_t max_length)
{
    return show(SwkbdType_Latin, title, initial, out, size, max_length);
}

SystemKeyboardResult systemKeyboardNumber(const char *title, const char *initial, char *out,
                                          size_t size, size_t max_length)
{
    return show(SwkbdType_NumPad, title, initial, out, size, max_length);
}
