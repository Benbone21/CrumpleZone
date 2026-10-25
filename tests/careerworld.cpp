// Career worlds: difficulty start cash, which vehicles can start a career, folder names.
#include "../src/careerworld.h"
#include "../src/vehicles.h"
#include "harness.h"

SCENARIO(careerWorldSettings, "career/world-settings", "Start cash by difficulty; the Jesko can't be a starter; world slugs are folder-safe, unique and short") {
    ctx.check("light starts with 2000", startCash(DIFF_LIGHT) == 2000);
    ctx.check("medium starts with 1000", startCash(DIFF_MEDIUM) == 1000);
    ctx.check("hardcore starts with 500", startCash(DIFF_HARDCORE) == 500);
    ctx.check("difficulty name", std::string(difficultyName(DIFF_HARDCORE)) == "HARDCORE");
    ctx.check("crew mode name", std::string(modeName(MODE_CREW)) == "SHARED CREW");
    ctx.check("personal mode name", std::string(modeName(MODE_PERSONAL)) == "PERSONAL");

    int jesko = -1;
    for (int v = 0; v < (int)vehicleCatalog().size(); v++)
        if (std::string(vehicleCatalog()[v].name) == "KOENIGSEGG JESKO") jesko = v;
    ctx.check("the catalog has the Jesko", jesko >= 0);
    ctx.check("the Jesko is not a starter", !startableVehicle(jesko));
    bool allOthers = true;
    for (int v : listedVehicles())
        if (v != jesko && !startableVehicle(v)) allOthers = false;
    ctx.check("every other listed vehicle is a starter", allOthers);

    ctx.check("plain name", worldSlug("My World", {}) == "my-world");
    ctx.check("taken gets -2", worldSlug("my world!", {"my-world"}) == "my-world-2");
    ctx.check("taken twice gets -3", worldSlug("my world!", {"my-world", "my-world-2"}) == "my-world-3");
    ctx.check("empty name", worldSlug("", {}) == "world");
    ctx.check("long name is cut", worldSlug(std::string(50, 'a'), {}).size() <= 32);
    ctx.check("long taken name stays short", worldSlug(std::string(50, 'a'), {std::string(32, 'a')}).size() <= 32);
}
