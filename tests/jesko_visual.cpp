// Headless integration checks for the sculpted body and its physical attachment.
#include "../src/carvisual.h"
#include "../src/mods.h"
#include "harness.h"
#include <cstdio>
#include <set>

namespace {
int failures = 0;
void check(const char* name, bool ok) {
    std::printf("%s %s\n", ok ? "PASS" : "FAIL", name);
    failures += !ok;
}
V3 point(const BodyDynamic& v) { return V3(v.p[0], v.p[1], v.p[2]); }
V3 normal(const BodyDynamic& v) { return V3(v.n[0], v.n[1], v.n[2]); }
V3 turn(const V3& p) { return V3(-p.z, p.x, -p.y); }
struct Mesh {
    std::vector<BodyDynamic> vertices;
    std::vector<unsigned> opaque, glass;
    Mesh(const Car& c, const VisualTemplate& t) {
        std::vector<V3> P;
        for (const Node& n : c.nodes) P.push_back(n.p);
        evalBody(c,P,t,vertices,opaque,glass);
    }
};
Car factoryCar() {
    Car c;
    c.build(vehicleCatalog()[1], V3(), 0, V3(0.2,0.4,0.6));
    return c;
}
}

int main() {
    Car c=factoryCar();
    const VisualTemplate& model=visualTemplate(c);
    Mesh rest(c,model);
    check("Jesko has an authored surface and glass", !model.sculptBind.empty() && !rest.glass.empty());
    V3 lo(1e9,1e9,1e9),hi(-1e9,-1e9,-1e9);
    bool finite=true;
    for(const auto& v:rest.vertices) {
        V3 p=point(v);
        finite &= std::isfinite(p.x+p.y+p.z);
        lo=V3(std::min(lo.x,p.x),std::min(lo.y,p.y),std::min(lo.z,p.z));
        hi=V3(std::max(hi.x,p.x),std::max(hi.y,p.y),std::max(hi.z,p.z));
    }
    check("shell has finite, correctly scaled factory dimensions", finite &&
          std::fabs(hi.x-lo.x-4.626)<0.003 && std::fabs(hi.z-lo.z-2.030)<0.01 && std::fabs(hi.y-1.210)<0.003);
    std::printf("Shell extent %.6f x %.6f x %.6f m; roof %.6f m\n", hi.x-lo.x, hi.y-lo.y, hi.z-lo.z, hi.y);

    // Translation and rotation catch skins that secretly depend on world-space
    // axes or lose their panel offset when the vehicle tips over.
    V3 offset(31,7,-23);
    for(Node& n:c.nodes)n.p=turn(n.p)+offset;
    Mesh moved(c,model);
    double error=0,normalError=0;
    for(size_t i=0;i<rest.vertices.size();i++) {
        error=std::max(error,len(point(moved.vertices[i])-turn(point(rest.vertices[i]))-offset));
        normalError=std::max(normalError,len(normal(moved.vertices[i])-turn(normal(rest.vertices[i]))));
    }
    check("entire shell follows a rigid vehicle transform",error<0.0001);
    check("surface normals follow vehicle rotation",normalError<0.001);

    c=factoryCar();
    Panel* door=nullptr;
    for(Panel& p:c.panels)if(std::string(p.name)=="LEFT DOOR")door=&p;
    check("left door attachment exists",door!=nullptr);
    if(door) {
        V3 delta(0,0,-1.5);
        for(int p:door->pins)c.pins[p].broken=true;
        for(int n:door->panelNodes)c.nodes[n].p+=delta;
        c.updateStats(); c.updateTris();
        Mesh removed(c,model);
        double windowError=0,oppositeError=0;
        int windows=0;
        for(size_t i=0;i<rest.vertices.size();i++) {
            if((int)model.rvStatic[i].region!=REG_CABIN_SIDE)continue;
            V3 p=point(rest.vertices[i]);
            double d=len(point(removed.vertices[i])-p-(p.z<0?delta:V3()));
            if(p.z<0) {windowError=std::max(windowError,d); windows++;}
            else oppositeError=std::max(oppositeError,d);
        }
        check("detached door carries the complete sculpted window",windows>100 && windowError<0.00001);
        check("opposite door stays on the car",oppositeError<0.00001);
    }

    c=factoryCar();
    for(Panel& panel:c.panels)if(std::string(panel.name)=="HOOD" || std::string(panel.name)=="LEFT DOOR" || std::string(panel.name)=="ENGINE COVER") {
        for(int pin:panel.pins)c.pins[pin].broken=true;
        for(int n:panel.panelNodes)c.nodes[n].p+=V3(0,3,0);
    }
    c.updateStats(); c.updateTris();
    Mesh separated(c,model);
    double stretched=0;
    for(size_t i=0;i<separated.opaque.size();i+=3)for(int e=0;e<3;e++)
        stretched=std::max(stretched,len(point(separated.vertices[separated.opaque[i+e]])-
                                         point(separated.vertices[separated.opaque[i+(e+1)%3]])));
    check("detached panels separate without stretching paint across seams",stretched<0.5);

    bool rideHeightOk=true;
    for(int suspension:{1,2}) {
        ModSet mods; mods.pick[MOD_SUSPENSION]=suspension;
        const VehicleSpec& spec=tunedSpec(1,mods);
        Car lowered; lowered.build(spec,V3(),0,V3());
        const VisualTemplate& altered=visualTemplate(lowered);
        Mesh m(lowered,altered);
        double roof=-1e9;
        for(const auto& v:m.vertices)roof=std::max(roof,(double)v.p[1]);
        rideHeightOk &= std::fabs(roof-(1.21+spec.layerY[0]-0.11))<0.001;
    }
    check("track and lift mods move the authored body with the chassis",rideHeightOk);

    c=factoryCar();
    for(Tri& tr:c.tris)if(tr.mat==MAT_GLASS)tr.shattered=true;
    Mesh shattered(c,model);
    bool allShattered=true;
    for(unsigned v:shattered.glass)allShattered &= shattered.vertices[v].broken>0.5f;
    check("all sculpted windows respond to physical glass breakage",allShattered);
    if(!allShattered) {
        std::set<int> missed;
        for(const auto& f:model.faces)if(f.glass && !c.tris[c.quads[f.parent].tri].shattered)missed.insert(f.parent);
        for(int q:missed)std::printf("Unmapped glass quad %d, material %d, axis %d, side %d, cell %d/%d/%d\n",q,c.quads[q].mat,c.quads[q].axis,c.quads[q].side,c.quads[q].cell[0],c.quads[q].cell[1],c.quads[q].cell[2]);
    }

    Sim sim; sim.setupEmpty(); Ctx ctx(sim);
    Car& crash=ctx.add(vehicleCatalog()[1],100-4.626/2,0,0);
    ctx.focus(crash); ctx.run(1);
    Mesh before(crash,model);
    ctx.launch(crash,50/3.6);
    bool crashFinite=true;
    double maxVisibleEdge=0;
    ctx.run(2.5,[&] {
        Mesh m(crash,model);
        for(const auto& v:m.vertices)crashFinite &= std::isfinite(v.p[0]+v.p[1]+v.p[2]);
        for(size_t i=0;i<m.opaque.size();i+=3)for(int e=0;e<3;e++)
            maxVisibleEdge=std::max(maxVisibleEdge,len(point(m.vertices[m.opaque[i+e]])-point(m.vertices[m.opaque[i+(e+1)%3]])));
    });
    Mesh after(crash,model);
    // Distance between two surface landmarks is independent of the car's motion.
    double changed=0;
    for(size_t i=1;i<before.vertices.size();i++)
        changed=std::max(changed,std::fabs(len(point(before.vertices[i])-point(before.vertices[0]))-
                                         len(point(after.vertices[i])-point(after.vertices[0]))));
    check("50 km/h crash deforms the authored surface",changed>0.20);
    check("crash surface remains finite without long visual spikes",crashFinite && maxVisibleEdge<1.0);
    bool physicsOk=true;
    for(const auto& r:ctx.checks)physicsOk &= r.ok;
    check("mesh crash respects physics invariants",physicsOk);
    std::printf("Visual checks: %d failures (max edge %.3f m, deformation %.3f m)\n",failures,maxVisibleEdge,changed);
    return failures?1:0;
}
