#define private public
#include "../../../tests/harness.h"
#include "../../../src/vehicles.h"
#include <cstdio>
#include <cmath>
#include <cstring>
#include <algorithm>
#include <vector>
#include <cstdlib>
int main(int argc,char**argv){ double h=argc>1?atof(argv[1]):2.0; int vi=argc>2?atoi(argv[2]):4;
 static VehicleSpec spec=vehicleCatalog()[vi]; if(getenv("BW")){ spec.bodyWidth.clear(); for(char*t=strtok(getenv("BW"),",");t;t=strtok(nullptr,",")) spec.bodyWidth.push_back(atof(t)); } Sim sim; Ctx ctx(sim); Car& c=ctx.add(spec,-100,-50,0); ctx.focus(c); ctx.run(1.0);
 if(getenv("PITCH")) c.rotateRigid(c.frame().left, atof(getenv("PITCH"))*M_PI/180);
 for(Node&n:c.nodes) n.p.y+=h, n.v=V3();
 const Axle* ax=nullptr; for(const Axle&a:c.axles) if(!a.front&&!a.housing.empty()) ax=&a;
 printf("housing nodes %zu; links:\n",ax->housing.size());
 auto rel=[&](int n){ Frame fr=c.frame(); V3 d=c.nodes[n].p-fr.com; return V3(dot(d,fr.fwd),dot(d,fr.up),-dot(d,fr.left)); };
 V3 d0=rel(ax->housing[0]), t0=rel(ax->housing[2]); double p0=atan2(-(t0.x-d0.x),t0.y-d0.y);
 for(int s=0;s<=60;s++){ ctx.run(0.05);
   V3 d=rel(ax->housing[0]), t=rel(ax->housing[2]); double p=(atan2(-(t.x-d.x),t.y-d.y)-p0)*57.3;
   int broken=0; for(const SuspLink&l:c.links) if(c.beams[l.beam].broken) broken++;
   int bb=0; for(auto&b:c.beams) if(b.broken) bb++;
   double bs[2]={0,0}; for(int k=2;k<4;k++){const Wheel&w=c.wheels[k]; if(w.bumpStop>=0){const Beam&q=c.beams[w.bumpStop]; double L=len(c.nodes[q.a].p-c.nodes[q.b].p); bs[k-2]=L<q.minL?q.k*(q.minL-L):0;}}
   { V3 hl=rel(c.wheels[2].inner), hr=rel(c.wheels[3].inner); const Beam&q=c.beams[c.wheels[2].bumpStop]; V3 sn=rel(q.a==c.wheels[2].inner?q.b:q.a); printf("   hubL (%.2f %.2f %.2f) hubR (%.2f %.2f %.2f) stopnodeL (%.2f %.2f %.2f) stopL %.3f/%.3f\n",hl.x,hl.y,hl.z,hr.x,hr.y,hr.z,sn.x,sn.y,sn.z,len(c.nodes[q.a].p-c.nodes[q.b].p),q.minL);}
   printf("t %.2f pitch %+6.1f diff (%.2f %.2f) top (%.2f %.2f) brokenLinks %d brokenBeams %d stops %.0f %.0f loads %.0f %.0f vy %.2f\n",ctx.t,p,d.x,d.y,t.x,t.y,broken,bb,bs[0],bs[1],c.wheels[2].load,c.wheels[3].load,c.frame().vel.y);
 } 
 std::vector<std::pair<double,int>> v; for(size_t i=0;i<c.beams.size();i++){const Beam&b=c.beams[i]; double st=fabs(b.L0-b.Lorig)/b.Lorig; if(st>0.02) v.push_back({st,(int)i});}
 std::sort(v.rbegin(),v.rend()); printf("set beams %zu\n",v.size());
 for(size_t k=0;k<v.size()&&k<25;k++){const Beam&b=c.beams[v[k].second]; V3 a=c.restLocal[b.a],bb=c.restLocal[b.b]; printf(" set %.1f%% beam %d type %d %d(%.2f %.2f %.2f)-%d(%.2f %.2f %.2f) body<%d frameN<%d\n",v[k].first*100,v[k].second,b.type,b.a,a.x,a.y,a.z,b.b,bb.x,bb.y,bb.z,c.bodyNodes,c.frameNodes);}
 for(const SuspLink&l:c.links){const Beam&b=c.beams[l.beam]; if(l.wheel>=0 && c.wheels[l.wheel].front) continue; V3 a=c.restLocal[b.a],bb=c.restLocal[b.b]; printf(" link kind %d wheel %d %d(%.2f %.2f %.2f)-%d(%.2f %.2f %.2f) L/L0 %.3f\n",l.kind,l.wheel,b.a,a.x,a.y,a.z,b.b,bb.x,bb.y,bb.z,len(c.nodes[b.a].p-c.nodes[b.b].p)/b.L0);}
}
