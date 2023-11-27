// git log cbn-0.4..8870a9e64800a01024d50d36fdfd8059a61ab6ed --oneline --pretty=format:'{%n  "subject": "%s",%n  "author": "%an",%n  "date": "%ad"%n},' > scripts/changelog.json

import { titleCase, upperCase } from "https://deno.land/x/case@2.1.1/mod.ts"
import changelog from "./changelog.json" with { type: "json" }
import outdent from "$outdent/mod.ts"
import { typedRegEx } from "https://deno.land/x/typed_regex@0.1.0/mod.ts"

export const re = typedRegEx(
  "^(?<type>\\w+)(\\((?<scopes>.*)\\))?(?<breaking>!)?:\\s*(?<desc>.*?)\\s*\\(#(?<pr>\\d+)\\)$",
)

export const parseCommit = (x: string) => {
  const res = re.captures(x)
  if (!res) throw new Error(`Failed to parse commit: ${x}`)

  const { type, desc, scopes, pr, breaking } = res
  return { type, desc, scopes: scopes?.split(",") ?? [], pr: +pr, breaking: !!breaking }
}

const renderScopes = (xs: string[]) => xs.length > 0 ? `(${xs.join(", ")})` : ""

const prLink = (pr: number) =>
  `[#${pr}](https://github.com/cataclysmbnteam/Cataclysm-BN/pull/${pr})`

export const renderCommit = (
  { subject, author, date }: Record<"subject" | "author" | "date", string>,
) => {
  const { type, desc, scopes, pr, breaking } = parseCommit(subject)

  const header = `${type}${renderScopes(scopes)}${breaking ? "!" : ""}`
  const title = `- PR: ${prLink(pr)} **${header}: ${desc}** by ${author}`

  return ({ type, title, date: new Date(date), breaking })
}

// deno-fmt-ignore
const commitTypes = ["breaking changes", "feat", "fix", "perf", "refactor", "docs", "test", "build", "ci", "style", "chore"]
const typeSortOrder = Object.fromEntries(commitTypes.map((x, i) => [x, i]))

if (import.meta.main) {
  const xs = changelog
    .filter((x) => !x.subject.includes("routine i18n updates on"))
    .map(renderCommit)
    .sort((a, b) => a.date.getTime() - b.date.getTime())

  const byTypes = Object.groupBy(xs, ({ type, breaking }) => breaking ? "breaking changes" : type)

  const message = Object.entries(byTypes)
    .sort(([a], [b]) => typeSortOrder[a] - typeSortOrder[b])
    .map(([type, xs]) =>
      outdent`
        ## ${(type.length <= 2 ? upperCase : titleCase)(type)}

        ${xs.map(({ title }) => title).join("\n")}
      `
    )
    .join("\n\n")

  console.log(message)
  await Deno.writeTextFile("scripts/changelog.md", message)
}
