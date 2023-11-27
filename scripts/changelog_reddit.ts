import { mapValues } from "$std/collections/map_values.ts"
import { parse } from "https://deno.land/x/commit@0.1.5/mod.ts"
import { titleCase } from "https://deno.land/x/case@2.1.1/mod.ts"
import { TypedRegEx } from "https://raw.githubusercontent.com/scarf005/typed_regex/e98cebaeeea58d7a61bc6aa12243459484870c08/src/index.ts"

import changelog from "./changelog.json" with { type: "json" }

const regex = TypedRegEx("(?<title>.*?)\\(#(?<pr>\\d+)\\)$")

const withLink = ({ subject, author }: { subject: string; author: string }) => {
  const { title, pr } = regex.match(subject).groups!
  const entry =
    `- [#${pr}](https://github.com/cataclysmbnteam/Cataclysm-BN/pull/${pr}) **${title.trim()}** by ${author}.`

  return { pr: parseInt(pr, 10), entry }
}

const byTypes = Object.groupBy(changelog, ({ subject }) => parse(subject).type!)
const messages = mapValues(byTypes, (xs) =>
  xs!
    .map(withLink)
    .toSorted((a, b) => a.pr - b.pr)
    .map(({ entry }) => entry)
    .join("\n"))

const asSections = (group: Record<string, string>) =>
  Object.entries(group)
    .map(([type, message]) => `### ${titleCase(type)}\n\n${message}`)
    .join("\n\n")

const byAuthors = Object.groupBy(changelog, ({ author }) => author)
const authors = mapValues(
  byAuthors,
  (xs) => xs!.map(({ subject }) => parse(subject).subject).join("\n"),
)

const isoDate = (date: Date) => date.toISOString().split("T")[0]
const date = isoDate(new Date())

// template tag function
// convert newline into space, only if it's not consecutive
const paragraph = (strings: TemplateStringsArray, ...values: string[]) =>
  strings
    .map((str, i) => str + (values[i] ?? ""))
    .join("")
    .replace(/\n(?!\n)/g, " ")
    .replaceAll("\n", "\n\n")

const title = "Power Armor reworks, JSON updates and more!"

const desc = /*md*/ paragraph`
It's been 3 weeks since item identity refactor.
Thanks to Jove, many bugs have been fixed and latest experimental builds should be much more stable.
There's still some bugs left, but we're working on it.

Vollch has been working on overmap, which means many hardcoded maps have been migrated to JSON.
This means creating complex maps are now much easier than before.

New JSON flags for modders are added, too.
Armors with \`HEAVY_WEAPON_SUPPORT\` flag will let you fire minigun without mounts!
Currently power armors are the only armors with this flag, but modders can add this flag to any armors.
Also, now monsters can have armor for cold and electricity.
`

const thanks = /*md*/ `
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
`

const releaseNote = /*md*/ `
# CBN Changelog: ${date}. ${title}

https://preview.redd.it/kukz23r0cszb1.png?width=660&format=png&auto=webp&s=8039aa5748462b40570b8ba98600f5f7359f5320

**Changelog for Cataclysm: Bright Nights.**

**Changes for:** 2023-11-11/${date}.

- **Bright Nights discord server link:** https://discord.gg/XW7XhXuZ89
- **Bright Nights launcher/updater (also works for DDA!) by qrrk:** https://github.com/qrrk/Catapult/releases
- **Bright Nights launcher/updater by 4nonch:** https://github.com/4nonch/BN---Primitive-Launcher/releases
- **TheAwesomeBoophis' UDP revival project:** https://discord.gg/mSATZeZmjz

${desc}

## With thanks to

${thanks}
And to all others who contributed to making these updates possible!

## Changelog

${asSections(messages)}

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
`

console.log(
  asSections(
    Object.fromEntries(Object.entries(authors).toSorted(([a], [b]) => a.localeCompare(b))),
  ),
)
await Deno.writeTextFile("changelog.md", releaseNote)
