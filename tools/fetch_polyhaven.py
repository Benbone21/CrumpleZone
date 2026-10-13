# Downloads the CC0 Poly Haven starter set (models + ground textures, 1k, ~154 MB) into
# assets/source/polyhaven (gitignored), checking each file against the API's md5.
# Usage: python3 tools/fetch_polyhaven.py assets/source/polyhaven
import json, os, sys, hashlib, urllib.request
root = sys.argv[1]
models = "rock_moss_set_01 rock_moss_set_02 rock_07 rock_09 boulder_01 stone_01 namaqualand_boulder_03 namaqualand_boulder_04 namaqualand_boulder_05 namaqualand_boulders_01 namaqualand_stones_01 rock_face_01 rock_face_02 namaqualand_cliff_01 fir_sapling pine_sapling_small dead_tree_trunk dead_tree_trunk_02 shrub_02 shrub_03 shrub_04 fern_02 grass_medium_02 grass_bermuda_01 tree_stump_01 tree_stump_02 dry_branches_medium_01".split()
textures = "forrest_ground_01 brown_mud_leaves_01 brown_mud_03 brown_mud_dry rocky_terrain_02 gravel_ground_01 sandy_gravel_02 snow_02 forest_leaves_04 rocky_trail".split()
UA = {"User-Agent": "curl/8"}
def get(url):
    return urllib.request.urlopen(urllib.request.Request(url, headers=UA), timeout=120).read()
def save(path, ent):
    if os.path.exists(path) and hashlib.md5(open(path,'rb').read()).hexdigest() == ent['md5']: return 0
    os.makedirs(os.path.dirname(path), exist_ok=True)
    b = get(ent['url'])
    if hashlib.md5(b).hexdigest() != ent['md5']: raise SystemExit("md5 mismatch " + path)
    open(path, 'wb').write(b); return len(b)
total = 0
for m in models:
    d = json.loads(get("https://api.polyhaven.com/files/" + m))['gltf']['1k']['gltf']
    base = os.path.join(root, "models", m)
    total += save(os.path.join(base, m + "_1k.gltf"), d)
    for rel, ent in d['include'].items(): total += save(os.path.join(base, rel), ent)
    print("model", m, round(total/1e6,1), "MB", flush=True)
for t in textures:
    d = json.loads(get("https://api.polyhaven.com/files/" + t))
    base = os.path.join(root, "textures", t)
    for key, nm in (("Diffuse","diff"),("nor_gl","nor_gl"),("arm","arm"),("Displacement","disp")):
        if key in d and '1k' in d[key]:
            ent = d[key]['1k'].get('jpg') or d[key]['1k'].get('png')
            ext = 'jpg' if 'jpg' in d[key]['1k'] else 'png'
            total += save(os.path.join(base, f"{t}_{nm}_1k.{ext}"), ent)
    print("texture", t, round(total/1e6,1), "MB", flush=True)
print("TOTAL", round(total/1e6,1), "MB")
