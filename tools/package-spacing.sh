#!/bin/sh
# Tram & Railcar Spacing: assemble the Workshop item in dist/TramRailcarSpacing/
# and, with GAME set, copy it into the game's project folder for the item, from
# which the game's Workshop screen uploads it. Same flow as package-release.sh.
#
#   tools/package-spacing.sh          build, assemble, fill the project folder
#   tools/package-spacing.sh clean    remove the project folder after an upload
#
# First upload: no Workshop id yet. The item goes to workshop_wip/TramRailcarSpacing
# with its picture and description; the game's Workshop screen uploads it and
# writes $ITEM_ID / $OWNER_ID into its workshopconfig.ini. `clean` then prints
# that id: put it into ITEM below (or pass SPACING_ITEM=<id>); from then on the
# project folder is workshop_wip/<id>, recreated with the ids when missing.
ITEM=${SPACING_ITEM:-0}
FIRST=TramRailcarSpacing

if [ -f "$HOME/wrsr-env.sh" ]; then . "$HOME/wrsr-env.sh"; fi
if [ "$ITEM" = 0 ]; then DIR=$FIRST; else DIR=$ITEM; fi
if [ "$1" = "clean" ]; then
    CFG="$GAME/media_soviet/workshop_wip/$DIR/workshopconfig.ini"
    if [ "$ITEM" = 0 ] && [ -f "$CFG" ]; then
        id=$(grep -h '^\$ITEM_ID' "$CFG" | awk '{print $2}')
        if [ -z "$id" ]; then
            echo "[package] $DIR has no \$ITEM_ID yet - not uploaded? Folder kept." >&2
            exit 1
        fi
        echo "[package] WORKSHOP ID: $id  (tell it, or use SPACING_ITEM=$id from now on)"
    fi
    rm -rf "$GAME/media_soviet/workshop_wip/$DIR"
    echo "[package] removed media_soviet/workshop_wip/$DIR"
    exit 0
fi
set -e
cd "$(dirname "$0")/.."

cmake --build --preset mingw

OUT=dist/TramRailcarSpacing
rm -rf "$OUT"
mkdir -p "$OUT/plugins"
cp workshop-spacing/art/previewimage.png "$OUT/previewimage.png"
cp build/mingw/wrsr_tram_spacing.dll config/wrsr_tram_spacing.ini "$OUT/plugins/"

# The description is one quoted value: it must not contain a double quote.
if grep -q '"' workshop-spacing/description.bbcode; then
    echo "[package] workshop-spacing/description.bbcode contains a double quote" >&2
    exit 1
fi
{
    printf '%s\n\n' "\$ITEM_TYPE ${ITEM_TYPE:-WORKSHOP_ITEMTYPE_SCRIPT}"
    printf '%s\n\n' '$ITEM_NAME "Tram & Railcar Spacing"'
    printf '$ITEM_DESC "'
    cat workshop-spacing/description.bbcode
    printf '"\n\n$END\n'
} > "$OUT/workshopconfig.ini"

echo "[package] $OUT:"
ls -R "$OUT"

if [ -n "$GAME" ] && [ -d "$GAME/media_soviet" ]; then
    DEST="$GAME/media_soviet/workshop_wip/$DIR"
    mkdir -p "$DEST"
    if [ "$ITEM" != 0 ]; then
        if [ ! -f "$DEST/workshopconfig.ini" ]; then
            # Removed after the last upload ('clean'): recreate it with the ids
            # the game gave the item. 0 = not published, 2 = public.
            OWNER=$(grep -hoE '"7656119[0-9]{10}"' "$STEAM/config/loginusers.vdf" 2>/dev/null | head -n 1 | tr -d '"')
            [ -n "$OWNER" ] || { echo "[package] cannot read your Steam ID" >&2; exit 1; }
            printf '%s\n\n' '$ITEM_ID '"$ITEM" '$OWNER_ID '"$OWNER" '$VISIBILITY '"${VISIBILITY:-2}" > "$DEST/workshopconfig.ini"
        fi
        if ! grep -q "^\$ITEM_ID $ITEM" "$DEST/workshopconfig.ini"; then
            echo "[package] $DEST/workshopconfig.ini is not for item $ITEM" >&2
            exit 1
        fi
    fi
    # keep the ids the game wrote (none before the first upload)
    ids=$(grep -hE '^\$(ITEM_ID|OWNER_ID|VISIBILITY)' "$DEST/workshopconfig.ini" 2>/dev/null || true)
    { [ -n "$ids" ] && printf '%s\n\n' "$ids"; cat "$OUT/workshopconfig.ini"; } > "$OUT/workshopconfig.ini.new"
    mv "$OUT/workshopconfig.ini.new" "$OUT/workshopconfig.ini"
    cp -R "$OUT/." "$DEST/"
    echo "[package] copied to media_soviet/workshop_wip/$DIR:"
    grep -E '^\$(ITEM_ID|OWNER_ID|VISIBILITY|ITEM_NAME|ITEM_TYPE)' "$DEST/workshopconfig.ini"
    [ "$ITEM" = 0 ] && echo "[package] FIRST UPLOAD: the game shows it under Workshop -> your items; upload it there"
    echo "[package] upload it in the game (Workshop -> your items -> the card -> green tick),"
    if [ "$ITEM" = 0 ]; then echo "[package] then run: tools/package-spacing.sh clean   (it prints the new id)"
    else echo "[package] then run: SPACING_ITEM=$ITEM tools/package-spacing.sh clean"; fi
fi
