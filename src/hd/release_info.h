#ifndef FALLOUT_HD_RELEASE_INFO_H_
#define FALLOUT_HD_RELEASE_INFO_H_

// The release's name and version (PLAN.md R4), in one place. The engine shows
// them in debug.log and the developer overlay; tools/scripts/release.ps1 reads
// them from here for the zip names and the player docs. The name is
// provisional (the author, 2026-11-04: "Dustlight for now").
#define HD_RELEASE_NAME "Dustlight"
#define HD_RELEASE_VERSION "0.9.2-beta"

// The same version as four numbers, for the exe's version block
// (os/windows/fallout2-ce.rc). release.ps1 refuses to build if they disagree.
#define HD_RELEASE_VERSION_NUMBERS 0, 9, 2, 0

#endif /* FALLOUT_HD_RELEASE_INFO_H_ */
