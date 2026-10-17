#include "thread_cache.h"
#include "carvisual.h"

#include "bodyshell.h"
#include "modelkit.h"
#include "objshape.h"
#include "scenery.h"

#include <array>
#include <map>
#include <string>
#include <set>
#include <memory>
#include <tuple>

namespace {

// ---- Catmull-Clark subdivision on weight vectors -------------------------------

using Sparse = std::vector<std::pair<int, float>>;  // node id -> weight

struct Acc {
    std::map<int, float> m;
    void add(const Sparse& s, float k) {
        for (const auto& e : s) m[e.first] += e.second * k;
    }
    Sparse take() const {
        Sparse out;
        for (const auto& kv : m)
            if (std::fabs(kv.second) > 1e-5f) out.push_back(kv);
        return out;
    }
};

using Lat = std::array<float, 3>;

struct CCMesh {
    std::vector<Sparse> verts;
    std::vector<std::array<int, 4>> faces;
    std::vector<int> parent;
    std::vector<std::array<Lat, 4>> lat;  // lattice coords per face corner
    std::set<std::pair<int, int>> seams;  // edges kept sharp at every level (panel gaps)
};

Lat mix(const Lat& a, const Lat& b) { return {(a[0] + b[0]) / 2, (a[1] + b[1]) / 2, (a[2] + b[2]) / 2}; }

// One Catmull-Clark step. Edges listed in `creases` (vertex-id pairs) stay sharp for
// this step, which keeps character lines (shoulders, roof edges) crisp.
CCMesh subdivide(const CCMesh& in, std::set<std::pair<int, int>> creases = {}) {
    creases.insert(in.seams.begin(), in.seams.end());
    const int V = (int)in.verts.size(), F = (int)in.faces.size();
    std::map<std::pair<int, int>, int> edgeId;
    std::vector<std::array<int, 2>> edges;
    std::vector<std::vector<int>> edgeFaces;
    std::vector<std::vector<int>> vFaces(V), vEdges(V);
    std::vector<std::array<int, 4>> faceEdges(F);
    for (int f = 0; f < F; f++)
        for (int c = 0; c < 4; c++) {
            int a = in.faces[f][c], b = in.faces[f][(c + 1) % 4];
            auto key = std::make_pair(std::min(a, b), std::max(a, b));
            auto it = edgeId.find(key);
            int e;
            if (it == edgeId.end()) {
                e = (int)edges.size();
                edgeId[key] = e;
                edges.push_back({key.first, key.second});
                edgeFaces.push_back({});
                vEdges[a].push_back(e);
                vEdges[b].push_back(e);
            } else {
                e = it->second;
            }
            edgeFaces[e].push_back(f);
            faceEdges[f][c] = e;
            vFaces[a].push_back(f);
        }
    const int E = (int)edges.size();

    std::vector<Sparse> facePt(F), edgePt(E), edgeMid(E), vertPt(V);
    for (int f = 0; f < F; f++) {
        Acc a;
        for (int c = 0; c < 4; c++) a.add(in.verts[in.faces[f][c]], 0.25f);
        facePt[f] = a.take();
    }
    for (int e = 0; e < E; e++) {
        Acc mid;
        mid.add(in.verts[edges[e][0]], 0.5f);
        mid.add(in.verts[edges[e][1]], 0.5f);
        edgeMid[e] = mid.take();
        bool crease = creases.count({edges[e][0], edges[e][1]}) > 0;
        if (edgeFaces[e].size() == 2 && !crease) {
            Acc a;
            a.add(in.verts[edges[e][0]], 0.25f);
            a.add(in.verts[edges[e][1]], 0.25f);
            a.add(facePt[edgeFaces[e][0]], 0.25f);
            a.add(facePt[edgeFaces[e][1]], 0.25f);
            edgePt[e] = a.take();
        } else {
            edgePt[e] = edgeMid[e];  // boundary / non-manifold: keep it sharp
        }
    }
    for (int v = 0; v < V; v++) {
        int n = (int)vEdges[v].size();
        bool manifold = n >= 3 && (int)vFaces[v].size() == n;
        for (int e : vEdges[v]) manifold &= edgeFaces[e].size() == 2;
        if (!manifold) {
            vertPt[v] = in.verts[v];
            continue;
        }
        std::vector<int> creaseNbr;
        for (int e : vEdges[v])
            if (creases.count({edges[e][0], edges[e][1]})) creaseNbr.push_back(edges[e][0] == v ? edges[e][1] : edges[e][0]);
        if (creaseNbr.size() >= 3) {  // corner of creases: stays put
            vertPt[v] = in.verts[v];
            continue;
        }
        if (creaseNbr.size() == 2) {  // on a crease line: curve rule along the line
            Acc a;
            a.add(in.verts[v], 0.75f);
            a.add(in.verts[creaseNbr[0]], 0.125f);
            a.add(in.verts[creaseNbr[1]], 0.125f);
            vertPt[v] = a.take();
            continue;
        }
        Acc a;
        for (int f : vFaces[v]) a.add(facePt[f], 1.0f / (n * n));
        for (int e : vEdges[v]) a.add(edgeMid[e], 2.0f / (n * n));
        a.add(in.verts[v], (n - 3.0f) / n);
        vertPt[v] = a.take();
    }

    CCMesh out;
    for (const auto& sm : in.seams) {  // a seam splits into two seam edges
        auto it = edgeId.find(sm);
        if (it == edgeId.end()) continue;
        int mid = V + it->second;
        out.seams.insert({std::min(sm.first, mid), std::max(sm.first, mid)});
        out.seams.insert({std::min(sm.second, mid), std::max(sm.second, mid)});
    }
    out.verts.reserve(V + E + F);
    for (auto& s : vertPt) out.verts.push_back(std::move(s));
    for (auto& s : edgePt) out.verts.push_back(std::move(s));
    for (auto& s : facePt) out.verts.push_back(std::move(s));
    for (int f = 0; f < F; f++) {
        const auto& L = in.lat[f];
        Lat center = {(L[0][0] + L[1][0] + L[2][0] + L[3][0]) / 4, (L[0][1] + L[1][1] + L[2][1] + L[3][1]) / 4,
                      (L[0][2] + L[1][2] + L[2][2] + L[3][2]) / 4};
        for (int c = 0; c < 4; c++) {
            int prev = (c + 3) % 4;
            out.faces.push_back({in.faces[f][c], V + faceEdges[f][c], V + E + f, V + faceEdges[f][prev]});
            out.parent.push_back(in.parent[f]);
            out.lat.push_back({L[c], mix(L[c], L[(c + 1) % 4]), center, mix(L[prev], L[c])});
        }
    }
    return out;
}

int regionOf(const SkinQuad& q, int cells) {
    if (q.mat == MAT_WELL) return REG_WELL;
    if (q.axis == 2 && q.side == 0) return REG_UNDER;
    if (q.cellChar == 'G') {
        if (q.axis == 2) return REG_ROOF;
        if (q.axis == 1) return REG_CABIN_SIDE;
        return q.side == 0 ? REG_WINDSHIELD : REG_REAR_WINDOW;
    }
    if (q.axis == 2) return q.mat == MAT_TRIM && q.cell[2] == 0 ? REG_BED : REG_TOP;
    if (q.axis == 1) return REG_SIDE;
    if (q.side == 0 && q.cell[0] == 0) return REG_FRONT;
    if (q.side == 1 && q.cell[0] == cells - 1) return REG_REAR;
    return REG_STEP;
}

// ---- Binding points to the lattice ----------------------------------------------

V3 trilinear(const V3 P[8], const V3& u, double w[8]) {
    V3 p;
    for (int b = 0; b < 8; b++) {
        w[b] = ((b & 1) ? u.x : 1 - u.x) * ((b & 2) ? u.y : 1 - u.y) * ((b & 4) ? u.z : 1 - u.z);
        p += P[b] * w[b];
    }
    return p;
}

// Lattice coordinates u (0..1 per axis) of p inside the hexahedron P (bit-ordered
// corners), by Newton iteration on the trilinear map. Returns how far outside it is.
double solveTrilinear(const V3 P[8], const V3& p, V3& u) {
    u = V3(0.5, 0.5, 0.5);
    double w[8];
    for (int it = 0; it < 12; it++) {
        V3 r = trilinear(P, u, w) - p;
        V3 J[3];
        for (int a = 0; a < 3; a++) {
            const double eps = 1e-4;
            V3 up = u;
            if (a == 0) up.x += eps;
            if (a == 1) up.y += eps;
            if (a == 2) up.z += eps;
            double w2[8];
            J[a] = (trilinear(P, up, w2) - (r + p)) / eps;
        }
        double det = dot(J[0], cross(J[1], J[2]));
        if (std::fabs(det) < 1e-12) break;
        V3 d((dot(r, cross(J[1], J[2]))) / det, (dot(J[0], cross(r, J[2]))) / det, (dot(J[0], cross(J[1], r))) / det);
        u -= d;
        if (len(d) < 1e-7) break;
    }
    return std::max({0.0, -u.x, u.x - 1, -u.y, u.y - 1, -u.z, u.z - 1});
}

VisualTemplate::Bind bindInHex(const Car& car, const int ids[8], const V3& p) {
    V3 P[8], u;
    for (int b = 0; b < 8; b++) P[b] = car.restLocal[ids[b]];
    solveTrilinear(P, p, u);
    double w[8];
    trilinear(P, u, w);
    VisualTemplate::Bind out{};
    for (int b = 0; b < 8; b++) {
        out.n[b] = ids[b];
        out.w[b] = (float)w[b];
    }
    return out;
}

VisualTemplate::Bind bindPoint(const Car& car, const V3& p) {
    VisualTemplate::Bind best{};
    double bestOut = 1e18;
    for (size_t ci = 0; ci < car.cells.size(); ci++) {
        V3 P[8], u;
        for (int b = 0; b < 8; b++) P[b] = car.restLocal[car.cells[ci][b]];
        double out = solveTrilinear(P, p, u);
        if (out < bestOut - 1e-9) {
            bestOut = out;
            best = bindInHex(car, car.cells[ci].data(), p);
        }
    }
    return best;
}

// The engine block's 8 corners and the gearbox (block's rear face + 4 tail nodes), as
// bit-ordered hexahedra, so engine parts ride on the block as it rocks on its mounts.
void blockHex(const Car& car, int engine[8], int gearbox[8]) {
    for (int b = 0; b < 8; b++) engine[b] = car.engineNodes[b];
    // Gearbox hex: bit0 = 0 tail / 1 block side, bit1 = up, bit2 = across.
    const int map[8] = {8, 0, 9, 2, 10, 4, 11, 6};
    for (int b = 0; b < 8; b++) gearbox[b] = car.engineNodes[map[b]];
}

VisualTemplate::Bind bindPanel(const Car& car, const V3& p, const char* name) {
    for (const Panel& panel : car.panels) {
        if (std::string(panel.name) != name) continue;
        VisualTemplate::Bind best{};
        double score = 1e30;
        for (int qi : panel.quads) {
            const SkinQuad& q = car.quads[qi];
            int ids[3] = {q.v[0], q.v[1], q.v[3]};
            V3 a=car.restLocal[ids[0]], u=car.restLocal[ids[1]]-a, v=car.restLocal[ids[2]]-a, d=p-a;
            double uu=dot(u,u), vv=dot(v,v), uv=dot(u,v), det=uu*vv-uv*uv;
            if (det<1e-10) continue;
            double x=(dot(d,u)*vv-dot(d,v)*uv)/det, y=(dot(d,v)*uu-dot(d,u)*uv)/det;
            V3 n=norm(cross(u,v));
            double dist=std::fabs(dot(d,n)) + std::max(0.0,-x)+std::max(0.0,x-1)+std::max(0.0,-y)+std::max(0.0,y-1);
            if (dist>=score) continue;
            score=dist;
            best={};
            for (int k=0;k<3;k++) {
                auto it=std::find(panel.bodyNodes.begin(),panel.bodyNodes.end(),ids[k]);
                best.n[k]=panel.panelNodes[it-panel.bodyNodes.begin()];
            }
            best.w[0]=(float)(1-x-y); best.w[1]=(float)x; best.w[2]=(float)y;
            best.normalOffset=(float)dot(d,n);
        }
        if (score<1e29) return best;
    }
    return bindPoint(car,p);
}

// Cross sections shared by the shell and details that must sit flush on it.
V3 jeskoSection(double x) {
    struct Station { double x, width, center, shoulder; };
    const Station shape[] = {
        {-2.313,0.87,0.69,0.70}, {-2.10,0.94,0.79,0.77}, {-1.70,1.00,0.87,0.83},
        {-1.287,1.015,0.90,0.86}, {-0.85,0.99,0.84,0.85}, {-0.45,0.88,0.76,0.78},
        {0.0,0.84,0.73,0.73}, {0.50,0.86,0.75,0.78}, {0.95,0.96,0.73,0.80},
        {1.413,1.015,0.655,0.81}, {1.90,0.97,0.54,0.68}, {2.16,0.86,0.46,0.51},
        {2.313,0.67,0.40,0.43}
    };
    // Cubic Hermite interpolation preserves the authored stations while rounding
    // the hood and haunches. Clamping to neighbors prevents spline overshoot.
    auto section = [&](double x, int field) {
        const int count = sizeof(shape)/sizeof(shape[0]);
        auto value = [&](int i) { return field==0?shape[i].width:field==1?shape[i].center:shape[i].shoulder; };
        int i=0;
        while(i<count-2 && shape[i+1].x<x) i++;
        int a=std::max(0,i-1), b=std::min(count-1,i+2);
        double dx=shape[i+1].x-shape[i].x, t=clampd((x-shape[i].x)/dx,0,1);
        double m0=(value(i+1)-value(a))/(shape[i+1].x-shape[a].x);
        double m1=(value(b)-value(i))/(shape[b].x-shape[i].x);
        double y=(2*t*t*t-3*t*t+1)*value(i)+(t*t*t-2*t*t+t)*dx*m0
                +(-2*t*t*t+3*t*t)*value(i+1)+(t*t*t-t*t)*dx*m1;
        return clampd(y,std::min(value(i),value(i+1)),std::max(value(i),value(i+1)));
    };
    return V3(section(x,0),section(x,1),section(x,2));
}

V3 jeskoHoodPoint(double x,double z) {
    V3 s=jeskoSection(x);
    return V3(x,s.y+(s.z-s.y)*std::pow(std::fabs(z)/s.x,2.6)+0.006,z);
}

// A dedicated Jesko surface, authored in meters from the manufacturer's 4.626 m
// length, 2.030 m width and 1.210 m height. Stations describe the actual silhouette;
// their surface points are bound to the deforming cage, not drawn as a rigid model.
void buildJeskoMesh(const Car& car, VisualTemplate& out) {
    auto section=[](double x,int field) {
        V3 s=jeskoSection(x);
        return field==0?s.x:field==1?s.y:s.z;
    };
    auto latX = [&](double x) {
        const int n=(int)car.spec->profile[0].size();
        for(int i=0;i<n;i++) {
            double a=latticeX(*car.spec,i), b=latticeX(*car.spec,i+1);
            if(x>=b) return i+clampd((a-x)/(a-b),0,1);
        }
        return (double)n;
    };
    auto panelAt = [&](const V3& p, int region) -> const char* {
        if(region==REG_CABIN_SIDE) return p.z<0?"LEFT DOOR":"RIGHT DOOR";
        if(region==REG_TOP && p.x>0.84) return "HOOD";
        if(region==REG_TOP && p.x<-1.13) return "ENGINE COVER";
        if(region==REG_SIDE && p.x<0.78 && p.x>-0.58) return p.z<0?"LEFT DOOR":"RIGHT DOOR";
        if(region==REG_FRONT && p.y<0.30) return "FRONT BUMPER";
        if(region==REG_REAR && p.y<0.32) return "REAR BUMPER";
        return nullptr;
    };
    auto parentOf = [&](const V3& p, int region) {
        double best=1e30; int result=0;
        const char* panel=panelAt(p,region);
        for(int qi=0;qi<(int)car.quads.size();qi++) {
            const SkinQuad& q=car.quads[qi];
            const bool glass = region==REG_WINDSHIELD || region==REG_REAR_WINDOW || region==REG_CABIN_SIDE;
            if(glass && q.mat!=MAT_GLASS) continue;
            int original=regionOf(q,(int)car.spec->profile[0].size());
            int pi=car.quadPanel[qi];
            bool matches=panel && pi>=0 && std::string(car.panels[pi].name)==panel;
            if(panel && !matches) continue;
            V3 c;
            for(int v:q.v)c+=car.restLocal[v]*0.25;
            double d=dot(p-c,p-c)+(original==region?0:1.0);
            if(d<best){best=d;result=qi;}
        }
        return result;
    };
    // Each patch has its own normals at a deliberate crease; neighboring samples
    // within it share vertices, giving continuous highlights along curved surfaces.
    auto patch = [&](int nu,int nv,int region,const auto& surface,const auto& uv,bool flip) {
        // Split the topology at detachable panel boundaries. Sharing a vertex
        // across a door seam would stretch a ribbon of paint when the door leaves.
        std::map<std::pair<int,std::string>,int> vertices;
        const double lift = car.spec->layerY[0] - 0.11;
        for(int u=0;u<nu;u++) for(int v=0;v<nv;v++) {
            V3 center=surface((u+0.5)/nu,(v+0.5)/nv);
            if(region==REG_TOP && center.x>-1.06 && center.x<0.78 && std::fabs(center.z)<0.66) continue;
            const char* panel=panelAt(center,region);
            int a=u*(nv+1)+v,b=a+nv+1;
            int ids[4]={a,b,b+1,a+1};
            if(flip)std::swap(ids[1],ids[3]);
            VisualTemplate::Face face{};
            for(int k=0;k<4;k++) {
                auto key=std::make_pair(ids[k],std::string(panel?panel:""));
                auto found=vertices.find(key);
                if(found==vertices.end()) {
                    double pu=(double)(ids[k]/(nv+1))/nu,pv=(double)(ids[k]%(nv+1))/nv;
                    V3 p=surface(pu,pv);
                    Lat lat=uv(pu,pv,p);
                    p.y+=lift;
                    int index=out.topoCount++;
                    found=vertices.emplace(key,index).first;
                    out.sculptBind.push_back(panel?bindPanel(car,p,panel):bindPoint(car,p));
                    out.rvTopo.push_back(index);
                    out.rvStatic.push_back({{lat[0],lat[1],lat[2]},(float)region});
                }
                face.topo[k]=face.rv[k]=found->second;
            }
            center.y+=lift;
            face.parent=parentOf(center,region);
            face.glass=region==REG_WINDSHIELD || region==REG_REAR_WINDOW || region==REG_CABIN_SIDE;
            out.faces.push_back(face);
        }
    };
    auto bodyUV=[&](double,double,const V3& p)->Lat {
        double k=p.y<0.48?(p.y-0.11)/0.37:1+(p.y-0.48)/0.32;
        return {(float)latX(p.x),(float)((p.z/2.03+0.5)*4),(float)k};
    };
    // Upper shell, with a slightly concave hood between the raised front fenders.
    patch(80,24,REG_TOP,[&](double u,double v) {
        double x=-2.313+4.626*u,z=2*v-1;
        double y=section(x,1)+(section(x,2)-section(x,1))*std::pow(std::fabs(z),2.6);
        return V3(x,y,section(x,0)*z);
    },bodyUV,true);
    // Sills and genuinely open circular wheel arches. Extra samples at the tangent
    // points make vertical arch returns instead of a rectangular hole in the cage.
    std::vector<double> stations;
    for(int i=0;i<=100;i++)stations.push_back(-2.313+4.626*i/100);
    for(double ax:{1.413,-1.287})for(double side:{-1.0,1.0}){
        stations.push_back(ax+side*0.437);
        stations.push_back(ax+side*0.436);
    }
    std::sort(stations.begin(),stations.end());
    auto sideX=[&](double u){double t=u*(stations.size()-1);int i=std::min((int)t,(int)stations.size()-2);return stations[i]+(stations[i+1]-stations[i])*(t-i);};
    for(int sd:{-1,1}) {
        patch((int)stations.size()-1,10,REG_SIDE,[&](double u,double v) {
            double x=sideX(u), bottom=0.15;
            for(double ax:{1.413,-1.287}) {
                double d=x-ax;
                if(std::fabs(d)<0.437)bottom=std::max(bottom,0.355+std::sqrt(std::max(0.0,0.437*0.437-d*d)));
            }
            double top=section(x,2);
            bottom=std::min(bottom,top-0.012);
            double tail=clampd((-x-1.86)/0.453,0,1);
            double tuck=(1-v)*(1-v);
            double z=section(x,0)-(0.05+0.05*tail)*tuck;
            return V3(x+0.17*tail*tuck,bottom+(top-bottom)*v,sd*z);
        },bodyUV,sd<0);
        // Dark inner arch return, bound to the same fender as the outer lip.
        for(double ax:{1.413,-1.287})
            patch(32,3,REG_WELL,[&](double u,double v) {
                double a=-0.32+(M_PI+0.64)*u,x=ax+0.437*std::cos(a);
                return V3(x,0.355+0.437*std::sin(a),sd*(section(x,0)-0.20*v));
            },bodyUV,sd>0);
    }
    // Shaped nose and tail close the shell, with bumper depth rather than a flat slab.
    for(int sd:{-1,1}) {
        int region=sd>0?REG_FRONT:REG_REAR;
        patch(24,10,region,[&](double u,double v) {
            double z=2*u-1,x=sd*2.313;
            double top=section(x,1)+(section(x,2)-section(x,1))*std::pow(std::fabs(z),2.6);
            double tuck=(1-v)*(1-v);
            double retreat=sd<0 ? 0.17*tuck : 0;
            double width=section(x,0)-(sd<0?0.10:0.05)*tuck;
            return V3(x-sd*(retreat+0.075*std::sin(v*M_PI)*(1-z*z)),0.15+(top-0.15)*v,width*z);
        },[&](double u,double v,const V3&)->Lat {
            return {(float)(sd>0?0:12),(float)(u*4),(float)(v*2)};
        },sd>0);
    }
    // Carbon tub floor closes the underside without filling the wheel openings.
    patch(32,12,REG_UNDER,[&](double u,double v) {
        double x=-2.27+4.54*u;
        double half=std::fabs(x-0.063)<0.89 ? 0.80 : 0.54;
        return V3(x,0.145,(2*v-1)*half);
    },bodyUV,false);
    // Canopy: four curved surfaces with a narrow, arched painted roof.
    auto bezier=[](const V3& a,const V3& b,const V3& c,const V3& d,double t){double r=1-t;return a*(r*r*r)+b*(3*r*r*t)+c*(3*r*t*t)+d*(t*t*t);};
    auto front=[&](double t){return bezier(V3(0.88,0.78,0.73),V3(0.63,1.05,0.68),V3(0.33,1.185,0.59),V3(0.07,1.19,0.56),t);};
    auto rear=[&](double t){return bezier(V3(-1.17,0.87,0.83),V3(-0.91,1.07,0.70),V3(-0.76,1.18,0.59),V3(-0.55,1.19,0.56),t);};
    for(int side:{0,1}) {
        patch(24,16,side?REG_REAR_WINDOW:REG_WINDSHIELD,[&](double u,double v) {
            V3 c=side?rear(v):front(v);
            double z=2*u-1;
            return V3(c.x,c.y+0.02*(1-z*z),c.z*z);
        },[](double u,double v,const V3&)->Lat{return {0,(float)(u*4),(float)(2+v)};},side==0);
    }
    patch(16,20,REG_ROOF,[&](double u,double v){double z=2*v-1;return V3(0.07-0.62*u,1.19+0.02*(1-z*z),0.56*z);},bodyUV,false);
    for(int sd:{-1,1})
        patch(24,16,REG_CABIN_SIDE,[&](double u,double v){
            V3 a=front(v),b=rear(v),p=a+(b-a)*u;
            p.z=sd*(p.z+0.018*std::sin(u*M_PI)*std::sin(v*M_PI));
            return p;
        },[](double u,double v,const V3&)->Lat{return {(float)(3+5*u),0,(float)(2+v)};},sd>0);
}

// ---- Part geometry helpers (car-local coordinates) -------------------------------

enum PartGroup { ON_BODY, ON_ENGINE, ON_GEARBOX, ON_LEFT_DOOR, ON_RIGHT_DOOR, ON_HOOD, ON_LIFTGATE };
struct PartBuilder {
    std::vector<V3> corners;  // 3 per triangle
    std::vector<float> color;  // 4 per triangle
    std::vector<int> group;    // what each triangle rides on
    int current = ON_BODY;
    void tri(const V3& a, const V3& b, const V3& c, const V3& col, int mat) {
        corners.insert(corners.end(), {a, b, c});
        color.insert(color.end(), {(float)col.x, (float)col.y, (float)col.z, (float)mat});
        group.push_back(current);
    }
    void quad(const V3& a, const V3& b, const V3& c, const V3& d, const V3& col, int mat) {
        tri(a, b, c, col, mat);
        tri(a, c, d, col, mat);
    }
    void box(const V3& c, const V3& h, const V3& col, int mat) {
        V3 p[8];
        for (int b = 0; b < 8; b++) p[b] = c + V3((b & 1) ? h.x : -h.x, (b & 2) ? h.y : -h.y, (b & 4) ? h.z : -h.z);
        quad(p[0], p[2], p[3], p[1], col, mat);  // -z
        quad(p[4], p[5], p[7], p[6], col, mat);  // +z
        quad(p[0], p[4], p[6], p[2], col, mat);  // -x
        quad(p[1], p[3], p[7], p[5], col, mat);  // +x
        quad(p[0], p[1], p[5], p[4], col, mat);  // -y
        quad(p[2], p[6], p[7], p[3], col, mat);  // +y
    }
    // Box with its own axes (unit vectors ax, ay, az) and half sizes.
    void obox(const V3& c, const V3& ax, const V3& ay, const V3& az, double hx, double hy, double hz, const V3& col, int mat) {
        V3 p[8];
        for (int b = 0; b < 8; b++)
            p[b] = c + ax * ((b & 1) ? hx : -hx) + ay * ((b & 2) ? hy : -hy) + az * ((b & 4) ? hz : -hz);
        bool flip = dot(cross(ax, ay), az) < 0;
        auto q = [&](int a, int b2, int c2, int d) {
            if (flip) quad(p[a], p[d], p[c2], p[b2], col, mat);
            else quad(p[a], p[b2], p[c2], p[d], col, mat);
        };
        q(0, 2, 3, 1);
        q(4, 5, 7, 6);
        q(0, 4, 6, 2);
        q(1, 3, 7, 5);
        q(0, 1, 5, 4);
        q(2, 6, 7, 3);
    }
    void cylinder(const V3& a, const V3& b, double r, int sides, const V3& col, int mat, bool caps = true) {
        V3 axis = norm(b - a);
        V3 t = norm(cross(axis, std::fabs(axis.y) < 0.9 ? V3(0, 1, 0) : V3(1, 0, 0)));
        V3 s = cross(axis, t);
        for (int k = 0; k < sides; k++) {
            double a0 = 2 * M_PI * k / sides, a1 = 2 * M_PI * (k + 1) / sides;
            V3 d0 = (t * std::cos(a0) + s * std::sin(a0)) * r, d1 = (t * std::cos(a1) + s * std::sin(a1)) * r;
            quad(a + d0, a + d1, b + d1, b + d0, col, mat);
            if (caps) {
                tri(a, a + d1, a + d0, col, mat);
                tri(b, b + d0, b + d1, col, mat);
            }
        }
    }
};

const V3 kBlack(0.04, 0.04, 0.045), kDarkGrey(0.12, 0.12, 0.13), kMetal(0.45, 0.46, 0.48), kChrome(0.8, 0.8, 0.82);

// Bolt-on extras that make each vehicle recognizable: wings, spare wheels, racks...
void buildExtras(const Car& car, PartBuilder& pb) {
    const VehicleSpec& vs = *car.spec;
    const int cells = (int)vs.profile[0].size();
    const double W = vs.width, L = vs.length, R = vs.wheelRadius, roofY = vs.layerY.back();
    auto xAt = [&](double i) { return latticeX(vs, i); };
    auto topAt = [&](int i) { return i < (int)vs.topLine.size() && vs.topLine[i] > 0 ? vs.topLine[i] : vs.layerY[vs.layerY.size() - 2]; };
    const V3 carbon(0.035, 0.035, 0.04);
    int gi0 = cells, gi1 = -1;
    for (int k = 0; k < (int)vs.profile.size(); k++)
        for (int i = 0; i < cells; i++)
            if (vs.profile[k][i] == 'G') {
                gi0 = std::min(gi0, i);
                gi1 = std::max(gi1, i);
            }
    const double roofFront = gi1 >= 0 ? xAt(gi0) - vs.rakeFront : 0, roofRear = gi1 >= 0 ? xAt(gi1 + 1) + vs.rakeRear : 0;

    if (vs.extras & EXTRA_WING) {
        // Rear wing on two swan-neck pylons, tilted nose-down for downforce, with end plates.
        const double xw = -L / 2 + 0.25, yw = roofY + 0.10, span = W * 0.47, chord = 0.25, aoa = 0.16;
        const V3 ax(std::cos(aoa), std::sin(aoa), 0), ay(-std::sin(aoa), std::cos(aoa), 0), az(0, 0, 1);
        if(vs.model == 1) {
            auto wing=[&](double u,double v,double thick) {
                double z=(2*v-1)*span;
                return V3(xw+chord*(2*u-1)+0.10*std::pow(std::fabs(z/span),2),
                          yw+0.045*(2*u-1)-0.035*std::sin(u*M_PI)+0.06*std::pow(std::fabs(z/span),4)+thick,z);
            };
            for(int u=0;u<12;u++)for(int v=0;v<32;v++)for(double t:{-0.009,0.009}) {
                double a=u/12.0,b=(u+1)/12.0,c=v/32.0,d=(v+1)/32.0;
                if(t>0)pb.quad(wing(a,c,t),wing(a,d,t),wing(b,d,t),wing(b,c,t),carbon,SM_PAINT);
                else pb.quad(wing(a,c,t),wing(b,c,t),wing(b,d,t),wing(a,d,t),carbon,SM_PAINT);
            }
        } else {
            pb.obox(V3(xw, yw, 0), ax, ay, az, chord, 0.016, span, carbon, SM_PAINT);
            pb.obox(V3(xw - 0.13, yw + 0.045, 0), ax, ay, az, 0.06, 0.008, span, carbon, SM_PAINT);
        }
        for (int s = -1; s <= 1; s += 2) {
            pb.box(V3(xw - 0.02, yw - 0.02, s * (span + 0.004)), V3(chord + 0.04, 0.09, 0.006), carbon, SM_PAINT);
            // Pylon: up from the engine cover, over the top and down onto the wing.
            const double zp = s * span * 0.42, deck = topAt(cells - 1) - 0.02;
            pb.cylinder(V3(xw + 0.25, deck, zp), V3(xw + 0.12, yw + 0.12, zp), 0.022, 6, carbon, SM_PAINT);
            pb.cylinder(V3(xw + 0.12, yw + 0.12, zp), V3(xw - 0.02, yw + 0.03, zp), 0.022, 6, carbon, SM_PAINT);
        }
    }
    if ((vs.extras & EXTRA_ROOF_SCOOP) && gi1 >= 0 && vs.model != 1) {
        const double x = roofRear + 0.05;
        pb.box(V3(x, roofY + 0.025, 0), V3(0.24, 0.045, 0.13), carbon, SM_PAINT);
        pb.box(V3(x + 0.245, roofY + 0.03, 0), V3(0.005, 0.03, 0.10), kBlack, SM_MATTE);  // the opening
    }
    if (vs.extras & EXTRA_LIP_SPOILER) {
        const double x = vs.liftgate ? roofRear + 0.025 : xAt(cells) + 0.10;
        const double y = vs.liftgate ? roofY + 0.015 : topAt(cells) + 0.025;
        pb.current = ON_LIFTGATE;
        pb.box(V3(x, y, 0), V3(0.09, 0.018, W * 0.42), kBlack, SM_PAINT);
        pb.current = ON_BODY;
    }
    const V3 trim(vs.trimColor[0], vs.trimColor[1], vs.trimColor[2]);
    const V3 steel(0.035, 0.035, 0.038);  // black powder-coated steel
    const V3 rimCol = vs.wheelStyle == WHEEL_STEEL ? V3(0.86, 0.86, 0.83)
                    : vs.wheelStyle == WHEEL_OFFROAD_BRONZE ? V3(0.36, 0.27, 0.16)
                    : vs.wheelStyle == WHEEL_OFFROAD_BLACK  ? V3(0.05, 0.05, 0.055)
                                                            : V3(0.3, 0.3, 0.32);
    const double floorY = vs.layerY[0];
    const bool armor = vs.extras & EXTRA_ARMOR;
    if (armor) {
        // Front: plate-steel bumper on the frame horns, as wide as the flares, ends swept
        // back for clearance; winch in a center cradle with a fairlead, shackles, two lights.
        const double xf = L / 2 + 0.05, y0 = floorY - 0.04, y1 = floorY + 0.36, ym = 0.5 * (y0 + y1);
        const double half = W / 2 + 0.03;
        pb.box(V3(xf, ym + 0.04, 0), V3(0.12, (y1 - y0) / 2 - 0.04, half * 0.62), steel, SM_MATTE);
        for (int sd = -1; sd <= 1; sd += 2) {
            const V3 ax = norm(V3(0.6, 0, -sd * 1.0)), ay(0, 1, 0), az = norm(cross(ax, ay));
            pb.obox(V3(xf - 0.12, ym + 0.08, sd * half * 0.8), ax, ay, az, 0.2, (y1 - y0) / 2 - 0.08, 0.06, steel, SM_MATTE);
            pb.box(V3(xf + 0.13, y0 + 0.1, sd * 0.32), V3(0.02, 0.05, 0.012), V3(0.55, 0.55, 0.57), SM_METAL);  // shackle tab
            pb.box(V3(xf + 0.17, y0 + 0.06, sd * 0.32), V3(0.03, 0.035, 0.008), V3(0.6, 0.6, 0.62), SM_METAL);  // D-ring
            pb.box(V3(xf + 0.06, y1 + 0.03, sd * 0.42), V3(0.04, 0.045, 0.06), steel, SM_MATTE);  // LED light cube
            pb.box(V3(xf + 0.101, y1 + 0.03, sd * 0.42), V3(0.002, 0.035, 0.048), V3(1.0, 0.98, 0.9), SM_EMISSIVE);
        }
        pb.box(V3(xf + 0.03, y0 + 0.03, 0), V3(0.1, 0.05, half * 0.5), steel, SM_MATTE);  // bash plate
        pb.cylinder(V3(xf + 0.06, ym + 0.02, -0.24), V3(xf + 0.06, ym + 0.02, 0.24), 0.07, 12, V3(0.08, 0.08, 0.09), SM_METAL);  // winch
        pb.cylinder(V3(xf + 0.06, ym + 0.02, -0.06), V3(xf + 0.06, ym + 0.02, 0.06), 0.077, 12, V3(0.75, 0.72, 0.62), SM_MATTE);  // rope
        pb.box(V3(xf + 0.125, ym - 0.06, 0), V3(0.012, 0.03, 0.1), V3(0.55, 0.55, 0.57), SM_METAL);  // fairlead
        // Rear: steel bumper with a hitch receiver and the tire carrier's swing post.
        const double xr = -L / 2 - 0.07;
        pb.box(V3(xr, ym + 0.02, 0), V3(0.1, (y1 - y0) / 2 - 0.06, half * 0.98), steel, SM_MATTE);
        pb.box(V3(xr - 0.13, y0 + 0.1, 0), V3(0.05, 0.035, 0.035), V3(0.08, 0.08, 0.08), SM_METAL);  // hitch receiver
        for (int sd = -1; sd <= 1; sd += 2)
            pb.box(V3(xr - 0.101, ym + 0.08, sd * half * 0.82), V3(0.002, 0.03, 0.06), V3(0.8, 0.05, 0.03), SM_EMISSIVE);
    }
    if ((vs.extras & EXTRA_WINCH) && !armor && vs.model == 2) {
        // Winch tucked behind the Bronco's steel bumper: hawse fairlead and the hook.
        const double xf = L / 2 + 0.052, y = floorY + 0.13;
        pb.box(V3(xf, y, 0), V3(0.008, 0.032, 0.11), V3(0.6, 0.6, 0.62), SM_METAL);
        pb.box(V3(xf + 0.006, y, 0), V3(0.004, 0.012, 0.06), V3(0.03, 0.03, 0.03), SM_MATTE);  // slot
        pb.box(V3(xf + 0.03, y - 0.01, 0), V3(0.02, 0.025, 0.01), V3(0.75, 0.12, 0.06), SM_MATTE);  // hook strap
    }
    if (vs.extras & EXTRA_SPARE) {
        // Full-size spare on the swing gate: tire, wheel and its mount.
        // (With armor: on the bumper's swing-out carrier, off to the left.)
        const double tw = vs.wheelWidth, x = armor ? -L / 2 - 0.2 - tw / 2 : -L / 2 - 0.16;
        const double y = armor ? floorY + 0.36 + R * 0.55 : vs.layerY[1] + 0.12, z = armor ? -W * 0.2 : 0;
        const double rimR = vs.wheelStyle == WHEEL_ALLOY ? R * 0.55 : R * 0.49;
        if (!vs.wheelLoss) {  // (the trucks' spare is drawn where it really is, and only while it's there: render.cpp)
            pb.cylinder(V3(x - tw / 2, y, z), V3(x + tw / 2, y, z), R, 20, V3(0.03, 0.03, 0.03), SM_MATTE);
            pb.cylinder(V3(x - tw / 2 - 0.004, y, z), V3(x - tw / 2 + 0.02, y, z), rimR, 16, rimCol, SM_METAL);
        }
        if (armor) {
            // Carrier arm: a post at the left corner, a beam across, a mount plate.
            const double xa = -L / 2 - 0.16, ya = floorY + 0.36;
            pb.box(V3(xa, ya + 0.12, -W / 2 + 0.12), V3(0.035, 0.14, 0.035), steel, SM_MATTE);
            pb.box(V3(xa, ya + 0.24, -W * 0.12), V3(0.035, 0.035, W * 0.33), steel, SM_MATTE);
            pb.box(V3(x + tw / 2 + 0.02, y, z), V3(0.03, 0.08, 0.08), steel, SM_MATTE);
        } else {
            pb.box(V3(-L / 2 - 0.015, y, 0), V3(0.02, 0.08, 0.08), kDarkGrey, SM_MATTE);
        }
    }
    if (vs.extras & EXTRA_FLARES) {
        // Squared-off black fender flares around each wheel opening.
        for (int ax : axleColumns(vs)) {
            const double xw = xAt(ax);
            if (hasBodyShell(vs)) {
                // On a sculpted shell they follow its arch cut, standing out from the side.
                double cy, rr;
                shellArch(vs, cy, rr);
                const double grow = 0.04, rw = rr + grow;
                const int n = 20;
                for (int s = -1; s <= 1; s += 2)
                    for (int k = 0; k < n; k++) {
                        double d0 = -rw + 2 * rw * k / n, d1 = -rw + 2 * rw * (k + 1) / n;
                        double y0 = shellArchHeight(vs, d0 * rr / rw) + grow, y1 = shellArchHeight(vs, d1 * rr / rw) + grow;
                        y0 = std::max(y0, floorY + 0.02), y1 = std::max(y1, floorY + 0.02);
                        V3 p0(xw + d0, y0, s * (W / 2 + 0.02)), p1(xw + d1, y1, s * (W / 2 + 0.02));
                        if (len(p1 - p0) < 1e-4) continue;
                        V3 d = norm(p1 - p0), c = (p0 + p1) * 0.5;
                        pb.obox(c, d, cross(V3(0, 0, 1), d), V3(0, 0, 1), len(p1 - p0) * 0.52, 0.055, 0.035,
                                vs.trimTop > 0 ? trim : kBlack, vs.trimTop > 0 ? SM_PAINT : SM_MATTE);
                    }
                continue;
            }
            double cy = R, rr = R + 0.09;
            for (int s = -1; s <= 1; s += 2)
                for (int k = 0; k < 16; k++) {
                    double a0 = M_PI * k / 16, a1 = M_PI * (k + 1) / 16;
                    V3 p0(xw + std::cos(a0) * rr, cy + std::sin(a0) * rr, s * (W / 2 + 0.015));
                    V3 p1(xw + std::cos(a1) * rr, cy + std::sin(a1) * rr, s * (W / 2 + 0.015));
                    V3 d = norm(p1 - p0), c = (p0 + p1) * 0.5;
                    pb.obox(c, d, cross(V3(0, 0, 1), d), V3(0, 0, 1), len(p1 - p0) * 0.52, 0.05, 0.03, vs.trimTop > 0 ? trim : kBlack,
                            vs.trimTop > 0 ? SM_PAINT : SM_MATTE);
                }
        }
    }
    if ((vs.extras & EXTRA_ROOF_RACK) && gi1 >= 0) {
        const double x0 = roofFront - 0.15, x1 = roofRear + 0.08, y = roofY + 0.05;
        for (int s = -1; s <= 1; s += 2) {
            const double z = s * (W / 2 - vs.roofTuck - 0.12);
            pb.box(V3((x0 + x1) / 2, y, z), V3(std::fabs(x1 - x0) / 2, 0.015, 0.025), kBlack, SM_MATTE);
            pb.box(V3(x0 + 0.05, y - 0.03, z), V3(0.04, 0.03, 0.025), kBlack, SM_MATTE);  // feet
            pb.box(V3(x1 - 0.05, y - 0.03, z), V3(0.04, 0.03, 0.025), kBlack, SM_MATTE);
        }
        for (double t : {0.3, 0.7}) pb.box(V3(x0 + (x1 - x0) * t, y + 0.02, 0), V3(0.02, 0.012, W / 2 - vs.roofTuck - 0.1), kDarkGrey, SM_METAL);
    }
    if ((vs.extras & EXTRA_ROOF_BASKET) && gi1 >= 0) {
        // Full-length basket: feet on the roof, a tube frame with side rails, a slatted
        // floor, a wind deflector, and a row of driving lights across the front.
        const double x0 = roofFront - 0.02, x1 = roofRear + 0.02, y = roofY + 0.07, hz = W / 2 - vs.roofTuck - 0.04;  // x0 front
        const double cx = 0.5 * (x0 + x1), hx = 0.5 * (x0 - x1);
        for (int sd = -1; sd <= 1; sd += 2) {
            pb.box(V3(cx, y, sd * hz), V3(hx, 0.014, 0.014), steel, SM_MATTE);           // base rail
            pb.box(V3(cx, y + 0.12, sd * hz), V3(hx, 0.012, 0.012), steel, SM_MATTE);  // top rail
            for (int k = 0; k <= 6; k++)
                pb.box(V3(x1 + (x0 - x1) * k / 6, y + 0.06, sd * hz), V3(0.01, 0.06, 0.01), steel, SM_MATTE);
            for (double f : {0.08, 0.5, 0.92})
                pb.box(V3(x0 + (x1 - x0) * f, y - 0.04, sd * hz), V3(0.03, 0.04, 0.02), steel, SM_MATTE);  // feet
        }
        for (int k = 0; k <= 8; k++) pb.box(V3(x0 + (x1 - x0) * k / 8, y, 0), V3(0.012, 0.01, hz), steel, SM_MATTE);  // floor slats
        for (double xe : {x0, x1}) pb.box(V3(xe, y + 0.12, 0), V3(0.012, 0.012, hz), steel, SM_MATTE);
        const V3 ax = norm(V3(0.5, -1, 0)), az(0, 0, 1);
        pb.obox(V3(x0 + 0.03, y + 0.06, 0), ax, cross(az, ax), az, 0.07, 0.005, hz, steel, SM_MATTE);  // wind deflector
        for (int l = 0; l < 4; l++) {  // amber driving lights
            const double z = (l - 1.5) * 0.24;
            pb.box(V3(x0 - 0.02, y + 0.19, z), V3(0.05, 0.05, 0.05), steel, SM_MATTE);
            pb.box(V3(x0 + 0.031, y + 0.19, z), V3(0.002, 0.04, 0.04), V3(1.0, 0.75, 0.15), SM_EMISSIVE);
        }
    }
    if ((vs.extras & EXTRA_SNORKEL) && gi1 >= 0) {
        // Up the right-hand A-pillar from the fender to above the roof, ram head facing forward.
        const double z = W / 2 + 0.03, xb = xAt(gi0) + 0.18, yb = topAt(gi0 - 1) - 0.15, yt = roofY + 0.14;
        const V3 black(0.03, 0.03, 0.03);
        pb.box(V3(xb + 0.05, yb + 0.05, z - 0.01), V3(0.12, 0.06, 0.04), black, SM_MATTE);  // body into the fender
        const V3 top(xb - vs.rakeFront * 0.85, yt, z);
        pb.cylinder(V3(xb, yb, z), top, 0.045, 10, black, SM_MATTE);
        pb.obox(top + V3(0.06, 0.03, 0), V3(1, 0, 0), V3(0, 1, 0), V3(0, 0, 1), 0.1, 0.06, 0.055, black, SM_MATTE);
        pb.box(top + V3(0.161, 0.03, 0), V3(0.002, 0.045, 0.04), kDarkGrey, SM_MATTE);  // intake grille
        for (double f : {0.35, 0.75}) {  // pillar brackets
            V3 q = V3(xb, yb, z) + (top - V3(xb, yb, z)) * f;
            pb.box(q - V3(0, 0, 0.03), V3(0.02, 0.02, 0.03), black, SM_MATTE);
        }
    }
    if (vs.extras & EXTRA_SLIDERS) {
        // Rock sliders: a main rail under each sill and a kick-out tube, bolted to the frame.
        const auto ac = axleColumns(vs);
        const double xa = xAt(ac.front()) - R - 0.12, xb = xAt(ac.back()) + R + 0.12;
        for (int sd = -1; sd <= 1; sd += 2) {
            const double z = sd * (W / 2 - 0.03), y = floorY - 0.03;
            pb.box(V3(0.5 * (xa + xb), y, z), V3(0.5 * (xa - xb), 0.04, 0.05), steel, SM_MATTE);
            pb.cylinder(V3(xa, y + 0.02, z + sd * 0.06), V3(xb, y + 0.02, z + sd * 0.06), 0.022, 8, steel, SM_MATTE);
            for (double f : {0.15, 0.5, 0.85}) {
                const double x = xb + (xa - xb) * f;
                pb.box(V3(x, y + 0.02, z + sd * 0.03), V3(0.015, 0.015, 0.035), steel, SM_MATTE);
            }
        }
    }
    if (vs.extras & EXTRA_HOOD_SCOOP) {
        const double x = xAt(1.7), y = topAt(2) - 0.01;
        pb.box(V3(x, y + 0.03, 0), V3(0.2, 0.035, 0.26), kBlack, SM_MATTE);
        pb.box(V3(x + 0.2, y + 0.035, 0), V3(0.004, 0.022, 0.22), V3(0.01, 0.01, 0.01), SM_MATTE);
    }
    if (vs.extras & EXTRA_SKID_PLATE) {
        const double x = xAt(1.2);
        pb.box(V3(x, vs.layerY[0] - 0.03, 0), V3(0.45, 0.01, W * 0.3), V3(0.25, 0.25, 0.27), SM_METAL);
    }
}

// Coachwork details are geometry, with local bindings so dents carry them along.
void buildCoachwork(const Car& car, PartBuilder& pb) {
    const VehicleSpec& v = *car.spec;
    if (v.model < 0) return;
    const double L=v.length, W=v.width, floor=v.model==1 ? 0.11 : v.layerY[0];
    const size_t firstCorner=pb.corners.size();
    const V3 carbon(0.035,0.038,0.044), rubber(0.035,0.035,0.037);
    if (v.model == 1) {
        // Swept splitter and canards, vented side sills, diffuser and center exhaust.
        const double xf=L/2-0.04, xr=-L/2+0.08;
        for (int sd : {-1,1}) {
            pb.quad(V3(xf+0.09,floor+0.025,0),V3(xf-0.03,floor+0.025,sd*W*0.44),
                    V3(xf-0.45,floor+0.025,sd*W*0.48),V3(xf-0.36,floor+0.025,0),carbon,SM_MATTE);
            for (double y : {floor+0.18,floor+0.28})
                pb.obox(V3(xf-0.32,y,sd*W*0.475),norm(V3(1,0.15,0)),norm(V3(-0.15,1,0)),V3(0,0,1),0.18,0.008,0.075,carbon,SM_MATTE);
            pb.box(V3(-0.08,floor+0.03,sd*W*0.46),V3(0.94,0.035,0.08),carbon,SM_MATTE);
            // Wheel-top extraction louvres.
            pb.current=ON_HOOD;
            for (int k=0;k<5;k++)
                pb.box(jeskoHoodPoint(1.47-0.06*k,sd*0.89),V3(0.014,0.004,0.045),carbon,SM_MATTE);
            pb.current=ON_BODY;
        }
        pb.current=ON_HOOD;
        for (int sd : {-1,1}) {
            // Teardrop lenses follow the curved fender, including both LED strips.
            for(int j=0;j<12;j++) {
                double u=j/12.0,v=(j+1)/12.0;
                auto point=[&](double t,double w) {
                    double width=0.02+0.055*std::sin(t*M_PI*0.85);
                    return jeskoHoodPoint(2.07-0.44*t,sd*(0.64+0.225*t+width*w));
                };
                V3 a=point(u,0),b=point(v,0),c=point(v,1),d=point(u,1);
                if(sd>0)pb.quad(a,b,c,d,carbon,SM_PAINT);
                else pb.quad(a,d,c,b,carbon,SM_PAINT);
                for(double w:{0.23,0.72})pb.cylinder(point(u,w)+V3(0,0.004,0),point(v,w)+V3(0,0.004,0),0.005,6,V3(0.9,0.96,1.0),SM_EMISSIVE,false);
            }
        }
        pb.current=ON_BODY;
        for (int k=-3;k<=3;k++)
            pb.obox(V3(xr+0.14,floor+0.05,k*0.21),norm(V3(1,-0.20,0)),norm(V3(0.20,1,0)),V3(0,0,1),0.32,0.065,0.007,carbon,SM_MATTE);
        // Single wide hexagonal center outlet, with a thin metal lip and a dark bore.
        for(int i=0;i<6;i++) {
            auto rim=[&](int j,double x,double scale) {
                double a=j*M_PI/3;
                return V3(x,0.38+0.083*scale*std::sin(a),0.17*scale*std::cos(a));
            };
            V3 a=rim(i,xr-0.07,1),b=rim(i+1,xr-0.07,1);
            V3 c=rim(i+1,xr-0.075,0.85),d=rim(i,xr-0.075,0.85);
            pb.quad(a,b,c,d,kMetal,SM_METAL);
            pb.quad(c,rim(i+1,xr+0.06,0.8),rim(i,xr+0.06,0.8),d,kBlack,SM_MATTE);
            pb.tri(V3(xr+0.06,0.38,0),rim(i,xr+0.06,0.8),rim(i+1,xr+0.06,0.8),kBlack,SM_MATTE);
        }
    } else if (v.model == 0) {
        // Thin 1990 bumpers, front air dam and rub strips; no modern giant grille.
        for (int sd : {-1,1}) {
            pb.box(V3(sd*(L/2-0.035),0.405,0),V3(0.055,0.067,W*0.44),rubber,SM_MATTE);
            pb.box(V3(-0.05,0.645,sd*(W/2+0.006)),V3(1.00,0.022,0.008),rubber,SM_MATTE);
            pb.box(V3(1.10,0.30,sd*(W/2-0.05)),V3(0.016,0.085,0.07),rubber,SM_MATTE);
        }
        pb.box(V3(L/2-0.05,0.26,0),V3(0.06,0.028,W*0.43),rubber,SM_MATTE);
        // Sunroof inset in the roof, narrow glass seal (the sculpted shell draws its own).
        if (!hasBodyShell(v)) pb.box(V3(-0.10,v.layerY.back()+0.004,0),V3(0.31,0.004,0.34),rubber,SM_MATTE);
    } else if (v.model == 2) {
        // Hardtop channels and roof panel joints; squared off-road bumper and skid.
        for (double x : {-0.3,-1.05})
            pb.box(V3(x,v.layerY.back()+0.008,0),V3(0.006,0.007,W*0.44),rubber,SM_MATTE);
        pb.box(V3(L/2-0.025,floor+0.13,0),V3(0.075,0.12,W*0.45),rubber,SM_MATTE);
        for (int sd : {-1,1}) {
            pb.box(V3(0,floor+0.02,sd*(W/2+0.015)),V3(1.0,0.045,0.05),rubber,SM_MATTE);
            pb.box(V3(L/2-0.03,floor+0.17,sd*0.64),V3(0.08,0.045,0.09),V3(0.14,0.14,0.15),SM_METAL);
        }
    }
    if(v.model==1)for(size_t i=firstCorner;i<pb.corners.size();i++)pb.corners[i].y+=v.layerY[0]-0.11;
}

// ---- Tube-frame vehicles (rock buggy, side-by-side) --------------------------------
// The lattice is the chassis and cage and isn't drawn; the tubes, seats and bumpers here
// are parts bound to it, so a hit bends them with the frame. Only the 'p' cells carry
// body panels (see VehicleSpec::openFrame).
void buildOpenFrame(const Car& car, PartBuilder& pb) {
    const VehicleSpec& vs = *car.spec;
    const int cells = (int)vs.profile[0].size();
    const double W = vs.width, L = vs.length, hz = W / 2, F = vs.layerY[0], roofY = vs.layerY.back();
    auto xAt = [&](double i) { return latticeX(vs, i); };
    int gi0 = cells, gi1 = -1;
    for (const std::string& row : vs.profile)
        for (int i = 0; i < cells; i++)
            if (row[i] == 'G') gi0 = std::min(gi0, i), gi1 = std::max(gi1, i);
    const double cabF = xAt(gi0), cabR = xAt(gi1 + 1), Lh = L / 2;
    const std::vector<int> ax = axleColumns(vs);
    const double xf = xAt(ax.front()), xr = xAt(ax.back());
    const V3 tubeCol = vs.model == 4 ? V3(0.045, 0.045, 0.05) : V3(0.035, 0.035, 0.04);  // powder coat
    const V3 alu(0.42, 0.43, 0.45), seatCol(0.07, 0.07, 0.075), belt(0.75, 0.08, 0.05);
    const V3 paint(car.color);
    // A tube from a to b (and its mirror image across the car).
    auto T = [&](const V3& a, const V3& b, double r = 0.022, const V3& col = V3(-1, -1, -1)) {
        const V3 c = col.x < 0 ? tubeCol : col;
        pb.cylinder(a, b, r * (r > 0.01 ? 1.3 : 1.0), 6, c, SM_MATTE);  // (chromoly tube is 1.75 in across)
    };
    auto M = [&](const V3& a, const V3& b, double r = 0.022, const V3& col = V3(-1, -1, -1)) {
        T(a, b, r, col);
        T(V3(a.x, a.y, -a.z), V3(b.x, b.y, -b.z), r, col);
    };
    auto across = [&](double x, double y, double z, double r = 0.022) { T(V3(x, y, -z), V3(x, y, z), r); };
    // A tube path, joined end to end (with a small ball at each joint for a clean weld).
    auto path = [&](std::initializer_list<V3> pts, double r = 0.022, bool mirror = true) {
        const V3* prev = nullptr;
        for (const V3& p : pts) {
            if (prev) mirror ? M(*prev, p, r) : T(*prev, p, r);
            prev = &p;
        }
    };
    const double ey = vs.engineY;
    const bool buggy = vs.model == 4;

    if (buggy) {
        const double cz = 0.64, rz = 0.50;               // cage half width low and at the roof
        const double sill = F + 0.12;                    // top of the main rails
        // Roll cage: A-pillars, main hoop, roof, door bars, rear stays.
        const double xA = cabF + 0.03, xAt_ = cabF - 0.34, xB = cabR + 0.14, xBt = cabR + 0.06;
        M(V3(xA, sill, cz), V3(xAt_, roofY - 0.02, rz));
        M(V3(xB, sill, cz), V3(xBt, roofY - 0.02, rz));
        M(V3(xAt_, roofY - 0.02, rz), V3(xBt, roofY - 0.02, rz));  // roof rails
        across(xAt_, roofY - 0.02, rz);
        across(0.5 * (xAt_ + xBt), roofY - 0.02, rz);
        across(xBt, roofY - 0.02, rz);
        // Roof X brace and halo
        T(V3(xAt_, roofY - 0.02, -rz), V3(xBt, roofY - 0.02, rz), 0.016);
        T(V3(xAt_, roofY - 0.02, rz), V3(xBt, roofY - 0.02, -rz), 0.016);
        // Door bars (and a diagonal), the dash bar and the harness bar behind the seats.
        for (double y : {sill + 0.30, sill + 0.62}) {
            const double t = (y - sill) / (roofY - 0.02 - sill);
            M(V3(xA + (xAt_ - xA) * t, y, cz + (rz - cz) * t), V3(xB + (xBt - xB) * t, y, cz + (rz - cz) * t));
        }
        M(V3(xA - 0.02, sill + 0.30, cz + 0.01), V3(xB, sill + 0.62, cz - 0.04), 0.016);
        across(xA + 0.02, sill + 0.66, cz - 0.05);
        across(xB + 0.02, sill + 0.78, cz - 0.05);
        across(xB + 0.04, sill + 0.30, cz);
        // Rear stays from the roof down to the frame behind the axle, and the tail hoop.
        const double xTail = -Lh + 0.30;
        path({V3(xBt, roofY - 0.02, rz), V3(xr + 0.25, F + 0.65, 0.58), V3(xTail, F + 0.40, 0.55)}, 0.022);
        across(xr + 0.25, F + 0.65, 0.58);
        across(xTail, F + 0.40, 0.55);
        // Main rails (the lattice's floor members are drawn under the car by the running gear).
        M(V3(xA + 0.06, sill, 0.34), V3(xB, sill, 0.34), 0.03);
        M(V3(xB, sill, 0.34), V3(xTail, F + 0.30, 0.34), 0.03);
        // Front: horns, shock hoops over the axle, the tubular bumper, the winch plate.
        const double xH = Lh - 0.10;
        M(V3(xA + 0.06, sill, 0.34), V3(xf + 0.18, F + 0.35, 0.34), 0.03);
        M(V3(xf + 0.18, F + 0.35, 0.34), V3(xH, F + 0.30, 0.30), 0.03);
        const double sy = (vs.wheelRadius + vs.coilTop) + 0.07;  // coil-over tops
        const double shz = 0.725;
        for (double x : {xf + 0.06, xr - 0.06}) {
            M(V3(x, F + 0.30, 0.38), V3(x, sy, shz - 0.02), 0.022);
            M(V3(x - (x > 0 ? 0.20 : -0.20), F + 0.32, 0.45), V3(x, sy, shz - 0.02), 0.018);
            across(x, sy, shz - 0.02, 0.026);
        }
        M(V3(xf + 0.06, sy, shz - 0.02), V3(xA, roofY - 0.52, cz), 0.022);  // shock hoop to the A-pillar
        M(V3(xH, F + 0.30, 0.30), V3(xH + 0.06, F + 0.22, 0.62), 0.03);      // bumper wings
        across(xH + 0.06, F + 0.22, 0.62, 0.032);
        M(V3(xH + 0.06, F + 0.22, 0.62), V3(xH - 0.12, F + 0.70, 0.56), 0.024);
        across(xH - 0.12, F + 0.70, 0.56, 0.024);
        M(V3(xH - 0.12, F + 0.70, 0.56), V3(xf + 0.06, sy, shz - 0.02), 0.022);
        // Winch between the horns, with its fairlead.
        pb.box(V3(xH - 0.04, F + 0.30, 0), V3(0.10, 0.07, 0.24), V3(0.08, 0.08, 0.09), SM_METAL);
        pb.cylinder(V3(xH - 0.04, F + 0.30, -0.19), V3(xH - 0.04, F + 0.30, 0.19), 0.065, 12, V3(0.10, 0.10, 0.11), SM_METAL);
        pb.cylinder(V3(xH - 0.04, F + 0.30, -0.08), V3(xH - 0.04, F + 0.30, 0.08), 0.072, 12, V3(0.75, 0.72, 0.62), SM_MATTE);
        pb.box(V3(xH + 0.08, F + 0.27, 0), V3(0.012, 0.03, 0.1), V3(0.6, 0.6, 0.62), SM_METAL);
        // Rear bumper and tow loop.
        const double xRb = -Lh + 0.04;
        across(xRb, F + 0.35, 0.60, 0.03);
        M(V3(xRb, F + 0.35, 0.60), V3(xTail, F + 0.40, 0.55), 0.026);
        M(V3(xRb, F + 0.35, 0.60), V3(xRb, F + 0.75, 0.50), 0.024);
        across(xRb, F + 0.75, 0.50, 0.024);
        // Light bar on the A-pillar bar, and pods on the bumper.
        const double lx = xAt_ - 0.02;
        pb.box(V3(lx, roofY + 0.04, 0), V3(0.035, 0.032, 0.52), V3(0.03, 0.03, 0.035), SM_MATTE);
        pb.box(V3(lx + 0.037, roofY + 0.04, 0), V3(0.002, 0.024, 0.50), V3(1.0, 0.98, 0.9), SM_EMISSIVE);
        for (int s = -1; s <= 1; s += 2) {
            pb.box(V3(xH - 0.10, F + 0.62, s * 0.50), V3(0.035, 0.05, 0.06), V3(0.03, 0.03, 0.035), SM_MATTE);
            pb.box(V3(xH - 0.063, F + 0.62, s * 0.50), V3(0.002, 0.04, 0.05), V3(1.0, 0.98, 0.9), SM_EMISSIVE);
        }
        // Seats (bucket seats with harnesses), steering wheel and column, dash and gauges.
        const double sx = cabR + 0.55, szz = 0.30, sh = F + 0.20;
        for (int s = -1; s <= 1; s += 2) {
            pb.box(V3(sx, sh, s * szz), V3(0.24, 0.05, 0.20), seatCol, SM_MATTE);
            pb.obox(V3(sx - 0.24, sh + 0.30, s * szz), norm(V3(1, 0.22, 0)), norm(V3(-0.22, 1, 0)), V3(0, 0, 1), 0.05, 0.33, 0.20, seatCol, SM_MATTE);
            pb.box(V3(sx - 0.30, sh + 0.68, s * szz), V3(0.05, 0.08, 0.13), seatCol, SM_MATTE);
            pb.box(V3(sx - 0.20, sh + 0.30, s * (szz - 0.12)), V3(0.012, 0.28, 0.025), belt, SM_MATTE);
            pb.box(V3(sx - 0.20, sh + 0.30, s * (szz + 0.12)), V3(0.012, 0.28, 0.025), belt, SM_MATTE);
        }
        const V3 wc(cabF - 0.38, F + 0.58, -szz);
        for (int k = 0; k < 14; k++) {
            const double a0 = 2 * M_PI * k / 14, a1 = 2 * M_PI * (k + 1) / 14, R = 0.17, tilt = 0.5;
            auto ring = [&](double a) { return wc + V3(std::sin(a) * R * std::sin(tilt), std::sin(a) * R * std::cos(tilt), std::cos(a) * R); };
            pb.cylinder(ring(a0), ring(a1), 0.016, 5, kBlack, SM_MATTE, false);
        }
        pb.cylinder(wc, wc + V3(0.36, -0.14, 0), 0.022, 6, kDarkGrey, SM_MATTE);
        pb.box(V3(cabF - 0.10, F + 0.52, 0), V3(0.05, 0.09, 0.62), V3(0.04, 0.04, 0.045), SM_MATTE);  // dash
        pb.box(V3(cabF - 0.15, F + 0.64, 0), V3(0.012, 0.04, 0.30), V3(0.05, 0.4, 0.05), SM_EMISSIVE);  // gauge strip
        // Floor and firewall panels (aluminum), and the tail panel's fuel cell.
        pb.box(V3(cabF - 0.55, F + 0.02, 0), V3(0.85, 0.008, 0.56), alu, SM_METAL);
        pb.box(V3(cabF + 0.02, F + 0.38, 0), V3(0.008, 0.36, 0.62), alu, SM_METAL);
        pb.box(V3(xTail + 0.10, F + 0.34, 0), V3(0.36, 0.24, 0.40), V3(0.07, 0.07, 0.08), SM_MATTE);   // fuel cell
        pb.box(V3(xTail + 0.10, F + 0.60, 0), V3(0.36, 0.01, 0.40), alu, SM_METAL);
        // Side-exit exhaust from the headers back along the left sill, with a turndown.
        const double exZ = -0.43;
        pb.cylinder(V3(vs.engineX - 0.10, ey - 0.18, exZ + 0.12), V3(vs.engineX - 0.45, F + 0.14, exZ), 0.04, 8, kMetal, SM_METAL, false);
        pb.cylinder(V3(vs.engineX - 0.45, F + 0.14, exZ), V3(xr + 0.5, F + 0.14, exZ), 0.04, 8, kMetal, SM_METAL, false);
        pb.box(V3(xr + 0.8, F + 0.14, exZ), V3(0.30, 0.07, 0.07), V3(0.3, 0.3, 0.31), SM_METAL);
        pb.cylinder(V3(xr + 0.5, F + 0.14, exZ), V3(xr + 0.2, F + 0.06, exZ), 0.042, 8, kChrome, SM_CHROME);
    } else {
        // Side-by-side: a cage like a six-point hoop, door nets, a bumper tube, shock towers.
        const double cz = 0.60, rz = 0.48, sill = F + 0.32;
        const double xA = cabF + 0.02, xAt_ = cabF - 0.50, xB = cabR + 0.06, xBt = cabR - 0.02;
        M(V3(xA, sill, cz), V3(xAt_, roofY - 0.02, rz));
        M(V3(xB, sill, cz), V3(xBt, roofY - 0.02, rz));
        M(V3(xAt_, roofY - 0.02, rz), V3(xBt, roofY - 0.02, rz));
        across(xAt_, roofY - 0.02, rz);
        across(xBt, roofY - 0.02, rz);
        across(0.5 * (xAt_ + xBt), roofY - 0.02, rz, 0.018);
        // Door frames: two bars and the net's diagonal strings.
        for (double y : {sill + 0.26, sill + 0.56}) {
            const double t = (y - sill) / (roofY - 0.02 - sill);
            M(V3(xA + (xAt_ - xA) * t, y, cz + (rz - cz) * t), V3(xB + (xBt - xB) * t, y, cz + (rz - cz) * t), 0.02);
        }
        for (int k = 1; k < 5; k++) {
            const double f = k / 5.0;
            const double x0 = xA + (xB - xA) * f, x1 = xA + (xB - xA) * (f + 0.12);
            M(V3(x0, sill + 0.28, cz), V3(x1, sill + 0.54, cz), 0.006, V3(0.02, 0.02, 0.02));
        }
        // Rear: the engine cage behind the seats and the tail.
        const double xTail = -Lh + 0.12;
        M(V3(xBt, roofY - 0.02, rz), V3(xr + 0.20, F + 0.78, 0.55), 0.022);
        M(V3(xr + 0.20, F + 0.78, 0.55), V3(xTail, F + 0.55, 0.50), 0.022);
        across(xr + 0.20, F + 0.78, 0.55);
        across(xTail, F + 0.55, 0.50);
        M(V3(xB, sill, cz), V3(xr + 0.20, F + 0.78, 0.55), 0.02);
        // Main frame rails, front bumper, towers.
        M(V3(xA + 0.05, F + 0.10, 0.34), V3(xTail, F + 0.20, 0.34), 0.028);
        const double xH = Lh - 0.03;
        M(V3(xA + 0.05, F + 0.10, 0.34), V3(xH, F + 0.14, 0.34), 0.028);
        M(V3(xH, F + 0.14, 0.34), V3(xH + 0.02, F + 0.30, 0.55), 0.03);
        across(xH + 0.02, F + 0.30, 0.55, 0.03);
        across(xH + 0.01, F + 0.14, 0.34, 0.028);
        M(V3(xH + 0.02, F + 0.30, 0.55), V3(xH - 0.15, F + 0.62, 0.45), 0.024);
        across(xH - 0.15, F + 0.62, 0.45, 0.024);
        const double sy = (vs.wheelRadius + 0.46 * vs.wheelRadius / 0.32) + 0.06;
        for (double x : {xf, xr}) {
            M(V3(x, F + 0.12, 0.34), V3(x, sy, 0.50), 0.022);
            M(V3(x + (x > 0 ? 0.20 : -0.20), F + 0.14, 0.34), V3(x, sy, 0.50), 0.018);
            across(x, sy, 0.50, 0.024);
        }
        M(V3(xf, sy, 0.50), V3(xA, sill + 0.55, cz), 0.022);
        // LED bar on the cage's front crossbar, pods in the hood, roof panel.
        pb.box(V3(xAt_ - 0.02, roofY + 0.03, 0), V3(0.03, 0.03, 0.46), V3(0.03, 0.03, 0.035), SM_MATTE);
        pb.box(V3(xAt_ + 0.012, roofY + 0.03, 0), V3(0.002, 0.022, 0.44), V3(1.0, 0.98, 0.9), SM_EMISSIVE);
        pb.box(V3(0.5 * (xAt_ + xBt), roofY + 0.012, 0), V3(0.5 * (xAt_ - xBt), 0.012, rz + 0.04), paint, SM_PAINT);
        // Seats, wheel, dash.
        const double sx = cabR + 0.52, szz = 0.28, sh = F + 0.20;
        for (int s = -1; s <= 1; s += 2) {
            pb.box(V3(sx, sh, s * szz), V3(0.22, 0.05, 0.20), seatCol, SM_MATTE);
            pb.obox(V3(sx - 0.22, sh + 0.28, s * szz), norm(V3(1, 0.20, 0)), norm(V3(-0.20, 1, 0)), V3(0, 0, 1), 0.05, 0.30, 0.20, seatCol, SM_MATTE);
            pb.box(V3(sx - 0.28, sh + 0.62, s * szz), V3(0.05, 0.08, 0.13), seatCol, SM_MATTE);
            pb.box(V3(sx - 0.18, sh + 0.28, s * (szz - 0.11)), V3(0.012, 0.26, 0.025), belt, SM_MATTE);
            pb.box(V3(sx - 0.18, sh + 0.28, s * (szz + 0.11)), V3(0.012, 0.26, 0.025), belt, SM_MATTE);
        }
        const V3 wc(cabF - 0.34, F + 0.62, -szz);
        for (int k = 0; k < 14; k++) {
            const double a0 = 2 * M_PI * k / 14, a1 = 2 * M_PI * (k + 1) / 14, R = 0.15, tilt = 0.55;
            auto ring = [&](double a) { return wc + V3(std::sin(a) * R * std::sin(tilt), std::sin(a) * R * std::cos(tilt), std::cos(a) * R); };
            pb.cylinder(ring(a0), ring(a1), 0.015, 5, kBlack, SM_MATTE, false);
        }
        pb.cylinder(wc, wc + V3(0.32, -0.12, 0), 0.02, 6, kDarkGrey, SM_MATTE);
        pb.box(V3(cabF - 0.04, F + 0.58, 0), V3(0.06, 0.08, 0.56), V3(0.05, 0.05, 0.055), SM_MATTE);
        pb.box(V3(cabF - 0.10, F + 0.68, 0), V3(0.012, 0.04, 0.22), V3(0.05, 0.4, 0.05), SM_EMISSIVE);
        // Fender arches over each tire (plastic, drawn at the tire's width), and a skid under the nose.
        for (double x : {xf, xr})
            for (int s = -1; s <= 1; s += 2)
                for (int k = 0; k < 14; k++) {
                    const double a0 = M_PI * k / 14, a1 = M_PI * (k + 1) / 14, rr = vs.wheelRadius + 0.07, cy = vs.wheelRadius;
                    const V3 p0(x + std::cos(a0) * rr, cy + std::sin(a0) * rr, s * vs.trackFront / 2), p1(x + std::cos(a1) * rr, cy + std::sin(a1) * rr, s * vs.trackFront / 2);
                    const V3 d = norm(p1 - p0), c = (p0 + p1) * 0.5;
                    pb.obox(c, d, cross(V3(0, 0, 1), d), V3(0, 0, 1), len(p1 - p0) * 0.52, 0.012, 0.14, paint, SM_PAINT);
                }
        // Exhaust: out the rear with a turned-down tip.
        const double exZ = -0.18;
        pb.cylinder(V3(vs.engineX - 0.25, ey, exZ), V3(xTail + 0.12, F + 0.32, exZ), 0.032, 8, kMetal, SM_METAL, false);
        pb.cylinder(V3(xTail + 0.14, F + 0.32, exZ), V3(xTail - 0.04, F + 0.26, exZ), 0.04, 10, kChrome, SM_CHROME);
    }
}

void buildParts(const Car& car, PartBuilder& pb) {
    const VehicleSpec& vs = *car.spec;
    const int cells = (int)vs.profile[0].size();
    const double W = vs.width, L = vs.length;
    auto xAt = [&](double i) { return latticeX(vs, i); };
    const double floorY = vs.layerY[0];

    // Glass cabin extents.
    int gi0 = cells, gi1 = -1, gk0 = 99, gk1 = -1;
    for (int k = 0; k < (int)vs.profile.size(); k++)
        for (int i = 0; i < cells; i++)
            if (vs.profile[k][i] == 'G') {
                gi0 = std::min(gi0, i);
                gi1 = std::max(gi1, i);
                gk0 = std::min(gk0, k);
                gk1 = std::max(gk1, k);
            }
    const bool hasCabin = gi1 >= 0;
    const double cabFront = hasCabin ? xAt(gi0) : L / 4, cabRear = hasCabin ? xAt(gi1 + 1) : -L / 4;
    const double beltY = hasCabin ? vs.layerY[gk0] : vs.layerY[1];

    // (The engine and gearbox are models on the block's nodes: carparts.h.)
    const V3 eng(vs.engineX, vs.engineY, vs.engineZ);
    const double ey = eng.y;
    {
        // Radiator behind the grille, kept under the (sometimes very low) nose.
        double hood = vs.layerY[vs.layerY.size() - 2];
        if (!vs.topLine.empty() && vs.topLine[0] > 0) hood = std::min(hood, 0.5 * (vs.topLine[0] + vs.topLine[std::min<size_t>(1, vs.topLine.size() - 1)]));
        double top = vs.model==1 ? 0.40+floorY-0.11 : std::min(ey + 0.2, hood - 0.07);
        double bottom = floorY + 0.08, hh = std::max(0.04, (top - bottom) / 2);
        pb.box(V3(L / 2 - 0.2, bottom + hh, 0), V3(0.025, hh, W * 0.33), V3(0.18, 0.19, 0.2), SM_METAL);
    }
    for (const MassItem& it : vs.items)
        if (std::string(it.name) == "battery") pb.box(V3(it.x, it.y, it.z), V3(0.12, 0.09, 0.08), kBlack, SM_MATTE);

    if (vs.openFrame) {  // tube-frame vehicles: their own cage, seats, exhaust and bumpers
        buildExtras(car, pb);
        buildOpenFrame(car, pb);
        return;
    }
    // Exhaust: down from the engine, along the floor, out the back.
    const double exY = floorY - 0.06;
    const std::vector<int> axCols = axleColumns(vs);
    if (vs.rearSusp != SOLID_AXLE || axCols.size() < 2) {
        const double exZ = W * 0.18;
        pb.cylinder(V3(eng.x - 0.1, ey - 0.15, exZ * 0.5), V3(eng.x - 0.45, exY, exZ), 0.03, 6, kMetal, SM_METAL, false);
        pb.cylinder(V3(eng.x - 0.45, exY, exZ), V3(-L / 2 + 0.95, exY, exZ), 0.03, 6, kMetal, SM_METAL, false);
        pb.box(V3(-L / 2 + 0.7, exY, exZ), V3(0.26, 0.07, 0.13), V3(0.3, 0.3, 0.31), SM_METAL);  // muffler
        pb.cylinder(V3(-L / 2 + 0.44, exY, exZ), V3(-L / 2 + 0.12, exY + 0.03, exZ), 0.028, 8, kMetal, SM_METAL, false);
        pb.cylinder(V3(-L / 2 + 0.16, exY + 0.026, exZ), V3(-L / 2 + 0.03, exY + 0.035, exZ), 0.036, 10, kChrome, SM_CHROME);
        // Fuel tank under the rear seats.
        pb.box(V3(cabRear + 0.35, floorY - 0.05, -W * 0.05), V3(0.25, 0.06, W * 0.28), kBlack, SM_MATTE);
    } else {
        // Over a live rear axle (as on the real trucks): the muffler ahead of it, inboard of
        // the frame rail, then the tailpipe up over the axle, clear of it at full bump
        // (axleReach), and back down behind it once past the track bar. Everything at floor
        // level behind the axle used to sit where the axle tube and diff are.
        const double xr = xAt(axCols.back()), railZ = W / 2 - W / vs.widthCells;
        const double exZ = std::min(W * 0.18, railZ - 0.14), overY = axleReach(vs, false) + kAxleTubeR + 0.03 + 0.01;
        const double xm = xr + 0.64, xDown = xr - 0.48, xTip = std::min(-L / 2 + 0.12, xDown - 0.05);
        pb.cylinder(V3(eng.x - 0.1, ey - 0.15, exZ * 0.5), V3(eng.x - 0.45, exY, exZ), 0.03, 6, kMetal, SM_METAL, false);
        pb.cylinder(V3(eng.x - 0.45, exY, exZ), V3(xm + 0.26, exY, exZ), 0.03, 6, kMetal, SM_METAL, false);
        pb.box(V3(xm, exY, exZ), V3(0.26, 0.07, 0.10), V3(0.3, 0.3, 0.31), SM_METAL);  // muffler
        const V3 path[] = {V3(xm - 0.26, exY, exZ), V3(xr + 0.12, overY, exZ), V3(xr - 0.25, overY, exZ), V3(xDown, exY + 0.03, exZ),
                           V3(xTip, exY + 0.03, exZ)};
        for (int k = 0; k + 1 < 5; k++) pb.cylinder(path[k], path[k + 1], 0.028, 8, kMetal, SM_METAL, false);
        pb.cylinder(V3(xTip + 0.04, exY + 0.03, exZ), V3(xTip - 0.09, exY + 0.035, exZ), 0.036, 10, kChrome, SM_CHROME);
        // Fuel tank ahead of the axle, between the left rail and the prop shaft.
        double tx = cabRear + 0.35;
        for (const MassItem& it : vs.items)
            if (std::string(it.name).rfind("fuel tank", 0) == 0) tx = it.x;
        tx = std::max(tx, xr + 0.6);
        const double zIn = 0.09, zOut = railZ - 0.05;
        pb.box(V3(tx, floorY - 0.05, -(zIn + zOut) / 2), V3(0.3, 0.07, (zOut - zIn) / 2), kBlack, SM_MATTE);
    }

    buildExtras(car, pb);
    buildCoachwork(car, pb);

    if (!hasCabin) return;
    const size_t cabinStart=pb.corners.size();
    const double cabLen = cabFront - cabRear;
    const double seatY = vs.model==1 ? 0.30 : beltY - 0.4;
    const double driverZ = vs.model==1 ? -0.35 : -W * 0.23;

    // Dashboard, steering wheel, seats.
    if(vs.model==1) {
        pb.box(V3(0.60,0.70,0),V3(0.12,0.045,0.63),V3(0.065,0.068,0.072),SM_MATTE);
        pb.box(V3(-0.02,0.37,0),V3(0.48,0.09,0.085),kBlack,SM_MATTE);
        pb.obox(V3(0.45,0.63,0),norm(V3(0.45,-1,0)),norm(V3(1,0.45,0)),V3(0,0,1),0.095,0.006,0.067,kBlack,SM_PAINT);
    } else pb.box(V3(cabFront - 0.18, beltY - 0.06, 0), V3(0.16, 0.09, W / 2 - 0.12), V3(0.1, 0.1, 0.11), SM_MATTE);
    double frontSeatX = vs.model==1 ? -0.07 : cabFront - std::min(0.95, cabLen * 0.55);
    V3 wheelC(frontSeatX + 0.52, vs.model==1 ? 0.66 : beltY + 0.02, driverZ);
    {
        const int segs = 16;
        const double R = vs.model==1 ? 0.135 : 0.17, r = 0.018, tilt = 0.45;
        for (int k = 0; k < segs; k++) {
            double a0 = 2 * M_PI * k / segs, a1 = 2 * M_PI * (k + 1) / segs;
            auto ringPt = [&](double a) {
                V3 p(0, std::sin(a) * R, std::cos(a) * R);
                return wheelC + V3(p.y * std::sin(tilt), p.y * std::cos(tilt), p.z);
            };
            pb.cylinder(ringPt(a0), ringPt(a1), r, 5, kBlack, SM_MATTE, false);
        }
        pb.cylinder(wheelC, wheelC + V3(0.3, -0.05, 0), 0.025, 6, kDarkGrey, SM_MATTE);  // column
        pb.cylinder(wheelC + V3(0, 0.02, -0.15), wheelC + V3(0, 0.02, 0.15), 0.012, 4, kBlack, SM_MATTE, false);
    }
    const V3 seatCol(0.16, 0.15, 0.15);
    auto seat = [&](double x, double z, double halfW) {
        pb.box(V3(x, seatY, z), V3(0.24, 0.06, halfW), seatCol, SM_MATTE);
        if(vs.model==1) {
            pb.obox(V3(x-0.20,seatY+0.25,z),norm(V3(1,0.24,0)),norm(V3(-0.24,1,0)),V3(0,0,1),0.05,0.26,halfW*0.83,seatCol,SM_MATTE);
            for(int side:{-1,1})pb.box(V3(x-0.02,seatY+0.09,z+side*halfW*0.9),V3(0.22,0.07,0.035),kBlack,SM_MATTE);
        } else pb.box(V3(x - 0.24, seatY + 0.3, z), V3(0.06, 0.3, halfW * 0.95), seatCol, SM_MATTE);
        pb.box(V3(x - 0.25, seatY + (vs.model==1 ? 0.56 : 0.66), z), V3(0.05, 0.08, halfW * 0.55), seatCol, SM_MATTE);
    };
    seat(frontSeatX, driverZ, 0.23);
    seat(frontSeatX, -driverZ, 0.23);
    // Rear bench, far enough forward to sit under the roof (fastbacks slope down early).
    // (Its back kept inside the cab: a pickup's back wall is right behind the rear seat.)
    if (cabLen > 1.5 && vs.model != 1) seat(std::max(cabRear + 0.33, std::min(cabRear + 0.42 + 0.6 * vs.rakeRear, frontSeatX - 0.75)), 0, W / 2 - 0.22);

    // Mirrors on stalks at the base of the A-pillars, and wipers.
    for (int s = -1; s <= 1; s += 2) {
        pb.current = s < 0 ? ON_LEFT_DOOR : ON_RIGHT_DOOR;
        V3 base = vs.model==1 ? V3(0.33,0.93,s*0.69) : V3(cabFront - 0.1, beltY + 0.05, s * (W / 2 - 0.05));
        V3 head = base + (vs.model==1 ? V3(-0.05,0.05,s*0.22) : V3(0.02, 0.05, s * 0.08));
        pb.cylinder(base, head, 0.012, 4, kBlack, SM_MATTE, false);
        pb.box(head + V3(0, 0.025, s * 0.03), V3(0.035, 0.04, 0.065), car.color, SM_PAINT);
        pb.box(head + V3(-0.037, 0.025, s * 0.03), V3(0.003, 0.032, 0.055), V3(0.5, 0.55, 0.6), SM_CHROME);
    }
    pb.current = ON_BODY;
    for (int s = -1; s <= 1; s += 2)
        pb.box(V3(vs.model==1 ? 0.78 : cabFront - 0.03, vs.model==1 ? 0.82 : beltY + 0.035, s * W * 0.17), V3(0.012, 0.01, W * 0.17), kBlack, SM_MATTE);
    if(vs.model==1)for(size_t i=cabinStart;i<pb.corners.size();i++)pb.corners[i].y+=vs.layerY[0]-0.11;
}

std::map<const VehicleSpec*, std::unique_ptr<VisualTemplate>>& cache() {
    static std::map<const VehicleSpec*, std::unique_ptr<VisualTemplate>> c;
    return c;
}

// ---- Running-gear helpers --------------------------------------------------------

void tube(std::vector<Vtx>& out, const V3& a, const V3& b, double r, int sides, const V3& col, int mat) {
    V3 axis = b - a;
    double L = len(axis);
    if (L < 1e-6) return;
    axis = axis / L;
    V3 t = norm(cross(axis, std::fabs(axis.y) < 0.9 ? V3(0, 1, 0) : V3(1, 0, 0)));
    V3 s = cross(axis, t);
    auto v = [&](const V3& p, const V3& n) {
        return Vtx{{(float)p.x, (float)p.y, (float)p.z}, {(float)n.x, (float)n.y, (float)n.z},
                   {(float)col.x, (float)col.y, (float)col.z, (float)mat}};
    };
    for (int k = 0; k < sides; k++) {
        double a0 = 2 * M_PI * k / sides, a1 = 2 * M_PI * (k + 1) / sides;
        V3 n0 = t * std::cos(a0) + s * std::sin(a0), n1 = t * std::cos(a1) + s * std::sin(a1);
        V3 p0 = a + n0 * r, p1 = a + n1 * r, p2 = b + n1 * r, p3 = b + n0 * r;
        out.insert(out.end(), {v(p0, n0), v(p1, n1), v(p2, n1), v(p0, n0), v(p2, n1), v(p3, n0)});
    }
}

void blob(std::vector<Vtx>& out, const V3& c, double r, const V3& col, int mat) {
    tube(out, c - V3(r, 0, 0), c + V3(r, 0, 0), r, 6, col, mat);
    tube(out, c - V3(0, 0, r), c + V3(0, 0, r), r * 0.9, 6, col, mat);
}

}  // namespace

// =============================================================================

const VisualTemplate& visualTemplate(const Car& car) {
    auto& slot = cache()[car.spec];
    if (slot) return *slot;
    slot = std::make_unique<VisualTemplate>();
    VisualTemplate& t = *slot;
    const VehicleSpec& vs = *car.spec;
    const int cells = (int)vs.profile[0].size();
    int glassStart=cells, glassEnd=0, glassLayer=0;
    for (int k=0;k<(int)vs.profile.size();k++)
        for (int i=0;i<cells;i++) if(vs.profile[k][i]=='G') {
            glassStart=std::min(glassStart,i); glassEnd=std::max(glassEnd,i+1); glassLayer=k;
        }
    const double belt=vs.layerY[glassLayer], roof=vs.layerY[glassLayer+1];

    // ---- Body: dedicated Jesko surface, or the general subdivided cage ----
    if (vs.model == 1 && !car.isProp()) {
        buildJeskoMesh(car, t);
    } else if (!buildBodyShell(car, t)) {  // sculpted shell (4Runner, bodyshell.cpp), else the cage
    CCMesh m;
    m.verts.resize(car.bodyNodes);
    for (int n = 0; n < car.bodyNodes; n++) m.verts[n] = {{n, 1.0f}};
    for (size_t q = 0; q < car.quads.size(); q++) {
        const SkinQuad& sq = car.quads[q];
        // Tube-frame vehicles show skin only on their panel cells ('p', and 'w' wheel wells).
        if (vs.openFrame && sq.cellChar != 'p' && sq.cellChar != 'w') continue;
        m.faces.push_back({sq.v[0], sq.v[1], sq.v[2], sq.v[3]});
        m.parent.push_back((int)q);
        std::array<Lat, 4> l;
        for (int c = 0; c < 4; c++) l[c] = {sq.lat[c][0], sq.lat[c][1], sq.lat[c][2]};
        m.lat.push_back(l);
    }
    std::set<std::pair<int, int>> creases;
    {
        std::map<std::pair<int, int>, std::vector<int>> edgeQuads;
        for (size_t q = 0; q < car.quads.size(); q++)
            for (int c = 0; c < 4; c++) {
                int a = car.quads[q].v[c], b = car.quads[q].v[(c + 1) % 4];
                edgeQuads[{std::min(a, b), std::max(a, b)}].push_back((int)q);
            }
        for (const auto& eq : edgeQuads) {
            if (eq.second.size() != 2) continue;
            const SkinQuad& a = car.quads[eq.second[0]];
            const SkinQuad& b = car.quads[eq.second[1]];
            bool aTop = a.axis == 2 && a.side == 1, bTop = b.axis == 2 && b.side == 1;
            if ((aTop && b.axis == 1) || (bTop && a.axis == 1)) creases.insert(eq.first);
        }
    }
    // Panel gaps: where a bolt-on panel meets the body (or another panel), the edge stays
    // sharp at every level, so each panel's surface depends only on its own nodes.
    auto panelOf = [&](int q) { return car.quadPanel.empty() ? -1 : car.quadPanel[q]; };
    {
        std::map<std::pair<int, int>, std::vector<int>> eq;
        for (size_t q = 0; q < car.quads.size(); q++)
            for (int c = 0; c < 4; c++) {
                int a = car.quads[q].v[c], b = car.quads[q].v[(c + 1) % 4];
                eq[{std::min(a, b), std::max(a, b)}].push_back((int)q);
            }
        for (const auto& e : eq)
            if (e.second.size() == 2 && panelOf(e.second[0]) != panelOf(e.second[1])) m.seams.insert(e.first);
    }
    m = subdivide(subdivide(m, creases));

    // Compact to vertices actually used by faces, one copy per panel: a panel's faces
    // follow the panel's own nodes (they coincide with the body's until it comes loose).
    std::vector<std::map<int, int>> onPanel(car.panels.size());
    for (size_t p = 0; p < car.panels.size(); p++)
        for (size_t k = 0; k < car.panels[p].bodyNodes.size(); k++) onPanel[p][car.panels[p].bodyNodes[k]] = car.panels[p].panelNodes[k];
    std::map<std::pair<int, int>, int> remap;
    std::vector<std::array<int, 4>> faceTopo(m.faces.size());
    for (size_t fi = 0; fi < m.faces.size(); fi++) {
        int pnl = panelOf(m.parent[fi]);
        for (int c = 0; c < 4; c++) {
            auto key = std::make_pair(m.faces[fi][c], pnl);
            auto it = remap.find(key);
            if (it == remap.end()) {
                it = remap.emplace(key, t.topoCount++).first;
                t.wStart.push_back((int)t.wNode.size());
                for (const auto& e : m.verts[key.first]) {
                    int node = e.first;
                    if (pnl >= 0) {
                        auto cp = onPanel[pnl].find(node);
                        if (cp != onPanel[pnl].end()) node = cp->second;
                    }
                    t.wNode.push_back(node);
                    t.wVal.push_back(e.second);
                }
            }
            faceTopo[fi][c] = it->second;
        }
    }
    t.wStart.push_back((int)t.wNode.size());

    // Render vertices: split topological vertices where the region changes; window
    // regions are split per skin quad too so each pane can shatter on its own.
    std::map<std::tuple<int, int, int>, int> rvIndex;
    for (size_t f = 0; f < m.faces.size(); f++) {
        VisualTemplate::Face face;
        face.parent = m.parent[f];
        int region = regionOf(car.quads[face.parent], cells);
        // A raked windshield spans top faces of the cage too. Treating every top
        // face as roof paint left a tiny black slit below a painted windshield.
        if (region == REG_ROOF) {
            V3 center;
            for (int c=0;c<4;c++) for (const auto& w : m.verts[m.faces[f][c]]) center+=car.restLocal[w.first]*(w.second*0.25);
            if (center.x > latticeX(vs,glassStart)-vs.rakeFront-0.015 && center.y < roof-0.015) region=REG_WINDSHIELD;
            else if(center.x < latticeX(vs,glassEnd)+vs.rakeRear+0.015 && center.y < roof-0.015) region=REG_REAR_WINDOW;
        }
        face.glass = region == REG_CABIN_SIDE || region == REG_WINDSHIELD || region == REG_REAR_WINDOW;
        for (int c = 0; c < 4; c++) {
            int topo = faceTopo[f][c];
            face.topo[c] = topo;
            auto key = std::make_tuple(topo, region, face.glass ? face.parent : -1);
            auto it = rvIndex.find(key);
            if (it == rvIndex.end()) {
                int id = (int)t.rvTopo.size();
                rvIndex[key] = id;
                t.rvTopo.push_back(topo);
                BodyStatic st;
                for (int a = 0; a < 3; a++) st.lat[a] = m.lat[f][c][a];
                if (region == REG_WINDSHIELD || region == REG_REAR_WINDOW) {
                    V3 rest;
                    for (const auto& w : m.verts[m.faces[f][c]]) rest+=car.restLocal[w.first]*w.second;
                    st.lat[2]=(float)(glassLayer + (rest.y-belt)/std::max(0.01,roof-belt));
                }
                st.region = (float)region;
                t.rvStatic.push_back(st);
                face.rv[c] = id;
            } else {
                face.rv[c] = it->second;
            }
        }
        t.faces.push_back(face);
    }

    }
    // ---- Decal layout ----
    int gi0 = cells, gi1 = -1, gk0 = 99, gk1 = -1;
    for (int k = 0; k < (int)vs.profile.size(); k++)
        for (int i = 0; i < cells; i++)
            if (vs.profile[k][i] == 'G') {
                gi0 = std::min(gi0, i);
                gi1 = std::max(gi1, i);
                gk0 = std::min(gk0, k);
                gk1 = std::max(gk1, k);
            }
    auto topCell = [&](int i) {
        int top = 0;
        for (int k = 0; k < (int)vs.profile.size(); k++)
            if (vs.profile[k][i] != '.') top = k;
        return top;
    };
    t.cabin[0] = (float)gi0;
    t.cabin[1] = (float)(gi1 + 1);
    t.cabin[2] = (float)gk0;
    t.cabin[3] = (float)(gk1 + 1);
    t.body[0] = (float)cells;
    t.body[1] = (float)vs.widthCells;
    t.body[2] = topCell(0) + 0.5f;
    t.body[3] = topCell(cells - 1) + 0.5f;
    {
        // So round lamps come out round: how many k units make one j unit at the lamp row.
        int lk = std::min((int)vs.layerY.size() - 2, (int)topCell(0));
        double dj = vs.width / vs.widthCells, dk = vs.layerY[lk + 1] - vs.layerY[lk];
        t.style[0] = (float)vs.lampStyle;
        t.style[1] = (float)vs.grilleStyle;
        t.style[2] = (float)(dj / std::max(0.05, dk));
        t.style[3] = (float)(doorCells(vs) * (hasRearDoors(vs) ? 1 : -1));
    }

    // ---- Parts ----
    PartBuilder pb;
    buildParts(car, pb);
    int engineHex[8], gearboxHex[8];
    if (car.engineNodes.size() >= 12) blockHex(car, engineHex, gearboxHex);
    for (size_t k = 0; k < pb.corners.size(); k++) {
        int g = pb.group[k / 3];
        if (g == ON_ENGINE) t.partBind.push_back(bindInHex(car, engineHex, pb.corners[k]));
        else if (g == ON_GEARBOX) t.partBind.push_back(bindInHex(car, gearboxHex, pb.corners[k]));
        else if (g >= ON_LEFT_DOOR) {
            const char* names[] = {"LEFT DOOR", "RIGHT DOOR", "HOOD", "LIFTGATE"};
            t.partBind.push_back(bindPanel(car, pb.corners[k], names[g-ON_LEFT_DOOR]));
        } else t.partBind.push_back(bindPoint(car, pb.corners[k]));
    }
    t.partColor = pb.color;
    return t;
}

void evalBody(const Car& car, const VisualTemplate& t, std::vector<BodyDynamic>& dyn, std::vector<unsigned>& opaque,
              std::vector<unsigned>& glass) {
    CZ_THREAD_CACHE(std::vector<V3>, pos);
    CZ_THREAD_CACHE(std::vector<V3>, nrm);
    pos.assign(t.topoCount, V3());
    nrm.assign(t.topoCount, V3());
    for (int v = 0; v < t.topoCount; v++) {
        V3 p;
        if (!t.sculptBind.empty()) {
            const auto& b=t.sculptBind[v];
            for(int k=0;k<8;k++)p+=car.nodes[b.n[k]].p*b.w[k];
            if(b.normalOffset!=0)p+=norm(cross(car.nodes[b.n[1]].p-car.nodes[b.n[0]].p,car.nodes[b.n[2]].p-car.nodes[b.n[0]].p))*b.normalOffset;
        } else {
            for (int k = t.wStart[v]; k < t.wStart[v + 1]; k++) p += car.nodes[t.wNode[k]].p * t.wVal[k];
        }
        pos[v] = p;
    }

    // Skin quads that have torn apart (stretched far past factory length) are hidden.
    CZ_THREAD_CACHE(std::vector<char>, torn);
    CZ_THREAD_CACHE(std::vector<char>, shattered);
    torn.assign(car.quads.size(), 0);
    shattered.assign(car.quads.size(), 0);
    for (size_t q = 0; q < car.quads.size(); q++) {
        const SkinQuad& sq = car.quads[q];
        int pnl = car.quadPanel.empty() ? -1 : car.quadPanel[q];
        auto at = [&](int n) -> const V3& {  // a panel's face is torn if the panel itself tears
            if (pnl >= 0) {
                const Panel& p = car.panels[pnl];
                for (size_t k = 0; k < p.bodyNodes.size(); k++)
                    if (p.bodyNodes[k] == n) return car.nodes[p.panelNodes[k]].p;
            }
            return car.nodes[n].p;
        };
        for (int k = 0; k < 2; k++) {
            const Tri& tr = car.tris[sq.tri + k];
            for (int e = 0; e < 3; e++)
                if (len(at(tr.v[(e + 1) % 3]) - at(tr.v[e])) > 1.8 * tr.rest[e]) torn[q] = 1;
            shattered[q] |= tr.shattered;
        }
    }

    for (const auto& f : t.faces) {
        if (torn[f.parent]) continue;
        V3 n = cross(pos[f.topo[2]] - pos[f.topo[0]], pos[f.topo[3]] - pos[f.topo[1]]);
        for (int c = 0; c < 4; c++) nrm[f.topo[c]] += n;
    }

    dyn.resize(t.rvTopo.size());
    for (size_t r = 0; r < t.rvTopo.size(); r++) {
        int topo = t.rvTopo[r];
        V3 n = norm(nrm[topo]);
        BodyDynamic& d = dyn[r];
        d.p[0] = (float)pos[topo].x;
        d.p[1] = (float)pos[topo].y;
        d.p[2] = (float)pos[topo].z;
        d.n[0] = (float)n.x;
        d.n[1] = (float)n.y;
        d.n[2] = (float)n.z;
        d.broken = 0;
    }
    for (const auto& f : t.faces) {
        if (torn[f.parent]) continue;
        if (f.glass && shattered[f.parent])
            for (int c = 0; c < 4; c++) dyn[f.rv[c]].broken = 1;
        unsigned a = f.rv[0], b = f.rv[1], c = f.rv[2], d = f.rv[3];
        opaque.insert(opaque.end(), {a, b, c, a, c, d});
        if (f.glass) glass.insert(glass.end(), {a, b, c, a, c, d});
    }
}

void evalParts(const Car& car, const VisualTemplate& t, std::vector<Vtx>& out) {
    const size_t triCount = t.partBind.size() / 3;
    for (size_t tri = 0; tri < triCount; tri++) {
        V3 p[3];
        for (int c = 0; c < 3; c++) {
            const VisualTemplate::Bind& b = t.partBind[tri * 3 + c];
            for (int k = 0; k < 8; k++) p[c] += car.nodes[b.n[k]].p * b.w[k];
            if (b.normalOffset != 0)
                p[c] += norm(cross(car.nodes[b.n[1]].p-car.nodes[b.n[0]].p,car.nodes[b.n[2]].p-car.nodes[b.n[0]].p))*b.normalOffset;
        }
        V3 n = norm(cross(p[1] - p[0], p[2] - p[0]));
        const float* col = &t.partColor[tri * 4];
        for (int c = 0; c < 3; c++)
            out.push_back(Vtx{{(float)p[c].x, (float)p[c].y, (float)p[c].z},
                              {(float)n.x, (float)n.y, (float)n.z},
                              {col[0], col[1], col[2], col[3]}});
    }
}

namespace {

}  // namespace

// Frame rails and crossmembers under the floor, between the axles (ahead of and behind
// them they're hidden inside the bumpers). Over a live axle the rails kick up, as a real
// ladder frame does, high enough for the axle to reach full bump (axleReach) under them,
// and the crossmember at that axle moves to just behind it, clear of the diff.
void buildFrameRails(const Car& car, std::vector<Vtx>& out) {
    if (car.wheels.empty()) return;
    const VehicleSpec& vs = *car.spec;
    const V3 frameCol(0.09, 0.09, 0.10);
    const double railR = 0.03;
    auto P = [&](int n) { return car.nodes[n].p; };
    const Frame fr = car.frame();
    double xFront = -1e9, xRear = 1e9;
    for (const Wheel& w : car.wheels) {
        xFront = std::max(xFront, car.restLocal[w.inner].x);
        xRear = std::min(xRear, car.restLocal[w.inner].x);
    }
    // Live axles: station x, which way is "behind" it (away from the other axle), and how
    // far the rails lift over it.
    struct Kick {
        double x, out, lift;
    };
    std::vector<Kick> kicks;
    bool propShaft = false;
    for (const Axle& a : car.axles) {
        propShaft |= a.propShaft >= 0;
        if (a.housing.empty() || a.wheel[0] < 0) continue;
        const double top = axleReach(vs, a.front) + kAxleTubeR + railR + 0.01;
        kicks.push_back({car.restLocal[car.wheels[a.wheel[0]].inner].x, a.front ? 1.0 : -1.0, std::max(0.0, top - (vs.layerY[0] - 0.02))});
    }
    auto kickAt = [&](double x) -> const Kick* {
        for (const Kick& k : kicks)
            if (std::fabs(x - k.x) < 1e-3) return &k;
        return nullptr;
    };
    const double centerZ = 0.02;
    std::vector<std::pair<double, V3>> tails;  // kicked rail ends behind a live axle: (z, point)
    for (int bi : car.frameBeams) {
        const Beam& b = car.beams[bi];
        if (b.broken) continue;
        // The Jesko has a carbon tub. Upper cage braces are solver supports,
        // not exposed steel rails running across the sculpted clamshell.
        if(vs.model==1 && std::max(car.restLocal[b.a].y,car.restLocal[b.b].y)>vs.layerY[0]+0.34) continue;
        const V3 &ra = car.restLocal[b.a], &rb = car.restLocal[b.b];
        double xa = ra.x, xb = rb.x;
        if (std::max(xa, xb) > xFront + 0.3 || std::min(xa, xb) < xRear - 0.3) continue;
        const bool across = std::fabs(xa - xb) < 1e-3;
        // No rail down the middle: that's where the prop shaft runs.
        if (!across && propShaft && std::fabs(ra.z) < centerZ && std::fabs(rb.z) < centerZ) continue;
        V3 a = P(b.a) - fr.up * 0.02, c = P(b.b) - fr.up * 0.02;
        if (across && kickAt(xa)) continue;  // moved behind the axle (below)
        const Kick* k = across ? nullptr : kickAt(xa) ? kickAt(xa) : kickAt(xb);
        if (!k) {
            tube(out, a, c, railR, 4, frameCol, SM_MATTE);
            continue;
        }
        // a = the end away from the axle, c = the end at the axle station.
        if (std::fabs(xa - k->x) < 1e-3) {
            std::swap(a, c);
            std::swap(xa, xb);
        }
        const double run = std::fabs(xa - xb);
        const V3 lift = fr.up * k->lift, along = (c - a) / std::max(run, 1e-6);
        const V3 rise = a + (c - a) * std::max(0.0, (run - 0.20) / run) + lift;  // up to full height 0.2 m short of the axle
        const V3 over = c + lift, past = over + along * 0.30;                    // over it and on 0.3 m beyond
        tube(out, a, rise, railR, 4, frameCol, SM_MATTE);
        tube(out, rise, past, railR, 4, frameCol, SM_MATTE);
        tails.push_back({ra.z + rb.z, past});
    }
    // Crossmember behind each live axle, between the kicked rails.
    std::sort(tails.begin(), tails.end(), [](const auto& p, const auto& q) { return p.first < q.first; });
    for (size_t i = 0; i + 1 < tails.size(); i++)
        if (len(tails[i + 1].second - tails[i].second) < vs.width * 0.8) tube(out, tails[i].second, tails[i + 1].second, railR, 4, frameCol, SM_MATTE);
}

// Running gear from the live physics: every arm, link, spring and shaft is drawn where
// its beam is right now, so you can watch the suspension work and the driveline twist.
void buildRunningGear(const Car& car, std::vector<Vtx>& out) {
    if (car.wheels.empty()) return;
    const VehicleSpec& vs = *car.spec;
    const V3 armCol(0.17, 0.17, 0.18), knuckleCol(0.36, 0.36, 0.37), damperCol(0.25, 0.25, 0.27),
        housingCol(0.15, 0.15, 0.16);
    const double sz = vs.wheelRadius / 0.32;
    auto P = [&](int n) { return car.nodes[n].p; };
    Frame fr = car.frame();

    if (car.ladder.empty()) buildFrameRails(car, out);  // (a ladder frame is models now: carframe.h)

    // (Arms, tie rods, trailing links, leaf packs and coil-overs are models on their beams: carparts.h.)

    for (size_t wi = 0; wi < car.wheels.size(); wi++) {
        const Wheel& wh = car.wheels[wi];
        V3 hub = (P(wh.inner) + P(wh.outer)) * 0.5;
        SuspensionType st = wh.front ? vs.frontSusp : vs.rearSusp;
        if (!wh.detached || st != SOLID_AXLE) {
            if (st == WISHBONE) {
                // Upright: ball joint to ball joint, a stub axle out to the hub, a steering arm back.
                V3 lo = P(wh.lower), hi = P(wh.top), mid = (lo + hi) * 0.5;
                tube(out, lo, hi, 0.03 * sz, 6, knuckleCol, SM_METAL);
                tube(out, mid, P(wh.inner), 0.045 * sz, 8, knuckleCol, SM_METAL);
                tube(out, (mid + lo) * 0.5, P(wh.arm), 0.018 * sz, 6, knuckleCol, SM_METAL);
                blob(out, lo, 0.03 * sz, kDarkGrey, SM_METAL);
                blob(out, hi, 0.028 * sz, kDarkGrey, SM_METAL);
            } else if (st == STRUT && wh.spring >= 0) {
                // Strut: the damper body rises from the knuckle to the spring seat (the coil
                // and the damper rod carry on up to the tower), a stub axle and steering arm.
                const Beam& sp = car.beams[wh.spring];
                V3 lo = P(wh.lower), seat = sp.a < car.frameNodes ? P(sp.b) : P(sp.a);
                V3 mid = lo + (seat - lo) * 0.3;
                tube(out, lo, seat, 0.032 * sz, 8, damperCol, SM_METAL);
                tube(out, mid, P(wh.inner), 0.045 * sz, 8, knuckleCol, SM_METAL);
                tube(out, lo + (mid - lo) * 0.5, P(wh.arm), 0.018 * sz, 6, knuckleCol, SM_METAL);
                blob(out, lo, 0.03 * sz, kDarkGrey, SM_METAL);
            } else if (st == SOLID_AXLE && wh.tieRod >= 0) {
                // Steering knuckle on a live front axle: the kingpin (ball joint to ball joint), a stub
                // axle out to the hub and the steering arm the tie rod pushes.
                V3 lo = P(wh.lower), hi = P(wh.top), mid = (lo + hi) * 0.5;
                tube(out, lo, hi, 0.04, 8, housingCol, SM_METAL);
                tube(out, mid, P(wh.inner), 0.045, 8, knuckleCol, SM_METAL);
                tube(out, (mid + lo) * 0.5, P(wh.arm), 0.02, 6, knuckleCol, SM_METAL);
                blob(out, lo, 0.045, kDarkGrey, SM_METAL);
                blob(out, hi, 0.04, kDarkGrey, SM_METAL);
            } else if (st == TWIST_BEAM) {
                // Boxed trailing arm from its pivot bushing back to the hub.
                V3 piv = (P(wh.lower) + P(wh.arm)) * 0.5;
                tube(out, P(wh.lower), P(wh.inner), 0.035 * sz, 6, armCol, SM_METAL);
                tube(out, P(wh.arm), P(wh.inner), 0.025 * sz, 6, armCol, SM_METAL);
                tube(out, P(wh.lower), P(wh.arm), 0.04 * sz, 8, kBlack, SM_MATTE);  // pivot bushing
                tube(out, P(wh.inner), P(wh.top), 0.03 * sz, 6, knuckleCol, SM_METAL);
                (void)piv;
            }
        }
        if (wh.spring >= 0 && !car.beams[wh.spring].broken) {
            const Beam& sp = car.beams[wh.spring];
            bool aBody = sp.a < car.frameNodes;
            V3 upper = aBody ? P(sp.a) : P(sp.b);
            if (wh.shackle >= 0) {
                // On leaf springs the leaf is the spring: just the shock, from below the tube
                // up to the frame.
                const V3 bottom = P(wh.lower), mid = bottom + (upper - bottom) * 0.55;
                tube(out, bottom, mid, 0.026, 8, damperCol, SM_METAL);
                tube(out, mid, upper, 0.011, 6, kMetal, SM_METAL);
            }
        }
        (void)hub;
    }

    for (const Axle& ax : car.axles) {
        if (ax.wheel[0] < 0 || ax.wheel[1] < 0) continue;
        const Wheel& wl = car.wheels[ax.wheel[0]];
        const Wheel& wr = car.wheels[ax.wheel[1]];
        SuspensionType st = ax.front ? vs.frontSusp : vs.rearSusp;
        if (st == SOLID_AXLE && !ax.housing.empty()) {
            // Axle tubes out to the hubs.
            // Real sizes (Dana 44: 3 in tubes, a ~10.5 in cover), not scaled with the tires.
            V3 diff = P(ax.housing[0]);
            // (A steering axle's tubes end at the kingpins, which turn with the knuckles.)
            auto tubeEnd = [&](const Wheel& w) { return w.tieRod >= 0 ? (P(w.lower) + P(w.top)) * 0.5 : P(w.inner); };
            if (!wl.detached) tube(out, diff, tubeEnd(wl), kAxleTubeR, 10, housingCol, SM_METAL);
            if (!wr.detached) tube(out, diff, tubeEnd(wr), kAxleTubeR, 10, housingCol, SM_METAL);
            // (The pumpkin and its pinion snout are the differential model: carparts.h.)
            for (const Wheel* w : {&wl, &wr})
                if (!w->detached && w->tieRod < 0) tube(out, P(w->lower), P(w->inner), 0.03, 6, housingCol, SM_METAL);  // link bracket
        }
        if (st == TWIST_BEAM) {
            // The torsion beam itself, between the two arms.
            tube(out, P(wl.arm), P(wr.arm), 0.045 * sz, 8, armCol, SM_METAL);
        }
        if ((st == WISHBONE || st == STRUT) && ax.arbK > 0 && !wl.detached && !wr.detached) {
            // Anti-roll bar across the car, drop links down to the lower arms.
            V3 back = fr.fwd * (ax.front ? -0.18 : 0.18);
            V3 l0 = P(wl.lower), r0 = P(wr.lower);
            V3 lb = l0 + (fr.left * -1) * 0.0 + back + fr.up * 0.06, rb = r0 + back + fr.up * 0.06;
            V3 lIn = lb + (rb - lb) * 0.22, rIn = lb + (rb - lb) * 0.78;
            tube(out, lIn, rIn, 0.012 * sz, 6, kBlack, SM_MATTE);
            tube(out, lIn, lb, 0.012 * sz, 6, kBlack, SM_MATTE);
            tube(out, rIn, rb, 0.012 * sz, 6, kBlack, SM_MATTE);
            tube(out, lb, l0 + back * 0.6, 0.008 * sz, 4, kMetal, SM_METAL);
            tube(out, rb, r0 + back * 0.6, 0.008 * sz, 4, kMetal, SM_METAL);
        }
        // Steering rack between the inner tie-rod ends (a live axle has a cross tie rod instead).
        if (ax.front && st != SOLID_AXLE) {
            int rl = -1, rr = -1;
            for (int s2 = 0; s2 < 2; s2++) {
                const Wheel& w = car.wheels[ax.wheel[s2]];
                if (w.tieRod < 0) continue;
                const Beam& t = car.beams[w.tieRod];
                (s2 ? rr : rl) = t.a < car.frameNodes ? t.a : t.b;
            }
            if (rl >= 0 && rr >= 0) {
                V3 a = P(rl), b = P(rr), d = norm(b - a);
                tube(out, a + d * 0.09, b - d * 0.09, 0.028 * sz, 8, kMetal, SM_METAL);
                tube(out, a + d * 0.3, a + d * 0.42, 0.045 * sz, 8, kMetal, SM_METAL);  // pinion housing
            }
        }
    }
    // (Prop shafts, half shafts and the differentials are models too: carparts.h.)
}

namespace {
// A model drawn round a trunk or boulder: built for its radius (to 2 cm), kept until the lab changes the picks.
const mk::Mesh& radiusModel(const char* id, double r) {
    static std::map<std::pair<std::string, int>, mk::Mesh> cache;
    static uint64_t version = 0;
    if (version != mk::picksVersion()) cache.clear(), version = mk::picksVersion();
    const int q = (int)std::lround(r * 50);
    auto it = cache.find({id, q});
    if (it == cache.end()) it = cache.emplace(std::make_pair(std::string(id), q), mk::modelMesh(id, {{"radius", q / 50.0}})).first;
    return it->second;
}
double yawToward(const V3& from, const V3& to) { return std::atan2(-(to.z - from.z), to.x - from.x); }
// A sling round a boulder, built for that rock's outline at the height it's tied (kept while the line is on).
const mk::Mesh& rockSlingModel(const char* id, int rock, const objshape::Wrap& wr) {
    static std::map<std::array<long, 4>, mk::Mesh> cache;
    static uint64_t version = 0;
    if (version != mk::picksVersion() || cache.size() > 16) cache.clear(), version = mk::picksVersion();
    const std::array<long, 4> key = {rock, std::lround(wr.attach.x * 100), std::lround(wr.attach.y * 100), std::lround(wr.attach.z * 100)};
    auto it = cache.find(key);
    if (it == cache.end()) {
        mk::pathHint() = objshape::localPath(wr, 0.09);
        it = cache.emplace(key, mk::modelMesh(id, {})).first;
        mk::pathHint().clear();
    }
    return it->second;
}
}  // namespace

// Winch lines and tow straps: a sagging line when slack, straight when loaded, with a
// shackle at each truck and, at a fixed anchor, its gear (anchors.h): a tree strap round the
// trunk, a chain sling round the boulder, a stake, a bolt or a picket in the ground. The ones
// left in the ground stay drawn after the line comes off.
void buildRopes(const Sim& sim, std::vector<Vtx>& out) {
    for (const SetAnchor& sa : sim.setAnchors)
        appendModel(out, mk::modelMesh(anchors::gearModel(sa.gear)), V3(sa.p.x, sim.world.height(sa.p.x, sa.p.z), sa.p.z), sa.yaw);
    for (const Rope& r : sim.ropes) {
        auto at = [&](const RopeEnd& e) { return e.kind == ANCHOR_CAR ? sim.cars[e.car].hookPoint(e.hook) : e.p; };
        const bool winch = r.kind == ROPE_WINCH;
        const V3 col = winch ? V3(0.72, 0.1, 0.05) : V3(0.95, 0.6, 0.08);
        const double rad = winch ? 0.011 : 0.02;  // (the winch line drawn a bit fat so it reads at a distance)
        const std::vector<V3> path = sim.ropePath(r, rad + 0.015);  // (slack line lies on the ground, not through it)
        for (size_t k = 1; k < path.size(); k++) tube(out, path[k - 1], path[k], rad, winch ? 5 : 4, col, SM_MATTE);
        for (const RopeEnd* e : {&r.a, &r.b}) {
            const V3 p = at(*e), to = at(e == &r.a ? r.b : r.a);
            if (e->kind == ANCHOR_CAR) blob(out, p, 0.025, V3(0.55, 0.55, 0.57), SM_METAL);  // shackle
            else if (e->kind == ANCHOR_STAKE) {
                bool left = false;  // (a stake you left in the ground is drawn above)
                for (const SetAnchor& sa : sim.setAnchors) left |= std::hypot(sa.p.x - p.x, sa.p.z - p.z) < 0.3;
                if (left) continue;
                anchors::GroundRule gr = anchors::groundRule(sim.world.surfaceAt(p.x, p.z), 0);  // (another player's: whatever it's in)
                appendModel(out, mk::modelMesh(anchors::gearModel(gr.gear == GEAR_COUNT ? GEAR_STAKE : gr.gear)), V3(p.x, sim.world.height(p.x, p.z), p.z), yawToward(p, to));
            } else if (e->kind == ANCHOR_TREE) {  // the strap round the trunk this is tied to
                const Pole* tree = nullptr;
                double best = 0.05;
                for (const Pole& pl : sim.world.poles) {
                    const double d = std::fabs(std::hypot(pl.x - p.x, pl.z - p.z) - pl.r);
                    if (pl.r >= anchors::kTreeMinR && d < best && p.y > pl.y0 && p.y < pl.y1) best = d, tree = &pl;
                }
                if (tree) appendModel(out, radiusModel("tree_saver", tree->r), V3(tree->x, p.y - 0.42, tree->z), yawToward(V3(tree->x, 0, tree->z), to));
                else blob(out, p, 0.05, V3(0.95, 0.6, 0.08), SM_MATTE);
            } else {  // the chain sling round the boulder: its cross-section at the height the line is tied, tight on the stone
                const int ri = objshape::rockAt(sim.world, p, 0.06);
                const objshape::Wrap wr = ri >= 0 ? objshape::wrapRock(sim.world.rocks[ri], p.y, p) : objshape::Wrap();
                if (wr.ok) appendModel(out, rockSlingModel("chain_sling", ri, wr), wr.attach, std::atan2(-wr.out.z, wr.out.x));
                else blob(out, p, 0.05, V3(0.95, 0.6, 0.08), SM_MATTE);
            }
        }
    }
}
