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
# The ini from config/, not build/: the build copies it only when the DLL is
# relinked, so an ini-only change would not arrive.
cp -f build/mingw/wrsr_rail_mod.dll config/wrsr_rail_mod.ini config/rail_priority.png "$GAME/tesmioloader/plugins/"
grep '^signal_aspect' "$GAME/tesmioloader/plugins/wrsr_rail_mod.ini" | sed 's/^/[deploy] /'
# Tram & Railcar Spacing, the second plugin. NOSPACING=1 leaves it out (and
# removes an earlier local copy).
if [ "$NOSPACING" = 1 ]; then
    rm -f "$GAME/tesmioloader/plugins/wrsr_tram_spacing.dll" "$GAME/tesmioloader/plugins/wrsr_tram_spacing.ini"
    echo "[deploy] tram spacing: left out"
else
    cp -f build/mingw/wrsr_tram_spacing.dll config/wrsr_tram_spacing.ini "$GAME/tesmioloader/plugins/"
    echo "[deploy] tram spacing -> tesmioloader/plugins"
fi

# The Scripts menu buttons come from the published Workshop item (subscribe
# to 3844461371). A local DLL in tesmioloader/plugins wins over the one in
# the Workshop folder, so this tests the new build with the published buttons.
# (The old local test project, workshop_wip/9957001, is no longer made: with
# the subscription it would show every button twice.)
# EXTRA="key = value" (several: one per line): set ini values for a single
# test. An existing key is changed in place - the game reads the FIRST
# occurrence, so appending a duplicate would be ignored.
if [ -n "$EXTRA" ]; then
    INI="$GAME/tesmioloader/plugins/wrsr_rail_mod.ini"
    printf '%s\n' "$EXTRA" | while IFS= read -r line; do
        [ -n "$line" ] || continue
        key=$(printf '%s' "$line" | sed 's/[[:space:]]*=.*//')
        if grep -q "^$key[[:space:]]*=" "$INI"; then
            sed -i '' "s|^$key[[:space:]]*=.*|$line|" "$INI"
        else
            printf '\n%s\n' "$line" >> "$INI"
        fi
        echo "[deploy] $(grep "^$key[[:space:]]*=" "$INI" | head -n 1)"
    done
fi
if [ -n "$ASPECT" ]; then
    sed -i '' "s/^signal_aspect = .*/signal_aspect = $ASPECT/" "$GAME/tesmioloader/plugins/wrsr_rail_mod.ini"
    echo "[deploy] signal_aspect = $ASPECT"
fi
echo "[deploy] ok - start the game with tesmiolauncher.exe --nogui"
