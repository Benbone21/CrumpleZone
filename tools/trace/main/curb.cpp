#define private public
#include "../../../tests/harness.h"
#include "../../../src/vehicles.h"
#include <cstdio>
// Slides a vehicle sideways into a curb at a speed (km/h); reports peak stud load and sheds.
int main(int argc,char**argv){ int vi=argc>1?atoi(argv[1]):2; double kmh=argc>2?atof(argv[2]):40; double ch=argc>3?atof(argv[3]):0.25;
 const VehicleSpec& spec=vehicleCatalog()[vi]; Sim sim; Ctx ctx(sim); World& w=sim.world;
 w.addBox(V3(-100, w.height(-100,-44)+ch/2, -44), V3(8, ch/2, 0.3), 0,0,0, V3(0.5,0.5,0.5)); w.boxes.back().rock=true; w.buildGrid();
 Car& c=ctx.add(spec,-100,-50,0); ctx.focus(c); ctx.run(1.0);
 for(Node&n:c.nodes) n.v=V3(0,0,kmh/3.6);
 double peak[4]={0,0,0,0}; int shed=0;
 ctx.run(3.0,[&]{ for(int k=0;k<4;k++){ peak[k]=std::max(peak[k],(double)c.wheels[k].studLoad); } });
 for(auto&wh:c.wheels) shed+=!wh.mounted;
 printf("%s %.0f km/h curb %.2f: peak stud %.0f %.0f %.0f %.0f kN-ish, shed %d, loose %zu, lugShear %.0f, up %.2f\n",spec.name,kmh,ch,peak[0]/1e3,peak[1]/1e3,peak[2]/1e3,peak[3]/1e3,shed,sim.looseWheels.size(),spec.lugShear,c.frame().up.y);
}
