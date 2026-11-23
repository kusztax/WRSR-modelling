// lang.h - the mod's few texts, in the language the player has chosen.
//
// The game reads its texts from media_soviet/soviet<Language>.btf and serves
// them through C3D_LANGUAGE::GetString(id), an import from C3DDLL64.dll. We
//  - watch which soviet<Language>.btf the game opens (fopen family and
//    C3DHelp_ReadFileIntoBuffer, in both the exe and the engine), and
//  - answer GetString for our own private ids from a built-in table.
// The scripts use those ids (Notification_CreateNewLangIDPic), so a
// notification shows in the player's language, and follows a language change
// in the options. Unknown language: English. Every hook goes through the
// loader's patchIat, so it chains with TesmioLoader's VFS and other plugins.
#pragma once

#include <windows.h>

struct TsmHost;

namespace wrsr::lang {

// Private ids, far above the game's highest (580231) and TesmioLoader's
// 1 000 000+ range. The scripts in workshop/ use the same numbers.
constexpr int ID_TITLE    = 1957001;   // "Rail priority"
constexpr int ID_TEXT_ON  = 1957002;   // what ON means
constexpr int ID_TEXT_OFF = 1957003;   // what OFF means
constexpr int ID_TEXT_DONE   = 1957004;   // research completed: switched on
constexpr int ID_TEXT_NOTYET = 1957005;   // ON pressed before the research
// 1957011..1957015: the maximum-wait button, for 30, 60, 90, 120, 180 s.
constexpr int ID_HOLD_FIRST = 1957011;

bool Init(const TsmHost* host, HMODULE exe, HMODULE engine);

}  // namespace wrsr::lang
