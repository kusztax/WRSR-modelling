#!/bin/sh
# Build and install the mod into the CrossOver bottle for testing.
#
#   tools/deploy-mac.sh            build, plugin -> tesmioloader/plugins,
#                                  scripts -> a local (never uploaded) Workshop project
#
# Needs ~/wrsr-env.sh (BOTTLE, DRIVE_C, STEAM, GAME). The game must be closed:
# it holds the DLL open.
set -e
cd "$(dirname "$0")/.."

if [ -f "$HOME/wrsr-env.sh" ]; then . "$HOME/wrsr-env.sh"; fi
if [ -z "$GAME" ] || [ ! -f "$GAME/SOVIET64.exe" ]; then
    echo "[deploy] GAME is not set or has no SOVIET64.exe - see docs/02-crossover-setup.md" >&2
    exit 1
fi

echo "[deploy] build"
cmake --build --preset mingw

echo "[deploy] plugin -> tesmioloader/plugins"
cp -f build/mingw/wrsr_rail_mod.dll build/mingw/wrsr_rail_mod.ini "$GAME/tesmioloader/plugins/"

# The Scripts menu buttons come from a Workshop item. For testing, a local
# project in workshop_wip: the game lists its scripts only when $ITEM_ID is the
# folder's number and $OWNER_ID is the player (Rail Operations' finding).
ITEM=9957001
WIP="$GAME/media_soviet/workshop_wip/$ITEM"
mkdir -p "$WIP"
cp -f workshop/item/*.txt workshop/item/*.png "$WIP/"

OWNER=$(grep -hoE '"7656119[0-9]{10}"' "$STEAM/config/loginusers.vdf" 2>/dev/null | head -n 1 | tr -d '"')
if [ -z "$OWNER" ]; then
    echo "[deploy] could not read your Steam ID from $STEAM/config/loginusers.vdf" >&2
    exit 1
fi
# One $OBJECT_SCRIPT line per button: the script's name and the label the
# game shows. The flag script has none, so it is not a button.
printf '%s\n\n' '$ITEM_ID '"$ITEM" '$OWNER_ID '"$OWNER" '$ITEM_TYPE WORKSHOP_ITEMTYPE_SCRIPT' '$VISIBILITY 2' \
    '$OBJECT_SCRIPT RailPriority_On "Rail priority: ON - passenger trains first at signals"' \
    '$OBJECT_SCRIPT RailPriority_Off "Rail priority: OFF - the game decides at signals"' \
    '$ITEM_NAME "Rail Priority (local test)"' '$ITEM_DESC "Local test build. Do not upload."' '$END' \
    > "$WIP/workshopconfig.ini"
echo "[deploy] scripts -> media_soviet/workshop_wip/$ITEM (owner $OWNER)"
echo "[deploy] ok - start the game with tesmiolauncher.exe --nogui"
