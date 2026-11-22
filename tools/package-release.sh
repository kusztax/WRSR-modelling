#!/bin/sh
# Assemble the Workshop item in dist/RailPriority/ and, with GAME set, copy it
# into the game's project folder for the Workshop item, from which the game's
# Workshop screen uploads it.
#
#   tools/package-release.sh          build, assemble, fill the game's project folder
#   tools/package-release.sh clean    remove the project folder after an upload
#                                     (else the author sees every button twice:
#                                     once from it, once from the subscription)
#
# The project was created in the game (Workshop -> +), which gave it its
# Workshop id and wrote $ITEM_ID, $OWNER_ID and $VISIBILITY into its
# workshopconfig.ini. Those lines are kept; everything else is ours.
ITEM=3844461371

if [ "$1" = "clean" ]; then
    rm -rf "$GAME/media_soviet/workshop_wip/$ITEM"
    echo "[package] removed media_soviet/workshop_wip/$ITEM"
    exit 0
fi
set -e
cd "$(dirname "$0")/.."
if [ -f "$HOME/wrsr-env.sh" ]; then . "$HOME/wrsr-env.sh"; fi

cmake --build --preset mingw

OUT=dist/RailPriority
rm -rf "$OUT"
mkdir -p "$OUT/plugins"
cp workshop/item/*.txt workshop/item/*.png "$OUT/"
cp workshop/art/previewimage.png "$OUT/previewimage.png"
cp build/mingw/wrsr_rail_mod.dll config/wrsr_rail_mod.ini "$OUT/plugins/"

# The description is one quoted value: it must not contain a double quote
# (Rail Operations' own description is cut short at one).
if grep -q '"' workshop/description.bbcode; then
    echo "[package] workshop/description.bbcode contains a double quote" >&2
    exit 1
fi
{
    printf '%s\n\n' '$ITEM_TYPE WORKSHOP_ITEMTYPE_SCRIPT'
    printf '%s\n' '$OBJECT_SCRIPT RailPriority_On "Rail priority: ON - passenger trains first at signals"'
    printf '%s\n\n' '$OBJECT_SCRIPT RailPriority_Off "Rail priority: OFF - the game decides at signals"'
    printf '%s\n\n' '$ITEM_NAME "Rail Priority"'
    printf '$ITEM_DESC "'
    cat workshop/description.bbcode
    printf '"\n\n$END\n'
} > "$OUT/workshopconfig.ini"

echo "[package] $OUT:"
ls -R "$OUT"

if [ -n "$GAME" ] && [ -d "$GAME/media_soviet" ]; then
    DEST="$GAME/media_soviet/workshop_wip/$ITEM"
    if [ ! -f "$DEST/workshopconfig.ini" ]; then
        # Removed after the last upload ('clean'): recreate it with the ids
        # the game gave the item. 0 = not published, 2 = public.
        OWNER=$(grep -hoE '"7656119[0-9]{10}"' "$STEAM/config/loginusers.vdf" 2>/dev/null | head -n 1 | tr -d '"')
        [ -n "$OWNER" ] || { echo "[package] cannot read your Steam ID" >&2; exit 1; }
        mkdir -p "$DEST"
        printf '%s\n\n' '$ITEM_ID '"$ITEM" '$OWNER_ID '"$OWNER" '$VISIBILITY '"${VISIBILITY:-2}" > "$DEST/workshopconfig.ini"
        echo "[package] recreated the project folder (visibility ${VISIBILITY:-2}; set VISIBILITY=0 for hidden)"
    fi
    if ! grep -q "^\$ITEM_ID $ITEM" "$DEST/workshopconfig.ini"; then
        echo "[package] $DEST/workshopconfig.ini is not for item $ITEM" >&2
        exit 1
    fi
    ids=$(grep -E '^\$(ITEM_ID|OWNER_ID|VISIBILITY)' "$DEST/workshopconfig.ini")
    { printf '%s\n\n' "$ids"; cat "$OUT/workshopconfig.ini"; } > "$OUT/workshopconfig.ini.new"
    mv "$OUT/workshopconfig.ini.new" "$OUT/workshopconfig.ini"
    cp -R "$OUT/." "$DEST/"
    echo "[package] copied to media_soviet/workshop_wip/$ITEM (ids and visibility kept):"
    grep -E '^\$(ITEM_ID|OWNER_ID|VISIBILITY|ITEM_NAME)' "$DEST/workshopconfig.ini"
    echo "[package] upload it in the game (Workshop -> your items -> the card -> green tick),"
    echo "[package] then run: tools/package-release.sh clean"
fi
