# Profiles for profile guided optimization

This folder is empty of profiles on purpose. It will hold the [PGO](../docs/glossary.md#pgo) profiles for
Call of Duty 2, one folder per edition, once they have been recorded on the console.

The profiles that came with the fork were removed: a GCC profile matches each function by a hash of its name, and
every recompiled function is named after its [guest](../docs/glossary.md#guest) address (`__imp__sub_XXXXXXXX`). The
addresses in those files belong to Need for Speed: Most Wanted, so no part of them can be used here.

Each file is a `.gcda` file (where GCC keeps its counters). Its name is the path of an object file relative to the
build folder, with `#` instead of `/`, so GCC finds it as long as the build folder keeps the same layout.

## How to record a profile for this game

1. Configure with `-DCOD2_PGO=generar` and build.
2. Play. The NRO writes its counters to `sdmc:/switch/cod2-nx/pgo/` every three minutes.
3. Copy the `.gcda` files into `pgo/<edition>/`.
4. Build again with `-DCOD2_PGO=usar`.

How to use a profile and the traps of PGO with GCC on the Switch are in [docs/building.md](../docs/building.md) and
[docs/toolchain.md](../docs/toolchain.md).
