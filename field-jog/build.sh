#!/bin/sh
# Regenerate every STL. Needs OpenSCAD on PATH.
set -e
cd "$(dirname "$0")"
mkdir -p stl
for p in top bottom platter fader fader_lock knob button loop flange plus faceplate foot; do
  echo "== $p"
  openscad -o "stl/$p.stl" -D "part=\"$p\"" field-jog.scad
done
echo "done"
