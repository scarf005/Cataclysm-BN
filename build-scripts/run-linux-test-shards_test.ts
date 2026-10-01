import { assert, assertEquals, assertRejects, assertStringIncludes } from "@std/assert"
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

Deno.test("runProcess does not spawn a child when opening its log fails", async () => {
  const dir = await Deno.makeTempDir({ prefix: "cata-shard-log-open." })
  const markerPath = join(dir, "spawned")
  const source = [
    "await Deno.writeTextFile(Deno.args[0], 'spawned')",
    "while (true) {",
    "  try { await Deno.stat(Deno.args[0]) }",
    "  catch { break }",
    "  await new Promise((resolve) => setTimeout(resolve, 5))",
    "}",
  ].join("; ")
  try {
    await assertRejects(() =>
      runProcess({
        command: Deno.execPath(),
        args: ["eval", "--allow-read", "--allow-write", source, markerPath],
        streamOutput: false,
        logPath: join(dir, "missing", "shard.log"),
      })
    )
    await new Promise((resolve) => setTimeout(resolve, 50))
    await assertRejects(() => Deno.stat(markerPath))
  } finally {
    await Deno.remove(markerPath).catch(() => {})
    await Deno.remove(dir, { recursive: true })
  }
})

Deno.test("runProcess waits for a child after an output callback fails", async () => {
  const dir = await Deno.makeTempDir({ prefix: "cata-shard-callback." })
  const startedPath = join(dir, "started")
  const releasePath = join(dir, "release")
  const finishedPath = join(dir, "finished")
  const source = [
    "const [startedPath, releasePath, finishedPath] = Deno.args",
    "await Deno.writeTextFile(startedPath, 'started')",
    "Deno.addSignalListener('SIGTERM', async () => {",
    "  while (true) {",
    "    try { await Deno.stat(releasePath); break }",
    "    catch { await new Promise((resolve) => setTimeout(resolve, 5)) }",
    "  }",
    "  await new Promise((resolve) => setTimeout(resolve, 100))",
    "  await Deno.writeTextFile(finishedPath, 'finished')",
    "  Deno.exit(0)",
    "})",
    "console.log('callback-trigger')",
    "await new Promise(() => {})",
  ].join("\n")
  try {
    await assertRejects(
      () =>
        runProcess({
          command: Deno.execPath(),
          args: [
            "eval",
            "--allow-read",
            "--allow-write",
            source,
            startedPath,
            releasePath,
            finishedPath,
          ],
          streamOutput: false,
          onOutput: async (event) => {
            if (event.text.includes("callback-trigger")) {
              await Deno.writeTextFile(releasePath, "release")
              throw new Error("output callback failure")
            }
          },
        }),
      Error,
      "output callback failure",
    )
    assertEquals(await Deno.readTextFile(finishedPath), "finished")
  } finally {
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
