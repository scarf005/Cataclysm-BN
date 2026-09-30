import { assert, assertEquals, assertRejects, assertStringIncludes } from "@std/assert"
import { deadline, delay } from "@std/async"
import { fromFileUrl, join } from "@std/path"
import {
  buildShardPlan,
  discoverTags,
  extractFileTags,
  normalizeRngSeed,
  runProcess,
} from "./run-linux-test-shards.ts"

type ProcessOptions = Parameters<typeof runProcess>[0]
type ProcessEvent = { stream: "stdout" | "stderr"; text: string }

const runEval = (source: string, options: Partial<ProcessOptions> = {}, args: string[] = []) =>
  runProcess({
    command: Deno.execPath(),
    args: ["eval", source, ...args],
    label: "eval-fixture",
    streamOutput: false,
    timeoutSeconds: 2,
    ...options,
  })

const withTempDir = async (test: (dir: string) => Promise<void>): Promise<void> => {
  const dir = await Deno.makeTempDir({ prefix: "cata-test-fixture." })
  try {
    await test(dir)
  } finally {
    await Deno.remove(dir, { recursive: true })
  }
}

Deno.test("runProcess streams output before the process exits", () =>
  withTempDir(async (dir) => {
    const events: ProcessEvent[] = []
    const outputSeen = Promise.withResolvers<void>()
    const releasePath = join(dir, "release")
    let completed = false
    const runPromise = runEval(
      "console.log('before-exit'); while (true) { try { await Deno.stat(Deno.args[0]); break; } catch { await new Promise(r => setTimeout(r, 10)); } }",
      {
        onOutput: (event) => {
          events.push(event)
          outputSeen.resolve()
        },
      },
      [releasePath],
    ).finally(() => {
      completed = true
    })
    try {
      await deadline(outputSeen.promise, 2000)
      assert(events.some((event) => event.text.includes("before-exit")))
      assertEquals(completed, false)
    } finally {
      await Deno.writeTextFile(releasePath, "release")
      assertEquals((await runPromise).code, 0)
    }
  }))

Deno.test("normalizeRngSeed resolves time and preserves numeric seeds", () => {
  assertEquals(normalizeRngSeed(["--rng-seed", "time"], 1_700_000_123_000), [
    "--rng-seed",
    "1700000123",
  ])
  assertEquals(normalizeRngSeed(["--rng-seed=42"]), ["--rng-seed=42"])
})

Deno.test("runProcess returns nonzero status and times out with cleanup", async () => {
  assertEquals((await runEval("Deno.exit(7)")).code, 7)
  const timedOut = await runEval("await new Promise((resolve) => setTimeout(resolve, 60_000));", {
    timeoutSeconds: 0.1,
    debuggerCommand: "cata-debugger-that-does-not-exist",
  })
  assert(timedOut.timedOut)
  assert(timedOut.code !== 0)
})

Deno.test("runProcess terminates descendants that keep pipes open after root exit", async () => {
  if (Deno.build.os !== "linux") return
  const started = Date.now()
  const result = await runEval(
    "new Deno.Command(Deno.execPath(), { args: ['eval', 'await new Promise((resolve) => setTimeout(resolve, 60000))'], stdout: 'inherit', stderr: 'inherit' }).spawn(); Deno.exit(0);",
  )
  assertEquals(result.code, 0)
  assert(Date.now() - started < 3000)
})

Deno.test({
  name: "runProcess force-kills a stubborn root on timeout",
  ignore: Deno.build.os === "windows",
  fn: async () => {
    const started = Date.now()
    const result = await runEval(
      "Deno.addSignalListener('SIGTERM', () => {}); await new Promise(() => {});",
      { timeoutSeconds: 0.1, debuggerCommand: "cata-debugger-that-does-not-exist" },
    )
    assert(result.timedOut)
    assert(result.code !== 0)
    assert(Date.now() - started < 3000)
  },
})

Deno.test("runProcess force-kills stubborn descendants on timeout", async () => {
  if (Deno.build.os !== "linux") return
  const events: ProcessEvent[] = []
  const started = Date.now()
  const descendant =
    "Deno.addSignalListener('SIGTERM', () => {}); console.log('descendant-ready'); await new Promise(() => {});"
  const result = await runEval(
    `new Deno.Command(Deno.execPath(), { args: ['eval', ${
      JSON.stringify(descendant)
    }], stdout: 'inherit', stderr: 'inherit' }).spawn(); await new Promise(() => {});`,
    {
      timeoutSeconds: 1,
      debuggerCommand: "cata-debugger-that-does-not-exist",
      onOutput: (event) => {
        events.push(event)
      },
    },
  )
  assert(events.some((event) => event.text.includes("descendant-ready")))
  assert(result.timedOut)
  assert(Date.now() - started < 5000)
})

Deno.test("runProcess cleans a failed spawn and its log", () =>
  withTempDir(async (dir) => {
    const logPath = join(dir, "spawn-failure.log")
    await assertRejects(() => runEval("", { command: `${Deno.execPath()}.missing`, logPath }))
    await Deno.remove(logPath)
  }))

Deno.test("runProcess cleans up after an output callback failure", async () => {
  await assertRejects(() =>
    runEval(
      "console.error('callback-failure'); await new Promise((resolve) => setTimeout(resolve, 60000));",
      {
        onOutput: () => {
          throw new Error("callback failed")
        },
      },
    )
  )
})

Deno.test("runProcess bounds a no-newline output event", async () => {
  const events: ProcessEvent[] = []
  await runEval("await Deno.stdout.write(new TextEncoder().encode('x'.repeat(200000)))", {
    onOutput: (event) => {
      events.push(event)
    },
  })
  assert(events.length > 1)
  assert(events.every((event) => event.text.length <= 64 * 1024 + 1))
})

Deno.test("runProcess bounds a stalled debugger and cleans its output", async () => {
  if (Deno.build.os !== "linux") return
  await withTempDir(async (dir) => {
    const debuggerPath = join(dir, "stalled-debugger.sh")
    const logPath = join(dir, "stalled-debugger.log")
    await Deno.writeTextFile(debuggerPath, "#!/bin/sh\nexec sleep 60\n")
    await Deno.chmod(debuggerPath, 0o755)
    const result = await runEval("await new Promise((resolve) => setTimeout(resolve, 60000))", {
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
  })
})

Deno.test("runProcess reports a missing debugger without leaking the timeout", () =>
  withTempDir(async (dir) => {
    const logPath = join(dir, "missing-debugger.log")
    const result = await runEval("await new Promise((resolve) => setTimeout(resolve, 60_000));", {
      timeoutSeconds: 0.1,
      logPath,
      debuggerCommand: "cata-debugger-that-does-not-exist",
    })
    assert(result.timedOut)
    assertStringIncludes(await Deno.readTextFile(logPath), "stack capture unavailable")
  }))

Deno.test("discoverTags fails promptly when discovery exits nonzero", async () => {
  if (Deno.build.os === "windows") return
  await withTempDir(async (dir) => {
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
        dir,
        dir,
      )
    )
  })
})

Deno.test("runProcess inherits environment and applies overrides", async () => {
  const events: ProcessEvent[] = []
  await runEval(
    "console.log(Deno.env.get('PATH')); console.log(Deno.env.get('CATA_TEST_FIXTURE'));",
    {
      env: { CATA_TEST_FIXTURE: "override" },
      onOutput: (event) => {
        events.push(event)
      },
    },
  )
  assertEquals(events.map((event) => event.text.trimEnd()), [Deno.env.get("PATH"), "override"])
})

Deno.test("invalid seed does not create shard directories", () =>
  withTempDir(async (dir) => {
    const result = await runProcess({
      command: Deno.execPath(),
      args: [
        "run",
        "--allow-read",
        "--allow-write",
        "--allow-run",
        "--allow-env",
        fromFileUrl(new URL("./run-linux-test-shards.ts", import.meta.url)),
        "--mode",
        "legacy",
        "--jobs",
        "1",
        "--dry-run",
        "--",
        "--rng-seed",
        "bogus",
      ],
      env: { RUNNER_TEMP: dir, CATA_TEST_SHARD_DIR: "" },
      label: "invalid-seed",
      streamOutput: false,
      timeoutSeconds: 10,
    })
    assertEquals(result.code, 1)
    assertEquals([...Deno.readDirSync(dir)], [])
  }))

Deno.test("runProcess cleans debugger descendants after debugger exit", async () => {
  if (Deno.build.os !== "linux") return
  await withTempDir(async (dir) => {
    const debuggerPath = join(dir, "exited-debugger.sh")
    const logPath = join(dir, "exited-debugger.log")
    const pidPath = join(dir, "descendant.pid")
    await Deno.writeTextFile(
      debuggerPath,
      `#!/bin/sh\nsleep 60 &\necho $! > '${pidPath}'\nexit 0\n`,
    )
    await Deno.chmod(debuggerPath, 0o755)
    let pid: number | undefined
    try {
      const result = await runEval("await new Promise(r => setTimeout(r, 60000))", {
        timeoutSeconds: 0.1,
        debuggerCommand: debuggerPath,
        logPath,
      })
      pid = Number(await Deno.readTextFile(pidPath))
      assert(result.timedOut)
      assertStringIncludes(await Deno.readTextFile(logPath), "[debugger output truncated]")
      for (let attempt = 0; attempt < 100; attempt++) {
        const status = await new Deno.Command("ps", {
          args: ["-o", "stat=", "-p", String(pid)],
          stdout: "piped",
          stderr: "null",
        }).output()
        if (!status.success || new TextDecoder().decode(status.stdout).trim().startsWith("Z")) {
          return
        }
        await delay(20)
      }
      throw new Error("debugger descendant survived cleanup")
    } finally {
      if (pid !== undefined) {
        try {
          Deno.kill(pid, "SIGKILL")
        } catch { /* Already terminated. */ }
      }
    }
  })
})

Deno.test("runProcess bounds large debugger output", async () => {
  if (Deno.build.os !== "linux") return
  await withTempDir(async (dir) => {
    const debuggerPath = join(dir, "large-debugger.sh")
    const logPath = join(dir, "large-debugger.log")
    const source = 'await Deno.stdout.write(new TextEncoder().encode("x".repeat(300000)));'
    await Deno.writeTextFile(
      debuggerPath,
      `#!/bin/sh\nexec "${Deno.execPath()}" eval '${source}'\n`,
    )
    await Deno.chmod(debuggerPath, 0o755)
    const result = await runEval("await new Promise(r => setTimeout(r, 60000))", {
      timeoutSeconds: 0.1,
      debuggerCommand: debuggerPath,
      logPath,
    })
    assert(result.timedOut)
    const log = await Deno.readTextFile(logPath)
    assertStringIncludes(log, "[debugger output truncated]")
    assert(log.length < 270000)
  })
})

Deno.test("discoverTags accepts Catch2 tag-count exit status", async () => {
  if (Deno.build.os === "windows") return
  await withTempDir(async (dir) => {
    const testBin = join(dir, "catch-list-tags.sh")
    await Deno.writeTextFile(
      testBin,
      "#!/bin/sh\nprintf '  1 [#fixture_test]\\n1 tag\\n\\n'\nexit 1\n",
    )
    await Deno.chmod(testBin, 0o755)
    const result = await discoverTags(
      {
        mode: "file-tags",
        jobs: 1,
        slowShards: 1,
        nonSlowShards: 1,
        dryRun: false,
        diagnostics: false,
        shardTimeoutSeconds: 2,
        discoveryTimeoutSeconds: 1,
        testBin,
        testOpts: [],
      },
      dir,
      dir,
    )
    assertEquals(result.allTags, ["[#fixture_test]"])
    assertEquals(result.slowTags, ["[#fixture_test]"])
  })
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
