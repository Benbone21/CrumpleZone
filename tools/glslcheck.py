# Offline GLSL check (no GPU needed): pulls the shaders out of the sources (header +
# kCommonFS + the chunks each program is linked from) and compiles them with
# glslangValidator (apt install glslang-tools).  Usage: python3 tools/glslcheck.py [srcdir]
import os, re, subprocess, sys, tempfile

srcdir = sys.argv[1] if len(sys.argv) > 1 else "src"
if srcdir.endswith(".cpp"):  # (the old usage: a path to render.cpp)
    srcdir = os.path.dirname(srcdir) or "."
src = "".join(open(os.path.join(srcdir, f)).read() for f in ("render.cpp", "groundshader.cpp", "propdraw.cpp"))


def raw(name):
    m = re.search(r'const char\*(?: const)? ' + name + r' = R"\((.*?)\)";', src, re.S)
    if not m:
        sys.exit("shader string %s not found" % name)
    return m.group(1)


H = "#version 330 core\n"
# (program, vertex shader, fragment shader parts; parts without a #version get the header)
programs = [
    ("car", "kCarVS", ["kCommonFS", "kCarFSBody"]),
    ("lit", "kLitVS", ["kCommonFS", "kLitIns", "kTerrainProcFS", "kLitFSBody"]),
    ("terrain (photo ground)", "kLitVS", ["kCommonFS", "kLitIns", "kTerrainProcFS", "kGroundShaderFS"]),
    ("water", "kLitVS", ["kCommonFS", "kWaterFSBody"]),
    ("grass", "kGrassVS", ["kCommonFS", "kGrassFSBody"]),
    ("props", "kPropVS", ["kCommonFS", "kPropFSBody"]),
    ("props depth", "kPropVS", ["kPropDepthFS"]),
    ("billboards", "kBillboardVS", ["kCommonFS", "kBillboardFSBody"]),
    ("billboards depth", "kBillboardVS", ["kBillboardDepthFS"]),
]
bad = 0
for name, vs, fs in programs:
    d = tempfile.mkdtemp()
    frag = "".join(raw(p) for p in fs)
    if not frag.lstrip().startswith("#version"):
        frag = H + frag
    for f, text in (("s.vert", raw(vs)), ("s.frag", frag)):
        path = os.path.join(d, f)
        open(path, "w").write(text)
        r = subprocess.run(["glslangValidator", path], capture_output=True, text=True)
        print("%-24s %-5s %s" % (name, f[2:], "OK" if r.returncode == 0 else "FAIL"))
        if r.returncode:
            bad += 1
            print(r.stdout.strip()[-1500:])
sys.exit(1 if bad else 0)
