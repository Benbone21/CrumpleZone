#define private public
#include "../../../tests/harness.h"
#include "../../../src/vehicles.h"
#include <cstdio>
int main(int argc,char**argv){ const VehicleSpec& spec=vehicleCatalog()[3]; Sim sim; Ctx ctx(sim); Car& c=ctx.add(spec,0,0,0);
 int bi=argc>1?atoi(argv[1]):2213; const Beam&b=c.beams[bi];
 double xr=1e9; for(auto&w:c.wheels) if(!w.front) xr=std::min(xr,c.restLocal[w.inner].x);
 printf("beam %d type %d a %d (%.3f %.3f %.3f) b %d (%.3f %.3f %.3f) yield %.4f k %.0f xr(inner) %.3f frameNodes %d body %d\n",bi,b.type,b.a,c.restLocal[b.a].x,c.restLocal[b.a].y,c.restLocal[b.a].z,b.b,c.restLocal[b.b].x,c.restLocal[b.b].y,c.restLocal[b.b].z,b.yieldStrain,b.k,xr,c.frameNodes,c.bodyNodes);
 for(size_t i=0;i<c.beams.size();i++){const Beam&q=c.beams[i]; if(q.type==BEAM_FRAME && (q.a==b.a||q.b==b.a)) printf("  %zu: %d-%d yield %.4f\n",i,q.a,q.b,q.yieldStrain);} }
