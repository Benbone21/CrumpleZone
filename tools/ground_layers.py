#!/usr/bin/env python3
"""Makes assets/ground's 1k sets (diff, nor_gl, arm, disp jpgs: what src/groundtex.cpp loads) from 2k sources.

  Poly Haven:  python3 tools/ground_layers.py ph assets/source/polyhaven/textures cliff_side rock_boulder_cracked ...
  ambientCG:   python3 tools/ground_layers.py acg assets/source/ambientcg Rock029      (written as acg_rock029_*: its
               AmbientOcclusion and Roughness packed into the arm map, metal 0)

Needs PIL (the ROCm venv has it).
"""
import os, sys
from PIL import Image

OUT = "assets/ground"


def save(img, name, mode="RGB"):
    img = img.convert(mode).resize((1024, 1024), Image.LANCZOS)
    img.save(os.path.join(OUT, name), quality=92)


def main():
    kind, root, ids = sys.argv[1], sys.argv[2], sys.argv[3:]
    for i in ids:
        if kind == "ph":
            d = os.path.join(root, i)
            for part in ("diff", "nor_gl", "arm", "disp"):
                save(Image.open(os.path.join(d, f"{i}_{part}_2k.jpg")), f"{i}_{part}_1k.jpg", "L" if part == "disp" else "RGB")
        else:
            d = os.path.join(root, i)
            p = lambda s: os.path.join(d, f"{i}_2K-JPG_{s}.jpg")
            n = "acg_" + i.lower()
            save(Image.open(p("Color")), f"{n}_diff_1k.jpg")
            save(Image.open(p("NormalGL")), f"{n}_nor_gl_1k.jpg")
            save(Image.open(p("Displacement")), f"{n}_disp_1k.jpg", "L")
            ao = Image.open(p("AmbientOcclusion")).convert("L") if os.path.exists(p("AmbientOcclusion")) else None
            ro = Image.open(p("Roughness")).convert("L")
            if ao is None:
                ao = Image.new("L", ro.size, 255)
            save(Image.merge("RGB", (ao.resize(ro.size), ro, Image.new("L", ro.size, 0))), f"{n}_arm_1k.jpg")
        print("made", i)


if __name__ == "__main__":
    main()
