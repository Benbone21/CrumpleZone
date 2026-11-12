# Downloads CC0 ambientCG materials (2K-JPG zips) into assets/source/ambientcg/<id>/ (gitignored),
# checking each zip against the API's size, and extracting only plain image files (no paths).
# ambientCG normal maps are DirectX (_NormalDX) by default; the script reports whether _NormalGL is present.
# Usage: python3 tools/fetch_ambientcg.py assets/source/ambientcg Rock029 Rock009
import json, os, sys, io, zipfile, urllib.request
root, ids = sys.argv[1], sys.argv[2:]
RES = "2K-JPG"
UA = {"User-Agent": "curl/8"}
EXT = (".jpg", ".jpeg", ".png")
def get(url):
    return urllib.request.urlopen(urllib.request.Request(url, headers=UA), timeout=300).read()
def extract(zb, dest):
    names = []
    with zipfile.ZipFile(io.BytesIO(zb)) as z:
        for info in z.infolist():
            name = os.path.basename(info.filename)
            # only flat image files: no directories, no traversal, no hidden files
            if info.is_dir() or info.filename != name or name.startswith('.') or not name.lower().endswith(EXT): continue
            out = os.path.join(dest, name)
            if os.path.exists(out) and os.path.getsize(out) == info.file_size: names.append(name); continue
            data = z.read(info)
            open(out, 'wb').write(data); names.append(name)
    return names
total = 0
for a in ids:
    q = json.loads(get(f"https://ambientcg.com/api/v2/full_json?id={a}&include=downloadData"))
    asset = next(x for x in q['foundAssets'] if x['assetId'].lower() == a.lower())
    dls = [d for f in asset['downloadFolders'].values() for c in f['downloadFiletypeCategories'].values() for d in c['downloads']]
    dl = next(d for d in dls if d['attribute'] == RES and d['filetype'] == 'zip')
    dest = os.path.join(root, asset['assetId'])
    os.makedirs(dest, exist_ok=True)
    zpath = os.path.join(dest, dl['fileName'])
    if os.path.exists(zpath) and os.path.getsize(zpath) == dl['size']:
        zb = open(zpath, 'rb').read()
    else:
        zb = get(dl['downloadLink'])
        if len(zb) != dl['size']: raise SystemExit(f"size mismatch {a}: {len(zb)} != {dl['size']}")
        open(zpath, 'wb').write(zb); total += len(zb)
    names = extract(zb, dest)
    gl = any('NormalGL' in n for n in names)
    print("material", asset['assetId'], round(len(zb)/1e6, 1), "MB", len(names), "images,", "NormalGL" if gl else "NormalDX only", flush=True)
print("TOTAL downloaded", round(total/1e6, 1), "MB")
