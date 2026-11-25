#!/bin/sh
# Tram & Railcar Spacing: assemble the Workshop item in dist/TramRailcarSpacing/
# and, with GAME set, copy it into the game's project folder for the item, from
# which the game's Workshop screen uploads it. Same flow as package-release.sh.
#
#   tools/package-spacing.sh          build, assemble, fill the project folder
#   tools/package-spacing.sh clean    remove the project folder after an upload
#
# The project is created once in the game (Workshop -> +): that gives it its
# Workshop id. Put the id here (ITEM=...) or pass SPACING_ITEM=<id>.
ITEM=${SPACING_ITEM:-0}

if [ -f "$HOME/wrsr-env.sh" ]; then . "$HOME/wrsr-env.sh"; fi
if [ "$ITEM" = 0 ]; then
    echo "[package] no Workshop id yet. Create the item in the game (Workshop -> +), then run:" >&2
    echo "[package]   SPACING_ITEM=<id> tools/package-spacing.sh" >&2
    if [ -n "$GAME" ] && [ -d "$GAME/media_soviet/workshop_wip" ]; then
        echo "[package] project folders in the game now:" >&2
        for f in "$GAME"/media_soviet/workshop_wip/*/workshopconfig.ini; do
            [ -f "$f" ] || continue
            printf '[package]   %s  %s\n' "$(basename "$(dirname "$f")")" \
                "$(grep -h '^\$ITEM_NAME' "$f" | head -n 1)" >&2
        done
    fi
    exit 1
fi
if [ "$1" = "clean" ]; then
    rm -rf "$GAME/media_soviet/workshop_wip/$ITEM"
    echo "[package] removed media_soviet/workshop_wip/$ITEM"
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
    DEST="$GAME/media_soviet/workshop_wip/$ITEM"
    if [ ! -f "$DEST/workshopconfig.ini" ]; then
        echo "[package] $DEST/workshopconfig.ini is missing - create the item in the game first" >&2
        exit 1
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
    grep -E '^\$(ITEM_ID|OWNER_ID|VISIBILITY|ITEM_NAME|ITEM_TYPE)' "$DEST/workshopconfig.ini"
    echo "[package] upload it in the game (Workshop -> your items -> the card -> green tick),"
    echo "[package] then run: SPACING_ITEM=$ITEM tools/package-spacing.sh clean"
fi
