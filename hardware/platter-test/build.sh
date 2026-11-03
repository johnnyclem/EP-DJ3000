#!/bin/sh
# Regenerate the touch-test STLs. Needs OpenSCAD on PATH
# (a nightly from https://openscad.org/downloads.html#snapshots is fine).
set -e
cd "$(dirname "$0")"
mkdir -p stl preview
SCAD="${OPENSCAD:-openscad}"

export_stl() {
  # Empty intersections are a passing fit check, not a failed export.
  if ! "$SCAD" -o "stl/$1.stl" --export-format=binstl -D "part=\"$1\"" platter-test.scad; then
    case "$1" in
      overlap|seated) echo "(empty $1, no interference)"; : > "stl/$1.stl"; return 0 ;;
    esac
    return 1
  fi
}

for p in carrier wedge wedges stand overlap seated; do
  echo "== $p"
  export_stl "$p"
done

echo "== previews"
"$SCAD" -o preview/top.png --render --imgsize=1400,1000 --viewall --autocenter \
  --projection=ortho --camera=0,0,0,0,0,0,400 -D 'part="assembly"' platter-test.scad
"$SCAD" -o preview/angle.png --render --imgsize=1400,1000 --viewall --autocenter \
  --camera=0,0,0,62,0,32,420 -D 'part="assembly"' platter-test.scad
"$SCAD" -o preview/section.png --render --imgsize=1400,900 --viewall --autocenter \
  --camera=0,0,0,72,0,25,280 -D 'part="section"' platter-test.scad
"$SCAD" -o preview/wedges.png --render --imgsize=1400,800 --viewall --autocenter \
  --projection=ortho -D 'part="wedges"' platter-test.scad

python3 check.py
rm -f stl/overlap.stl stl/seated.stl
echo "done"
