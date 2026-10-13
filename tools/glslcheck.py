# Offline GLSL check (no GPU needed): pulls the car body and lit shaders out of
# src/render.cpp (header + kCommonFS + body) and compiles them with glslangValidator
# (apt install glslang-tools).  Usage: python3 tools/glslcheck.py src/render.cpp
import os, re, subprocess, sys, tempfile

src = open(sys.argv[1] if len(sys.argv) > 1 else "src/render.cpp").read()


def raw(name):
    m = re.search(r'const char\* ' + name + r' = R"\((.*?)\)";', src, re.S)
    if not m:
        sys.exit("shader string %s not found" % name)
    return m.group(1)


bad = 0
for vs, fs in (("kCarVS", "kCarFSBody"), ("kLitVS", "kLitFSBody")):
    d = tempfile.mkdtemp()
    files = {"s.vert": raw(vs), "s.frag": "#version 330 core\n" + raw("kCommonFS") + raw(fs)}
    for f, text in files.items():
        path = os.path.join(d, f)
        open(path, "w").write(text)
        r = subprocess.run(["glslangValidator", path], capture_output=True, text=True)
        name = fs if f.endswith("frag") else vs
        print(name, "OK" if r.returncode == 0 else "FAIL")
        if r.returncode:
            bad += 1
            print(r.stdout.strip()[-1500:])
sys.exit(1 if bad else 0)
