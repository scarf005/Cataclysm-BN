
# CBN Changelog: 2023-12-26. Power Armor reworks, JSON updates and more!

https://preview.redd.it/kukz23r0cszb1.png?width=660&format=png&auto=webp&s=8039aa5748462b40570b8ba98600f5f7359f5320

**Changelog for Cataclysm: Bright Nights.**

**Changes for:** 2023-11-11/2023-12-26.

- **Bright Nights discord server link:** https://discord.gg/XW7XhXuZ89
- **Bright Nights launcher/updater (also works for DDA!) by qrrk:** https://github.com/qrrk/Catapult/releases
- **Bright Nights launcher/updater by 4nonch:** https://github.com/4nonch/BN---Primitive-Launcher/releases
- **TheAwesomeBoophis' UDP revival project:** https://discord.gg/mSATZeZmjz

 It's been 3 weeks since item identity refactor. Thanks to Jove, many bugs have been fixed and latest experimental builds should be much more stable. There's still some bugs left, but we're working on it.

 Vollch has been working on overmap, which means many hardcoded maps have been migrated to JSON. This means creating complex maps are now much easier than before.

 New JSON flags for modders are added, too. Armors with `HEAVY_WEAPON_SUPPORT` flag will let you fire minigun without mounts! Currently power armors are the only armors with this flag, but modders can add this flag to any armors. Also, now monsters can have armor for cold and electricity. 

## With thanks to


**0Monet** for multiple mapgen fixes.
**Ali Anomma** for JSONized pumps.
**Chaosvolt** for power armor updates and various balance updates.
**Chorus System** for various balance changes and martial arts fixes.
**Fruitybite** for adding coca tree description.
**JoveEater** for numerous bug fixes regarding item identity refactor.
**Kheir Ferrum** for energy weapon QOL update.
**Lil Shining Man** for 6 new house layouts.
**NobleJake** for cacao tree graphics and chocolate recipe update.
**Olanti** for build improvements, crash fixes, and PR template updates.
**RoyalFox** for adding sleepdebt effect.
**Scarf** for numerous QOL updates and build fixes.
**Vollch** for numerous contribution to mutable overmap migration.
**YeOldeMiller** for adjusting biodiesel recipe.
**Zlorthishen** for vehicle side mirrors.

And to all others who contributed to making these updates possible!

## Changelog

### Feat

- [#3520](https://github.com/cataclysmbnteam/Cataclysm-BN/pull/3520) **feat(port): mutable specials and unhardcored labs and anthills** by Vollch.
- [#3637](https://github.com/cataclysmbnteam/Cataclysm-BN/pull/3637) **feat: Liquid summoned by spells can be stored and used** by Vollch.
- [#3638](https://github.com/cataclysmbnteam/Cataclysm-BN/pull/3638) **feat(content): house_fence07** by Lil Shining Man.
- [#3649](https://github.com/cataclysmbnteam/Cataclysm-BN/pull/3649) **feat(balance): Constructable furniture consistency updates** by Chaosvolt.
- [#3650](https://github.com/cataclysmbnteam/Cataclysm-BN/pull/3650) **feat(content): bungalow 26** by Lil Shining Man.
- [#3653](https://github.com/cataclysmbnteam/Cataclysm-BN/pull/3653) **feat(balance): Rebalanced occurrences of some specials** by Vollch.
- [#3659](https://github.com/cataclysmbnteam/Cataclysm-BN/pull/3659) **feat(content): cacao tree graphics update** by NobleJake.
- [#3660](https://github.com/cataclysmbnteam/Cataclysm-BN/pull/3660) **feat(content): made some bionics better** by Chorus System.
- [#3663](https://github.com/cataclysmbnteam/Cataclysm-BN/pull/3663) **feat(content): 2Story01** by Lil Shining Man.
- [#3670](https://github.com/cataclysmbnteam/Cataclysm-BN/pull/3670) **feat(interface): new UDP external tileset sprite for signs** by Chaosvolt.
- [#3673](https://github.com/cataclysmbnteam/Cataclysm-BN/pull/3673) **feat(content): Add sleepdebt effect** by RoyalFox.
- [#3675](https://github.com/cataclysmbnteam/Cataclysm-BN/pull/3675) **feat(content): 2Story02** by Lil Shining Man.
- [#3688](https://github.com/cataclysmbnteam/Cataclysm-BN/pull/3688) **feat: Convert non-pistol only energy weapon mods to be compatible with all energy weapons.** by KheirFerrum.
- [#3691](https://github.com/cataclysmbnteam/Cataclysm-BN/pull/3691) **feat(balance): `HEAVY_WEAPON_SUPPORT` flag for large mutation and power armors** by Chaosvolt.
- [#3697](https://github.com/cataclysmbnteam/Cataclysm-BN/pull/3697) **feat(balance): MILITARY_MECH flag affects ID card needed, fix duplicate message** by Chaosvolt.
- [#3700](https://github.com/cataclysmbnteam/Cataclysm-BN/pull/3700) **feat(content): house_coop01** by Lil Shining Man.
- [#3703](https://github.com/cataclysmbnteam/Cataclysm-BN/pull/3703) **feat(content): debuff war scythe damage, buff crafting time** by Chorus System.
- [#3705](https://github.com/cataclysmbnteam/Cataclysm-BN/pull/3705) **feat(content): swap travois and wood box size** by Chorus System.
- [#3706](https://github.com/cataclysmbnteam/Cataclysm-BN/pull/3706) **feat(i18n): routine i18n updates on 2023-11-18** by Coolthulhu.
- [#3707](https://github.com/cataclysmbnteam/Cataclysm-BN/pull/3707) **feat(balance): adjust biodiesel recipe for more reasonable crafting time** by YeOldeMiller.
- [#3708](https://github.com/cataclysmbnteam/Cataclysm-BN/pull/3708) **feat(balance): chocolate recipe update** by NobleJake.
- [#3711](https://github.com/cataclysmbnteam/Cataclysm-BN/pull/3711) **feat(content): vehicle side mirrors** by Zlorthishen.
- [#3716](https://github.com/cataclysmbnteam/Cataclysm-BN/pull/3716) **feat(content): Biodiesel Fuel for Gasoline Engines** by Chorus System.
- [#3719](https://github.com/cataclysmbnteam/Cataclysm-BN/pull/3719) **feat(balance): Bionic zombies generate CBMs less often, harvesting of what generates made more reliable** by Chaosvolt.
- [#3733](https://github.com/cataclysmbnteam/Cataclysm-BN/pull/3733) **feat(balance): update capacity of power armor holsters** by Chaosvolt.
- [#3747](https://github.com/cataclysmbnteam/Cataclysm-BN/pull/3747) **feat(i18n): routine i18n updates on 2023-11-25** by Coolthulhu.
- [#3750](https://github.com/cataclysmbnteam/Cataclysm-BN/pull/3750) **feat: monster armor for `cold` and `electric`** by scarf.
- [#3751](https://github.com/cataclysmbnteam/Cataclysm-BN/pull/3751) **feat: allow using `MOUNTABLE` terrains nearby when firing heavy weapon** by scarf.
- [#3752](https://github.com/cataclysmbnteam/Cataclysm-BN/pull/3752) **feat: mop 3x3 area around** by scarf.

### Fix

- [#3614](https://github.com/cataclysmbnteam/Cataclysm-BN/pull/3614) **fix: calculate modes in gunmods too** by scarf.
- [#3639](https://github.com/cataclysmbnteam/Cataclysm-BN/pull/3639) **fix: Better attempt detach logic** by joveeater.
- [#3641](https://github.com/cataclysmbnteam/Cataclysm-BN/pull/3641) **fix: Add harvestable coca tree description** by Fruitybite.
- [#3642](https://github.com/cataclysmbnteam/Cataclysm-BN/pull/3642) **fix: food rotting incorrectly** by joveeater.
- [#3643](https://github.com/cataclysmbnteam/Cataclysm-BN/pull/3643) **fix: save/load breaking effect of relic stat boosts** by Chaosvolt.
- [#3646](https://github.com/cataclysmbnteam/Cataclysm-BN/pull/3646) **fix: red text when refilling cars** by joveeater.
- [#3652](https://github.com/cataclysmbnteam/Cataclysm-BN/pull/3652) **fix: update eye encumbrance, infravision when transforming items** by Chaosvolt.
- [#3654](https://github.com/cataclysmbnteam/Cataclysm-BN/pull/3654) **fix(content): palette error in Bungalow26** by Lil Shining Man.
- [#3655](https://github.com/cataclysmbnteam/Cataclysm-BN/pull/3655) **fix(content): `NON_FOULING` flag not working due to typo** by scarf.
- [#3661](https://github.com/cataclysmbnteam/Cataclysm-BN/pull/3661) **fix: migration of acid anthills** by Vollch.
- [#3666](https://github.com/cataclysmbnteam/Cataclysm-BN/pull/3666) **fix: mutable copy-from** by Vollch.
- [#3668](https://github.com/cataclysmbnteam/Cataclysm-BN/pull/3668) **fix: don't allow cutting up corpses** by Chaosvolt.
- [#3678](https://github.com/cataclysmbnteam/Cataclysm-BN/pull/3678) **fix: crash when trying to load a world while missing a mod** by Olanti.
- [#3680](https://github.com/cataclysmbnteam/Cataclysm-BN/pull/3680) **fix: mass_grave map extras** by 0Monet.
- [#3681](https://github.com/cataclysmbnteam/Cataclysm-BN/pull/3681) **fix: sewer rework** by Vollch.
- [#3682](https://github.com/cataclysmbnteam/Cataclysm-BN/pull/3682) **fix(content): correct electric firestarter recipe** by Chorus System.
- [#3690](https://github.com/cataclysmbnteam/Cataclysm-BN/pull/3690) **fix(balance): Adjustments to bastion fort** by Chaosvolt.
- [#3693](https://github.com/cataclysmbnteam/Cataclysm-BN/pull/3693) **fix: another fix for anthill migration** by Vollch.
- [#3694](https://github.com/cataclysmbnteam/Cataclysm-BN/pull/3694) **fix: Crash in npc activities** by joveeater.
- [#3701](https://github.com/cataclysmbnteam/Cataclysm-BN/pull/3701) **fix: Indefinite vehicle refill** by joveeater.
- [#3702](https://github.com/cataclysmbnteam/Cataclysm-BN/pull/3702) **fix: campground mapgen** by 0Monet.
- [#3704](https://github.com/cataclysmbnteam/Cataclysm-BN/pull/3704) **fix: rotation of signs & graffity** by Vollch.
- [#3709](https://github.com/cataclysmbnteam/Cataclysm-BN/pull/3709) **fix: potential fix for liquid iexamine** by Vollch.
- [#3715](https://github.com/cataclysmbnteam/Cataclysm-BN/pull/3715) **fix: farm mapgen** by 0Monet.
- [#3720](https://github.com/cataclysmbnteam/Cataclysm-BN/pull/3720) **fix(balance): no more EXP gain when a skill is maxed out** by Chaosvolt.
- [#3724](https://github.com/cataclysmbnteam/Cataclysm-BN/pull/3724) **fix: toxic dump mapgen** by 0Monet.
- [#3726](https://github.com/cataclysmbnteam/Cataclysm-BN/pull/3726) **fix: Fix light and helmet power helms having each other's power draw** by Chaosvolt.
- [#3728](https://github.com/cataclysmbnteam/Cataclysm-BN/pull/3728) **fix: martial arts damage buffs apply to all damage types** by Chorus System.
- [#3732](https://github.com/cataclysmbnteam/Cataclysm-BN/pull/3732) **fix: specials spawned for missions will respect min range** by Vollch.
- [#3736](https://github.com/cataclysmbnteam/Cataclysm-BN/pull/3736) **fix: bad iterator message in chopping logs** by joveeater.
- [#3737](https://github.com/cataclysmbnteam/Cataclysm-BN/pull/3737) **fix: bad pump iterator** by joveeater.
- [#3741](https://github.com/cataclysmbnteam/Cataclysm-BN/pull/3741) **fix: potential fix for stack overflow in debug MSVS builds** by Vollch.
- [#3742](https://github.com/cataclysmbnteam/Cataclysm-BN/pull/3742) **fix: mopping floors** by joveeater.
- [#3743](https://github.com/cataclysmbnteam/Cataclysm-BN/pull/3743) **fix: movie_theater mapgen** by 0Monet.
- [#3755](https://github.com/cataclysmbnteam/Cataclysm-BN/pull/3755) **fix: use env. resist for `stun`, `blind`, `sap`, `paralysis`** by scarf.

### Ci

- [#3536](https://github.com/cataclysmbnteam/Cataclysm-BN/pull/3536) **ci: semantic PR title via conventional commits** by scarf.
- [#3595](https://github.com/cataclysmbnteam/Cataclysm-BN/pull/3595) **ci: release daily** by scarf.
- [#3615](https://github.com/cataclysmbnteam/Cataclysm-BN/pull/3615) **ci: bump dmgbuild to 1.6.1** by scarf.
- [#3676](https://github.com/cataclysmbnteam/Cataclysm-BN/pull/3676) **ci: temporary disable ASan/UBSan builds** by Olanti.
- [#3685](https://github.com/cataclysmbnteam/Cataclysm-BN/pull/3685) **ci: fix autofix bot not adding comments** by scarf.
- [#3749](https://github.com/cataclysmbnteam/Cataclysm-BN/pull/3749) **ci: bump vcpkg version to fix MSVC build** by scarf.

### Perf

- [#3729](https://github.com/cataclysmbnteam/Cataclysm-BN/pull/3729) **perf: specials placement optimizations** by Vollch.

### Refactor

- [#3633](https://github.com/cataclysmbnteam/Cataclysm-BN/pull/3633) **refactor(content): JSONize Pumps** by Ali-Anomma.
- [#3657](https://github.com/cataclysmbnteam/Cataclysm-BN/pull/3657) **refactor(content): Convert slimepit special to mutable** by Vollch.
- [#3696](https://github.com/cataclysmbnteam/Cataclysm-BN/pull/3696) **refactor(content): convert mine special to mutable** by Vollch.

### Chore

- [#3677](https://github.com/cataclysmbnteam/Cataclysm-BN/pull/3677) **chore: adjusted PR template per feedback, stylistic changes** by Olanti.

### Build

- [#3457](https://github.com/cataclysmbnteam/Cataclysm-BN/pull/3457) **build(port): CMake improvements** by Olanti.
- [#3674](https://github.com/cataclysmbnteam/Cataclysm-BN/pull/3674) **build: use import map for ts imports** by scarf.

## Links

- **Previous changelog:** https://www.reddit.com/r/cataclysmbn/comments/17t44xk/cbn_changelog_november_11_2023_item_identity/
- **Changes so far:** https://github.com/cataclysmbnteam/Cataclysm-BN/wiki/Changes-so-far
- **Download:** https://github.com/cataclysmbnteam/Cataclysm-BN/releases
- **Bugs and suggestions can be posted here:** https://github.com/cataclysmbnteam/Cataclysm-BN/issues

## How to help:

https://docs.cataclysmbn.org/en/contribute/contributing/

- **Translations!** https://www.transifex.com/bn-team/cataclysm-bright-nights/
- **Contributing via code changes.**
- **Contributing via JSON changes.** Yes, we need modders and content makers help.
- **Contributing via rebalancing content.**
- **Reporting bugs. Including ones inherited from DDA.**
- Identifying problems that aren't bugs. Misleading descriptions, values that are clearly off compared to similar cases, grammar mistakes, UI wonkiness that has an obvious solution.
- Making useless things useful or putting them on a blacklist. Adding deconstruction recipes for things that should have them but don't, replacing completely redundant items with their generic versions (say, "tiny marked bottle" with just "tiny bottle") in spawn lists.
- Tileset work. We're occasionally adding new objects, like the new electric grid elements, and they could use new tiles.
- Balance analysis. Those should be rather in depth or "obviously correct". Obviously correct would be things like: "weapon x has strictly better stats than y, but y requires rarer components and has otherwise identical requirements".
- Identifying performance bottlenecks with a profiler.
- Code quality help.
