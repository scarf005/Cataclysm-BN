import { assert, assertEquals, assertStringIncludes } from "@std/assert"
import { deadline } from "@std/async"
import { join } from "@std/path"
import { replaySeed, runProcess } from "./run-linux-test-shards.ts"

type ProcessEvent = {
  stream: "stdout" | "stderr"
  text: string
}

Deno.test("runProcess streams fixture output before exit and preserves status and stderr", async () => {
  const dir = await Deno.makeTempDir({ prefix: "cata-shard-process." })
  const releasePath = join(dir, "release")
  const logPath = join(dir, "shard.log")
  const events: ProcessEvent[] = []
  const outputSeen = Promise.withResolvers<void>()
  let completed = false
  const source = [
    "console.log('stdout-before-exit')",
    "console.error('stderr-before-exit')",
    "while (true) { try { await Deno.stat(Deno.args[0]); break } catch { await new Promise((resolve) => setTimeout(resolve, 5)) } }",
    "Deno.exit(7)",
  ].join("; ")
  const runPromise = runProcess({
    command: Deno.execPath(),
    args: ["eval", "--allow-read", source, releasePath],
    streamOutput: false,
    logPath,
    onOutput: (event) => {
      events.push(event)
      if (event.text.includes("stdout-before-exit")) {
        outputSeen.resolve()
      }
    },
  }).finally(() => {
    completed = true
  })
  try {
    await deadline(outputSeen.promise, 2000)
    assert(
      events.some((event) =>
        event.stream === "stdout" && event.text.includes("stdout-before-exit")
      ),
    )
    assertEquals(completed, false)
    await Deno.writeTextFile(releasePath, "release")
    const result = await runPromise
    assertEquals(result.code, 7)
    assertStringIncludes(result.stderr, "stderr-before-exit")
    const log = await Deno.readTextFile(logPath)
    assertStringIncludes(log, "stdout-before-exit")
    assertStringIncludes(log, "stderr-before-exit")
  } finally {
    await Deno.writeTextFile(releasePath, "release")
    await runPromise
    await Deno.remove(dir, { recursive: true })
  }
})

Deno.test("replaySeed only replaces a time seed", () => {
  assertEquals(replaySeed(["--rng-seed", "time", "--use-colour", "no"], "42"), [
    "--rng-seed",
    "42",
    "--use-colour",
    "no",
  ])
  assertEquals(replaySeed(["--rng-seed=time"], "42"), ["--rng-seed=42"])
})
