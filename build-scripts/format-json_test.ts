import { assertEquals, assertStringIncludes } from "@std/assert"
import { fromFileUrl, join } from "@std/path"

const script = fromFileUrl(new URL("./format-json.sh", import.meta.url))

Deno.test("format-json preserves exit handling with prebuilt and built CLIs", async () => {
  const root = await Deno.makeTempDir({ prefix: "json-format-" })
  try {
    const formatter = join(root, "formatter with spaces")
    const log = join(root, "calls")
    await Deno.writeTextFile(
      formatter,
      `#!/bin/bash
printf '%s\\n' "$1" >> '${log}'
case "$1" in
    */changed.json) exit 1 ;;
    */invalid.json) exit 7 ;;
    *) exit 0 ;;
esac
`,
    )
    await Deno.chmod(formatter, 0o755)
    await Deno.writeTextFile(
      join(root, "cmake"),
      `#!/bin/bash
printf 'cmake\\n' >> '${log}'
if [[ "$1" == --build ]]; then
    mkdir -p "$2/tools/format"
    cp '${formatter}' "$2/tools/format/json_formatter"
fi
`,
    )
    await Deno.chmod(join(root, "cmake"), 0o755)
    const files = ["changed.json", "invalid.json", "unchanged.json"].map((name) => join(root, name))
    for (const file of files) await Deno.writeTextFile(file, "[]\n")
    const env = {
      PATH: `${root}:${Deno.env.get("PATH")}`,
      CATA_JSON_FORMAT_BUILD_DIR: join(root, "build"),
    }
    const run = (args: string[], cli?: string) =>
      new Deno.Command("bash", {
        args: [script, ...args],
        clearEnv: true,
        env: { ...env, ...(cli ? { CATA_JSON_FORMATTER: cli } : {}) },
      }).output()
    for (const prebuilt of [true, false]) {
      await Deno.writeTextFile(log, "")
      const cli = prebuilt ? formatter : undefined
      assertEquals((await run([files[0], files[2]], cli)).code, 0)
      assertEquals((await run(files, cli)).code, 7)
      const calls = (await Deno.readTextFile(log)).trim().split("\n")
      assertEquals(calls.filter((line) => line === "cmake").length, prebuilt ? 0 : 4)
      assertEquals(calls.filter((line) => line !== "cmake"), [files[0], files[2], ...files])
    }
    const calls = await Deno.readTextFile(log)

    const missing = await run([files[0]], join(root, "missing"))
    assertEquals(missing.code, 127)
    assertStringIncludes(new TextDecoder().decode(missing.stderr), "No such file or directory")
    assertEquals(await Deno.readTextFile(log), calls)
  } finally {
    await Deno.remove(root, { recursive: true })
  }
})
