/* =========================================================================================

   This is an auto-generated file: Any edits you make may be overwritten!

*/

#pragma once

namespace BinaryData
{
    extern const char*   CreditsAU;
    const int            CreditsAUSize = 479;

    extern const char*   CreditsVST;
    const int            CreditsVSTSize = 537;

    extern const char*   flower_center_png;
    const int            flower_center_pngSize = 3299;

    extern const char*   flower_petals_png;
    const int            flower_petals_pngSize = 22287;

    extern const char*   copy_png;
    const int            copy_pngSize = 4098;

    extern const char*   garbage_png;
    const int            garbage_pngSize = 5113;

    extern const char*   reload_png;
    const int            reload_pngSize = 8380;

    extern const char*   settings_png;
    const int            settings_pngSize = 12539;

    extern const char*   DejaVuSansMono_ttf;
    const int            DejaVuSansMono_ttfSize = 340712;

    // Points to the start of a list of resource names.
    extern const char* namedResourceList[];

    // Number of elements in the namedResourceList array.
    const int namedResourceListSize = 9;

    // If you provide the name of one of the binary resource variables above, this function will
    // return the corresponding data and its size (or a null pointer if the name isn't found).
    const char* getNamedResource (const char* resourceNameUTF8, int& dataSizeInBytes) throw();
}
