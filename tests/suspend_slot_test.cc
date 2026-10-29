// Reserved suspend slots cannot be allocated to manual or rotating saves.
#include "save_catalog.h"
#include "save_storage.h"
#include <cstdio>
#include <cstdlib>
#include <string>

// Keep checks active in Release builds too.
#define CHECK(condition) do { if (!(condition)) { std::fprintf(stderr, "FAIL at %d: %s\n", __LINE__, #condition); std::abort(); } } while (false)

int main()
{
    char path[] = "/tmp/f2-suspend-slot-XXXXXX";
    CHECK(mkdtemp(path));
    std::string root(path);
    std::string snapshot(1024, 'x');
    snapshot.replace(0, 17, "FALLOUT SAVE FILE");
    fallout::SaveCatalog catalog;
    catalog.configure(root, 4, 0, 4);
    catalog.reserveSlot(3);
    CHECK(catalog.refresh());
    CHECK(catalog.write(3, [&] { return fallout::saveStorage::writeFile(root + "/SLOT04/SAVE.DAT", snapshot); }));
    CHECK(!catalog.isQuick(3));
    CHECK(catalog.nextQuick() != 3);
    CHECK(catalog.setQuickRange(2, 2));
    CHECK(catalog.entry(3).present);
    CHECK(catalog.nextQuick() == 2);
    CHECK(catalog.freeManual() == 0);
    CHECK(catalog.setQuickRange(0, 0));
    for (int slot = 0; slot < 3; ++slot) {
        CHECK(catalog.freeManual() == slot);
        CHECK(catalog.write(slot, [&] { return fallout::saveStorage::writeFile(catalog.saveDatPath(slot), snapshot); }));
    }
    CHECK(catalog.freeManual() == -1);
    CHECK(fallout::saveStorage::removeTree(root));
}
