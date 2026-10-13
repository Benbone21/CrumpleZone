#define private public
#include "../../../tests/harness.h"
#include "../../../src/vehicles.h"
#include <cstdio>
#include <map>
int main(int argc,char**argv){ int vi=argc>1?atoi(argv[1]):3; const VehicleSpec& spec=vehicleCatalog()[vi]; Sim sim; Ctx ctx(sim); Car& c=ctx.add(spec,0,0,0);
 for(int r=0;r<2;r++){ auto& rf=c.railFrame[r]; std::map<int,std::pair<double,int>> st; std::map<int,double> sx;
  for(size_t k=0;k<rf.nodes.size();k++){ int s=rf.section[k]; st[s].first+=c.nodes[rf.nodes[k]].m; st[s].second++; sx[s]=c.restLocal[rf.nodes[k]].x; }
  printf("rail %d kappa %.0f yield %.4f\n",r,rf.kappa,rf.yieldDist);
  for(auto&e:st) if(e.second.second>=4) printf("  sec %d x %.2f mass %.2f kg (%d nodes) yieldF %.0f N\n",e.first,sx[e.first],e.second.first,e.second.second, rf.kappa*e.second.first*rf.yieldDist);
  break; }
}
