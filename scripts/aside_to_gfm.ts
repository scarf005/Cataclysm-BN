#!/usr/bin/env -S deno run -RW

/**
 * Converts starlight-style [asides](https://starlight.astro.build/guides/authoring-content/#asides) to
 * [Github-flavored markdown alerts](https://docs.github.com/en/get-started/writing-on-github/getting-started-with-writing-and-formatting-on-github/basic-writing-and-formatting-syntax#alerts).
 */
export const asideToGfm = (aside: string): string =>
  aside.replace(
    regex,
    (_match, type, content: string) =>
      `> [!${type.toUpperCase()}]\n${
        content.trim().split("\n")
          .map((line) => `> ${line}`)
          .map((line) => line.trim())
          .join("\n")
      }`,
  )

const regex = /:::(\w+)\n+((?:.|\n)+?)\n+:::/g

if (import.meta.main) {
  const { Command } = await import("@cliffy/command")
  const { paragraph } = await import("./changelog/paragraph.ts")
  const { walk } = await import("@std/fs")

  const { args: [path] } = await new Command()
    .description(paragraph`
        Converts starlight-style asides (:::note) to github-flavored markdown alerts ([!NOTE]).
    `)
    .arguments("<path>")
    .parse(Deno.args)

  const files = await Array.fromAsync(walk(path, { exts: [".md"], includeDirs: false }))
  await Promise.allSettled(
    files.map(async ({ path }) => {
      const text = await Deno.readTextFile(path)
      await Deno.writeTextFile(path, asideToGfm(text))
    }),
  )
}
