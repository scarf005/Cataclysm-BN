import { assert, assertEquals, assertRejects, assertStringIncludes } from "@std/assert"
import { deadline, delay } from "@std/async"
import { join } from "@std/path"
import {
  buildShardPlan,
  discoverTags,
  extractFileTags,
  normalizeRngSeed,
  runProcess,
} from "./run-linux-test-shards.ts"

type ProcessEvent = {
  stream: "stdout" | "stderr"
  text: string
}

Deno.test("runProcess streams output before the process exits", async () => {
  const events: ProcessEvent[] = []
  let resolveOutput: () => void = () => {}
  const outputSeen = new Promise<void>((resolve) => {
    resolveOutput = resolve
  })
  let completed = false
  const runPromise = runProcess({
    command: Deno.execPath(),
    args: [
      "eval",
      "console.log('before-exit'); await new Promise((resolve) => setTimeout(resolve, 250));",
    ],
    label: "stream-fixture",
    streamOutput: false,
    timeoutSeconds: 2,
    onOutput: (event) => {
      events.push(event)
      resolveOutput()
    },
  }).finally(() => {
    completed = true
  })
  await deadline(outputSeen, 2000)
  assert(events.some((event) => event.text.includes("before-exit")))
  await delay(50)
  assertEquals(completed, false)
  assertEquals((await runPromise).code, 0)
})

Deno.test("normalizeRngSeed resolves time and preserves numeric seeds", () => {
  assertEquals(normalizeRngSeed(["--rng-seed", "time"], 1_700_000_123_000), [
    "--rng-seed",
    "1700000123",
  ])
  assertEquals(normalizeRngSeed(["--rng-seed=42"]), ["--rng-seed=42"])
})

Deno.test("runProcess returns nonzero status and times out with cleanup", async () => {
  const failed = await runProcess({
    command: Deno.execPath(),
    args: ["eval", "Deno.exit(7)"],
    label: "failure-fixture",
    streamOutput: false,
    timeoutSeconds: 2,
  })
  assertEquals(failed.code, 7)
  const timedOut = await runProcess({
    command: Deno.execPath(),
    args: ["eval", "await new Promise((resolve) => setTimeout(resolve, 60_000));"],
    label: "timeout-fixture",
    streamOutput: false,
    timeoutSeconds: 0.1,
    debuggerCommand: "cata-debugger-that-does-not-exist",
  })
  assert(timedOut.timedOut)
  assert(timedOut.code !== 0)
})

Deno.test("runProcess terminates descendants that keep pipes open after root exit", async () => {
  if (Deno.build.os !== "linux") {
    return
  }
  const started = Date.now()
  const result = await runProcess({
    command: Deno.execPath(),
    args: [
      "eval",
      "new Deno.Command(Deno.execPath(), { args: ['eval', 'await new Promise((resolve) => setTimeout(resolve, 60000))'], stdout: 'inherit', stderr: 'inherit' }).spawn(); Deno.exit(0);",
    ],
    label: "descendant-fixture",
    streamOutput: false,
    timeoutSeconds: 2,
  })
  assertEquals(result.code, 0)
  assert(Date.now() - started < 3000)
})

Deno.test("runProcess force-kills a stubborn root on timeout", async () => {
  const started = Date.now()
  const result = await runProcess({
    command: Deno.execPath(),
    args: [
      "eval",
      "Deno.addSignalListener('SIGTERM', () => {}); await new Promise(() => {});",
    ],
    label: "stubborn-fixture",
    streamOutput: false,
    timeoutSeconds: 0.1,
    debuggerCommand: "cata-debugger-that-does-not-exist",
  })
  assert(result.timedOut)
  assert(result.code !== 0)
  assert(Date.now() - started < 3000)
})

Deno.test("runProcess cleans a failed spawn and its log", async () => {
  const logDir = await Deno.makeTempDir({ prefix: "cata-test-spawn." })
  const logPath = join(logDir, "spawn-failure.log")
  try {
    await assertRejects(() =>
      runProcess({
        command: `${Deno.execPath()}.missing`,
        args: [],
        label: "spawn-failure",
        streamOutput: false,
        logPath,
      })
    )
    await Deno.remove(logPath)
  } finally {
    await Deno.remove(logDir, { recursive: true })
  }
})

Deno.test("runProcess cleans up after an output callback failure", async () => {
  await assertRejects(() =>
    runProcess({
      command: Deno.execPath(),
      args: [
        "eval",
        "console.error('callback-failure'); await new Promise((resolve) => setTimeout(resolve, 60000));",
      ],
      label: "callback-failure",
      streamOutput: false,
      timeoutSeconds: 2,
      onOutput: () => {
        throw new Error("callback failed")
      },
    })
  )
})

Deno.test("runProcess bounds a no-newline output event", async () => {
  const events: ProcessEvent[] = []
  await runProcess({
    command: Deno.execPath(),
    args: ["eval", "await Deno.stdout.write(new TextEncoder().encode('x'.repeat(200000)))"],
    label: "long-line-fixture",
    streamOutput: false,
    timeoutSeconds: 2,
    onOutput: (event) => {
      events.push(event)
    },
  })
  assert(events.length > 1)
  assert(events.every((event) => event.text.length <= 64 * 1024 + 1))
})

Deno.test("runProcess bounds a stalled debugger and cleans its output", async () => {
  if (Deno.build.os !== "linux") {
    return
  }
  const logDir = await Deno.makeTempDir({ prefix: "cata-test-debugger." })
  const debuggerPath = join(logDir, "stalled-debugger.sh")
  const logPath = join(logDir, "stalled-debugger.log")
  await Deno.writeTextFile(debuggerPath, "#!/bin/sh\nexec sleep 60\n")
  await Deno.chmod(debuggerPath, 0o755)
  try {
    const result = await runProcess({
      command: Deno.execPath(),
      args: ["eval", "await new Promise((resolve) => setTimeout(resolve, 60000))"],
      label: "stalled-debugger",
      streamOutput: false,
      timeoutSeconds: 0.1,
      logPath,
      initialLogText: "filter=startup-fixture seed=42 repro=deno-eval\n",
      debuggerCommand: debuggerPath,
      debuggerTimeoutSeconds: 0.1,
    })
    assert(result.timedOut)
    const log = await Deno.readTextFile(logPath)
    assertStringIncludes(log, "filter=startup-fixture seed=42 repro=deno-eval")
    assertStringIncludes(log, "stack capture timed out")
  } finally {
    await Deno.remove(logDir, { recursive: true })
  }
})

Deno.test("runProcess reports a missing debugger without leaking the timeout", async () => {
  const logDir = await Deno.makeTempDir({ prefix: "cata-test-diagnostics." })
  const logPath = join(logDir, "missing-debugger.log")
  try {
    const result = await runProcess({
      command: Deno.execPath(),
      args: ["eval", "await new Promise((resolve) => setTimeout(resolve, 60_000));"],
      label: "missing-debugger",
      streamOutput: false,
      timeoutSeconds: 0.1,
      logPath,
      debuggerCommand: "cata-debugger-that-does-not-exist",
    })
    assert(result.timedOut)
    assertStringIncludes(await Deno.readTextFile(logPath), "stack capture unavailable")
  } finally {
    await Deno.remove(logDir, { recursive: true })
  }
})

Deno.test("discoverTags fails promptly when discovery exits nonzero", async () => {
  if (Deno.build.os === "windows") {
    return
  }
  const shardDir = await Deno.makeTempDir({ prefix: "cata-test-discovery." })
  try {
    await assertRejects(() =>
      discoverTags(
        {
          mode: "file-tags",
          jobs: 1,
          slowShards: 1,
          nonSlowShards: 1,
          dryRun: false,
          diagnostics: false,
          shardTimeoutSeconds: 2,
          discoveryTimeoutSeconds: 1,
          testBin: "/bin/false",
          testOpts: [],
        },
        shardDir,
        shardDir,
      )
    )
  } finally {
    await Deno.remove(shardDir, { recursive: true })
  }
})

Deno.test("extractFileTags returns sorted unique file tags", () => {
  assertEquals(extractFileTags("x [#b_test] [foo] [#a_test] [#b_test]"), ["[#a_test]", "[#b_test]"])
})

Deno.test("buildShardPlan keeps visibility out of CPU shards", () => {
  const shards = buildShardPlan(["[#vision_test]", "[#map_test]"], [], 2, 1)
  const cpuFilters = shards.filter((shard) => shard.name.endsWith("cpu")).flatMap((
    shard,
  ) => shard.filters)
  assertEquals(cpuFilters.some((filter) => filter.includes("vision_test")), false)
  assertEquals(cpuFilters.some((filter) => filter.includes("map_test")), true)
})

Deno.test("buildShardPlan keeps visibility on its GPU shard", () => {
  const shards = buildShardPlan(["[#enchantment_test]", "[#tiny_test]"], [], 2, 1)
  const visibility = shards.find((shard) => shard.name === "20-visibility")
  assertEquals(visibility?.compute, "gpu_software")
})
