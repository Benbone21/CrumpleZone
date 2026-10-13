#define private public
#include "../../../tests/harness.h"
#include "../../../src/vehicles.h"
#include <cstdio>
int main(int argc,char**argv){ int vi=argc>1?atoi(argv[1]):4; const VehicleSpec& spec=vehicleCatalog()[vi]; Sim sim; Ctx ctx(sim); Car& c=ctx.add(spec,0,0,0); ctx.run(1.0);
 printf("bumpRear %.3f bumpFront %.3f wheelR %.3f\n",spec.bumpRear,spec.bumpFront,spec.wheelRadius);
 for(int k=0;k<4;k++){const Wheel&w=c.wheels[k]; printf("wheel %d front %d spring %d bumpStop %d\n",k,w.front,w.spring,w.bumpStop);
  if(w.spring>=0){const Beam&s=c.beams[w.spring]; double L=len(c.nodes[s.a].p-c.nodes[s.b].p); printf("  spring k %.0f L %.3f L0 %.3f minL %.3f maxL %.3f rest %.3f\n",s.k,L,s.L0,s.minL,s.maxL,w.springRest);}
  if(w.bumpStop>=0){const Beam&s=c.beams[w.bumpStop]; double L=len(c.nodes[s.a].p-c.nodes[s.b].p); printf("  stop k %.0f c %.0f L %.3f minL %.3f type %d a %d b %d\n",s.k,s.c,L,s.minL,s.type,s.a,s.b);}}
 int n=0; for(size_t i=0;i<c.beams.size();i++) if(c.beams[i].type==BEAM_BUMPSTOP){const Beam&s=c.beams[i]; double L=len(c.nodes[s.a].p-c.nodes[s.b].p); printf(" BUMPSTOP %zu k %.0f L %.3f minL %.3f gap %.3f\n",i,s.k,L,s.minL,L-s.minL); n++;}
}
