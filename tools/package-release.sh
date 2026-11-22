#!/bin/sh
# Assemble the Workshop item in dist/RailPriority/ and, with GAME set, copy it
# into the game's project folder for the Workshop item, from which the game's
# Workshop screen uploads it.
#
#   tools/package-release.sh
#
# The project was created in the game (Workshop -> +), which gave it its
# Workshop id and wrote $ITEM_ID, $OWNER_ID and $VISIBILITY into its
# workshopconfig.ini. Those lines are kept; everything else is ours.
ITEM=3844461371
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
    if [ ! -f "$DEST/workshopconfig.ini" ] || ! grep -q "^\$ITEM_ID $ITEM" "$DEST/workshopconfig.ini"; then
        echo "[package] $DEST has no workshopconfig.ini for item $ITEM - create the item in the game first" >&2
        exit 1
    fi
    ids=$(grep -E '^\$(ITEM_ID|OWNER_ID|VISIBILITY)' "$DEST/workshopconfig.ini")
    { printf '%s\n\n' "$ids"; cat "$OUT/workshopconfig.ini"; } > "$OUT/workshopconfig.ini.new"
    mv "$OUT/workshopconfig.ini.new" "$OUT/workshopconfig.ini"
    cp -R "$OUT/." "$DEST/"
    echo "[package] copied to media_soviet/workshop_wip/$ITEM (ids and visibility kept):"
    grep -E '^\$(ITEM_ID|OWNER_ID|VISIBILITY|ITEM_NAME)' "$DEST/workshopconfig.ini"
fi
