#!/usr/bin/env -S deno run --allow-read --allow-write --allow-run --allow-env
/**
 * @module
 *
 * Runs Cataclysm: Bright Nights tests as filename-tag shards.
 *
 * The runner discovers Catch2 filename tags, builds deterministic shards, starts the heaviest shards
 * first, and defaults local runs to the detected CPU count while CI can pass explicit limits.
 */

import { Command } from "@cliffy/command"
import { pooledMap } from "@std/async"
import { emptyDir, ensureDir } from "@std/fs"
import { basename, join } from "@std/path"

type Mode = "auto" | "file-tags" | "tiles" | "legacy"

type Options = {
  mode: Mode
  jobs: number | "auto"
  slowShards: number
  nonSlowShards: number | "auto"
  dryRun: boolean
  diagnostics: boolean
  shardTimeoutSeconds: number
  discoveryTimeoutSeconds: number
  testBin: string
  testOpts: string[]
}

type Shard = {
  name: string
  filters: string[]
  estimatedSeconds: number
  compute: "cpu" | "gpu_software"
}

type CommandResult = {
  code: number
  stdout: string
  stderr: string
}

type ProcessOutput = "stdout" | "stderr"

type ProcessEvent = {
  stream: ProcessOutput
  text: string
}

type ProcessOptions = {
  command: string
  args: string[]
  env?: Record<string, string>
  timeoutSeconds?: number
  label: string
  logPath?: string
  initialLogText?: string
  streamOutput: boolean
  onOutput?: (event: ProcessEvent) => void | Promise<void>
  debuggerCommand?: string
  debuggerTimeoutSeconds?: number
}

type ProcessResult = {
  code: number
  timedOut: boolean
}

const defaultTestBin = "./out/build/linux-full/tests/cata_test-tiles"
const defaultTestOpts = [
  "--min-duration",
  "20",
  "--use-colour",
  "no",
  "--rng-seed",
  "1",
  "--error-format=github-action",
  "--gpu-backend=software",
]

const fixedShards: Shard[] = [
  {
    name: "00-vehicle-rails-basic",
    filters: ["vehicle_rail_movement_basic*"],
    estimatedSeconds: 215,
    compute: "cpu",
  },
  {
    name: "09-vehicle-rails-fork",
    filters: ["vehicle_rail_movement_fork"],
    estimatedSeconds: 180,
    compute: "cpu",
  },
  {
    name: "19-vehicle-rails-shifting",
    filters: ["vehicle_rail_movement_shifting*"],
    estimatedSeconds: 200,
    compute: "cpu",
  },
  {
    name: "20-visibility",
    filters: [
      "[#vision_test] ~[.]",
      "[#shadowcasting_test] ~[.]",
      "[#zlevel_visibility_cache_test] ~[.]",
    ],
    estimatedSeconds: 250,
    compute: "gpu_software",
  },
  {
    name: "21-vehicle-efficiency",
    filters: ["[#vehicle_efficiency_test] ~[.]"],
    estimatedSeconds: 260,
    compute: "cpu",
  },
  {
    name: "22-starting-items",
    filters: ["starting_items"],
    estimatedSeconds: 220,
    compute: "cpu",
  },
  {
    name: "29-vehicle-rails-other",
    filters: ["vehicle_rail_movement_derailed,vehicle_rail_movement_ramp"],
    estimatedSeconds: 120,
    compute: "cpu",
  },
]

const tagWeights = new Map<string, number>([
  ["[#enchantment_test]", 350],
  ["[#vehicle_test]", 150],
  ["[#vehicle_ramp_test]", 100],
  ["[#map_test]", 100],
  ["[#crafting_test]", 120],
  ["[#weather_test]", 80],
  ["[#json_test]", 70],
  ["[#overmap_test]", 55],
  ["[#generic_factory_test]", 35],
  ["[#catalua_test]", 30],
  ["[#vehicle_drag_test]", 18],
])

const specialTagPattern =
  /^\[#(vehicle_(efficiency|rails)|vision|shadowcasting|zlevel_visibility_cache)_test\]$/

const parsePositiveInt = (name: string, value: string): number => {
  if (!/^[1-9][0-9]*$/.test(value)) {
    throw new Error(`${name} must be a positive integer: ${value}`)
  }
  return Number(value)
}

const commandOutput = async (
  command: string,
  args: string[],
  env: Record<string, string> = {},
): Promise<CommandResult> => {
  const result = await new Deno.Command(command, {
    args,
    env: { ...Deno.env.toObject(), ...env },
    stdout: "piped",
    stderr: "piped",
  }).output()
  const decoder = new TextDecoder()
  return {
    code: result.code,
    stdout: decoder.decode(result.stdout),
    stderr: decoder.decode(result.stderr),
  }
}

const MAX_PENDING_LINE_LENGTH = 64 * 1024
const MAX_DEBUGGER_OUTPUT = 256 * 1024

type TimeoutRace = {
  promise: Promise<void>
  cancel: () => void
}

type SpawnedProcess = {
  process: Deno.ChildProcess
  processGroup: boolean
}

type CapturedOutput = {
  text: string
  truncated: boolean
}

const createTimeout = (milliseconds: number): TimeoutRace => {
  let timer: ReturnType<typeof setTimeout> | undefined
  const promise = new Promise<void>((resolve) => {
    timer = setTimeout(resolve, milliseconds)
  })
  return {
    promise,
    cancel: () => {
      if (timer !== undefined) {
        clearTimeout(timer)
        timer = undefined
      }
    },
  }
}

const raceWithTimeout = async <T>(
  promise: Promise<T>,
  milliseconds: number,
): Promise<T | undefined> => {
  const timeout = createTimeout(milliseconds)
  try {
    return await Promise.race([promise, timeout.promise.then(() => undefined)])
  } finally {
    timeout.cancel()
  }
}

type WritableFile = Pick<Deno.FsFile, "write">

const writeAll = async (file: WritableFile, data: Uint8Array): Promise<void> => {
  let offset = 0
  while (offset < data.byteLength) {
    const written = await file.write(data.subarray(offset))
    if (written === 0) {
      throw new Error("short write")
    }
    offset += written
  }
}

const textEncoder = new TextEncoder()
const writeText = async (file: WritableFile, text: string): Promise<void> => {
  await writeAll(file, textEncoder.encode(text))
}

const safeFileName = (name: string): string => name.replaceAll(/[^A-Za-z0-9_.-]/g, "_")

const commandOutputBounded = async (
  command: string,
  args: string[],
  timeoutMilliseconds: number,
): Promise<CommandResult | undefined> => {
  const child = new Deno.Command(command, {
    args,
    stdout: "piped",
    stderr: "piped",
  }).spawn()
  const outputPromise = child.output()
  const output = await raceWithTimeout(outputPromise, timeoutMilliseconds)
  if (output !== undefined) {
    return {
      code: output.code,
      stdout: new TextDecoder().decode(output.stdout),
      stderr: new TextDecoder().decode(output.stderr),
    }
  }
  try {
    child.kill("SIGKILL")
  } catch {
    // The helper may have exited while the timeout was observed.
  }
  await raceWithTimeout(outputPromise, 1000)
  return undefined
}

const spawnProcess = (options: ProcessOptions): SpawnedProcess => {
  if (/[\\/]/.test(options.command)) {
    Deno.statSync(options.command)
  }
  const commandOptions = {
    args: options.args,
    env: options.env ? { ...Deno.env.toObject(), ...options.env } : undefined,
    stdout: "piped" as const,
    stderr: "piped" as const,
  }
  if (Deno.build.os !== "linux") {
    return {
      process: new Deno.Command(options.command, commandOptions).spawn(),
      processGroup: false,
    }
  }
  try {
    return {
      process: new Deno.Command("setsid", {
        ...commandOptions,
        args: ["--", options.command, ...options.args],
      }).spawn(),
      processGroup: true,
    }
  } catch {
    return {
      process: new Deno.Command(options.command, commandOptions).spawn(),
      processGroup: false,
    }
  }
}

const terminateProcess = async (
  process: Deno.ChildProcess,
  processGroup: boolean,
  force: boolean,
): Promise<void> => {
  if (Deno.build.os === "windows") {
    await commandOutputBounded("taskkill", ["/PID", String(process.pid), "/T", "/F"], 2000)
    try {
      process.kill("SIGKILL")
    } catch {
      // The process may have exited while taskkill was running.
    }
    return
  }

  if (processGroup) {
    await commandOutputBounded("kill", ["-TERM", `-${process.pid}`], 1000)
  } else {
    try {
      process.kill("SIGTERM")
    } catch {
      // The process may have exited while termination was requested.
    }
  }
  if (!force) {
    return
  }
  const gracefulStatus = await raceWithTimeout(process.status, 1000)
  if (gracefulStatus === undefined || processGroup) {
    if (processGroup) {
      await commandOutputBounded("kill", ["-KILL", `-${process.pid}`], 1000)
    }
    try {
      process.kill("SIGKILL")
    } catch {
      // The process may have exited while forced termination was requested.
    }
  }
}

const readLimited = async (
  stream: ReadableStream<Uint8Array>,
  activeReaders: Set<ReadableStreamDefaultReader<Uint8Array>>,
  limit: number,
): Promise<CapturedOutput> => {
  const reader = stream.getReader()
  activeReaders.add(reader)
  const decoder = new TextDecoder()
  let text = ""
  let truncated = false
  try {
    while (true) {
      const chunk = await reader.read()
      if (chunk.done) {
        break
      }
      if (text.length < limit) {
        text += decoder.decode(chunk.value, { stream: true })
        if (text.length > limit) {
          text = text.slice(0, limit)
          truncated = true
        }
      } else {
        truncated = true
      }
    }
    if (!truncated) {
      text += decoder.decode()
    }
  } finally {
    activeReaders.delete(reader)
    reader.releaseLock()
  }
  return { text, truncated }
}

const collectStack = async (
  process: Deno.ChildProcess,
  options: ProcessOptions,
  appendLog: (text: string) => Promise<void>,
): Promise<void> => {
  const report = async (text: string): Promise<void> => {
    const normalized = text.endsWith("\n") ? text : `${text}\n`
    const formatted = normalized.split("\n").slice(0, -1)
      .map((line) => `[${options.label}] stack: ${line}\n`).join("")
    await appendLog(formatted)
    if (options.streamOutput) {
      await writeAll(Deno.stdout, textEncoder.encode(formatted))
    }
  }
  if (Deno.build.os !== "linux") {
    await report(`stack capture unavailable on ${Deno.build.os}`)
    return
  }

  const debuggerCommand = options.debuggerCommand ?? "gdb"
  const activeReaders = new Set<ReadableStreamDefaultReader<Uint8Array>>()
  let debuggerProcess: SpawnedProcess | undefined
  let outputPromise: Promise<CapturedOutput[]> | undefined
  try {
    debuggerProcess = spawnProcess({
      command: debuggerCommand,
      args: ["--batch", "-ex", "thread apply all bt full", "-p", String(process.pid)],
      label: `${options.label}-debugger`,
      streamOutput: false,
    })
    outputPromise = Promise.all([
      readLimited(debuggerProcess.process.stdout, activeReaders, MAX_DEBUGGER_OUTPUT),
      readLimited(debuggerProcess.process.stderr, activeReaders, MAX_DEBUGGER_OUTPUT),
    ])
    const status = await raceWithTimeout(
      debuggerProcess.process.status,
      (options.debuggerTimeoutSeconds ?? 30) * 1000,
    )
    if (status === undefined) {
      await terminateProcess(debuggerProcess.process, debuggerProcess.processGroup, true)
      await Promise.all([...activeReaders].map((reader) => reader.cancel()))
      await raceWithTimeout(outputPromise, 1000)
      await report(`stack capture timed out after ${options.debuggerTimeoutSeconds ?? 30}s`)
      return
    }
    const output = await raceWithTimeout(outputPromise, 1000)
    const captured = output ?? [{ text: "", truncated: true }, { text: "", truncated: true }]
    const text = captured.map((part) => part.text).join("")
    const suffix = captured.some((part) => part.truncated) ? "\n[debugger output truncated]" : ""
    const result = status.success
      ? (text || "debugger returned no stack output")
      : `stack capture unavailable (gdb exit ${status.code}; ptrace may be denied)\n${text}`
    await report(`${result}${suffix}`)
  } catch (error) {
    if (debuggerProcess) {
      await terminateProcess(debuggerProcess.process, debuggerProcess.processGroup, true)
    }
    await Promise.all([...activeReaders].map((reader) => reader.cancel()))
    if (outputPromise) {
      await raceWithTimeout(outputPromise, 1000)
    }
    await report(`stack capture unavailable: ${error}`)
  }
}

export const runProcess = async (options: ProcessOptions): Promise<ProcessResult> => {
  let logFile: Deno.FsFile | undefined
  let spawned: SpawnedProcess | undefined
  let streamsDone: Promise<boolean> | undefined
  let streamsError: unknown
  const activeReaders = new Set<ReadableStreamDefaultReader<Uint8Array>>()
  let status: Deno.CommandStatus | undefined
  let timedOut = false
  let logWrite = Promise.resolve()
  const appendLog = async (text: string): Promise<void> => {
    if (!logFile) {
      return
    }
    const write = logWrite.then(() => writeText(logFile as Deno.FsFile, text))
    logWrite = write.catch(() => {})
    await write
  }
  const cancelStreams = async (): Promise<void> => {
    await Promise.all([...activeReaders].map((reader) => reader.cancel()))
  }
  const waitForStreams = async (): Promise<void> => {
    if (!streamsDone) {
      return
    }
    if (await raceWithTimeout(streamsDone, 1500) !== undefined) {
      return
    }
    if (spawned) {
      await terminateProcess(spawned.process, spawned.processGroup, true)
    }
    await cancelStreams()
    await raceWithTimeout(streamsDone, 500)
  }
  try {
    if (options.logPath) {
      logFile = await Deno.open(options.logPath, { create: true, truncate: true, write: true })
    }
    if (options.initialLogText) {
      await appendLog(options.initialLogText)
    }
    spawned = spawnProcess(options)
    const emit = async (event: ProcessEvent): Promise<void> => {
      const prefix = `[${options.label}] ${event.stream}: `
      const text = event.text.endsWith("\n") ? event.text : `${event.text}\n`
      const formatted = text.split("\n").slice(0, -1).map((line) => `${prefix}${line}\n`).join("")
      await appendLog(formatted)
      if (options.streamOutput) {
        const outputStream = event.stream === "stdout" ? Deno.stdout : Deno.stderr
        await writeAll(outputStream, textEncoder.encode(formatted))
      }
      await options.onOutput?.(event)
    }
    const consume = async (
      stream: ReadableStream<Uint8Array>,
      streamName: ProcessOutput,
    ): Promise<void> => {
      const reader = stream.getReader()
      activeReaders.add(reader)
      const decoder = new TextDecoder()
      let pending = ""
      const flushPending = async (final: boolean): Promise<void> => {
        while (pending.length > MAX_PENDING_LINE_LENGTH) {
          const newline = pending.indexOf("\n", MAX_PENDING_LINE_LENGTH)
          const cut = newline < 0 ? MAX_PENDING_LINE_LENGTH : newline + 1
          const part = pending.slice(0, cut)
          pending = pending.slice(cut)
          await emit({ stream: streamName, text: part.endsWith("\n") ? part : `${part}\n` })
        }
        if (final && pending) {
          await emit({ stream: streamName, text: pending })
          pending = ""
        }
      }
      try {
        while (true) {
          const chunk = await reader.read()
          if (chunk.done) {
            break
          }
          pending += decoder.decode(chunk.value, { stream: true })
          const lines = pending.split("\n")
          pending = lines.pop() ?? ""
          for (const line of lines) {
            await emit({ stream: streamName, text: `${line}\n` })
          }
          await flushPending(false)
        }
        pending += decoder.decode()
        await flushPending(true)
      } catch (error) {
        streamsError = error
        if (spawned) {
          await terminateProcess(spawned.process, spawned.processGroup, true)
        }
        throw error
      } finally {
        activeReaders.delete(reader)
        reader.releaseLock()
      }
    }
    streamsDone = Promise.all([
      consume(spawned.process.stdout, "stdout"),
      consume(spawned.process.stderr, "stderr"),
    ]).then(
      () => true,
      (error) => {
        streamsError = error
        return true
      },
    )
    const statusPromise = spawned.process.status
    const timeoutSeconds = options.timeoutSeconds ?? 0
    status = timeoutSeconds > 0
      ? await raceWithTimeout(statusPromise, timeoutSeconds * 1000)
      : await statusPromise
    if (status === undefined) {
      timedOut = true
      await emit({
        stream: "stderr",
        text: `[${options.label}] timeout after ${timeoutSeconds}s; collecting stacks\n`,
      })
      await collectStack(spawned.process, options, appendLog)
      await terminateProcess(spawned.process, spawned.processGroup, true)
    } else if (spawned.processGroup) {
      await terminateProcess(spawned.process, true, false)
    }
    await waitForStreams()
    if (streamsError) {
      throw streamsError
    }
    const finalStatus = status ?? await statusPromise
    return { code: timedOut && finalStatus.code === 0 ? 124 : finalStatus.code, timedOut }
  } finally {
    if (spawned && (status === undefined || activeReaders.size > 0)) {
      await terminateProcess(spawned.process, spawned.processGroup, true)
    }
    await cancelStreams()
    await waitForStreams()
    await logWrite.catch(() => {})
    try {
      logFile?.close()
    } catch {
      // Preserve the process result when a diagnostic file closes late.
    }
  }
}

export const normalizeRngSeed = (testOpts: string[], now = Date.now()): string[] => {
  const result = [...testOpts]
  const seedIndex = result.findIndex((arg) => arg === "--rng-seed" || arg.startsWith("--rng-seed="))
  if (seedIndex < 0) {
    return result
  }
  const value = result[seedIndex] === "--rng-seed"
    ? result[seedIndex + 1]
    : result[seedIndex].slice("--rng-seed=".length)
  if (value === undefined || value === "") {
    throw new Error("--rng-seed requires a value")
  }
  const resolved = value === "time" ? String(Math.floor(now / 1000) >>> 0) : value
  if (!/^\d+$/.test(resolved) || Number(resolved) > 0xffffffff) {
    throw new Error(`--rng-seed must be time or an unsigned integer: ${value}`)
  }
  if (result[seedIndex] === "--rng-seed") {
    result[seedIndex + 1] = resolved
  } else {
    result[seedIndex] = `--rng-seed=${resolved}`
  }
  return result
}

const diagnosticSeed = (testOpts: string[]): string => {
  const seedIndex = testOpts.findIndex((arg) =>
    arg === "--rng-seed" || arg.startsWith("--rng-seed=")
  )
  if (seedIndex < 0) {
    return "unspecified"
  }
  return testOpts[seedIndex] === "--rng-seed"
    ? testOpts[seedIndex + 1] ?? "missing"
    : testOpts[seedIndex].slice("--rng-seed=".length)
}

const detectCpuCount = async (): Promise<number> => {
  for (const [command, args] of [["nproc", []], ["getconf", ["_NPROCESSORS_ONLN"]]] as const) {
    try {
      const result = await commandOutput(command, [...args])
      if (result.code === 0) {
        return parsePositiveInt(command, result.stdout.trim())
      }
    } catch {
      // Try the next detector.
    }
  }
  return 4
}

const normalizedOptions = async (
  options: Options,
): Promise<Options & { jobs: number; nonSlowShards: number }> => {
  const jobs = options.jobs === "auto" ? await detectCpuCount() : options.jobs
  const nonSlowShards = options.nonSlowShards === "auto"
    ? Math.max(4, Math.min(8, Math.ceil(jobs * 0.75)))
    : options.nonSlowShards
  return { ...options, jobs, nonSlowShards }
}

const ensureFilenameTags = (testOpts: string[]): string[] =>
  testOpts.includes("--filenames-as-tags") ? testOpts : ["--filenames-as-tags", ...testOpts]

export const extractFileTags = (text: string): string[] =>
  [...text.matchAll(/\[#.*?_test\]/g)].map((match) => match[0]).toSorted()
    .filter((tag, index, tags) => index === 0 || tag !== tags[index - 1])

const distributeWorkItems = (items: Shard[], shardCount: number): Shard[] => {
  const bins = Array.from({ length: shardCount }, (_, index) => ({
    name: `${String(index + 1).padStart(2, "0")}-cpu`,
    filters: [] as string[],
    estimatedSeconds: 0,
    compute: "cpu" as const,
  }))
  const sortedItems = items.toSorted((a, b) =>
    b.estimatedSeconds - a.estimatedSeconds || a.name.localeCompare(b.name)
  )
  for (const item of sortedItems) {
    bins.sort((a, b) => a.estimatedSeconds - b.estimatedSeconds || a.name.localeCompare(b.name))
    const bin = bins[0]
    bin.filters.push(...item.filters)
    bin.estimatedSeconds += item.estimatedSeconds
  }
  return bins.filter((bin) => bin.filters.length > 0)
}

const tagWorkItem = (tag: string, prefix: "slow" | "non-slow"): Shard => ({
  name: tag,
  filters: [prefix === "slow" ? `[slow] ~starting_items ${tag}` : `~[slow] ~[.] ${tag}`],
  estimatedSeconds: tagWeights.get(tag) ?? 10,
  compute: "cpu",
})

export const buildShardPlan = (
  allTags: string[],
  slowTags: string[],
  nonSlowShards: number,
  _slowShards: number,
): Shard[] => {
  const visibility = fixedShards.filter((shard) => shard.compute === "gpu_software")
  const cpuFixed = fixedShards.filter((shard) => shard.compute === "cpu")
  const nonSlowTags = allTags.filter((tag) => !specialTagPattern.test(tag))
  const slowItems = slowTags.length > 0 ? slowTags.map((tag) => tagWorkItem(tag, "slow")) : [{
    name: "slow",
    filters: ["[slow] ~starting_items"],
    estimatedSeconds: 30,
    compute: "cpu" as const,
  }]
  const cpuItems = [
    ...cpuFixed,
    ...nonSlowTags.map((tag) => tagWorkItem(tag, "non-slow")),
    ...slowItems,
  ]
  return [
    ...visibility,
    ...distributeWorkItems(cpuItems, nonSlowShards),
  ].toSorted((a, b) => b.estimatedSeconds - a.estimatedSeconds || a.name.localeCompare(b.name))
}

const prepareShardDir = async (): Promise<{ path: string; cleanup: () => Promise<void> }> => {
  const configured = Deno.env.get("CATA_TEST_SHARD_DIR")
  if (configured) {
    await emptyDir(configured)
    return { path: configured, cleanup: async () => {} }
  }
  const path = await Deno.makeTempDir({
    dir: Deno.env.get("RUNNER_TEMP") ?? "/tmp",
    prefix: "cata-test-shards.",
  })
  return { path, cleanup: async () => await Deno.remove(path, { recursive: true }) }
}

const writeShardFiles = async (shardDir: string, shards: Shard[]): Promise<void> => {
  await ensureDir(shardDir)
  await Promise.all(
    shards.map((shard) =>
      Deno.writeTextFile(join(shardDir, shard.name), `${shard.filters.join("\n")}\n`)
    ),
  )
}

export const discoverTags = async (
  options: Options & { jobs: number; nonSlowShards: number },
  shardDir: string,
  logDir: string,
) => {
  const testOpts = ensureFilenameTags(options.testOpts)
  const discoveryEnv = {
    CATA_TEST_COMPUTE_ACCELERATION: Deno.env.get("CATA_TEST_COMPUTE_ACCELERATION") ?? "cpu",
    CATA_TEST_SHARD_DIAGNOSTICS: options.diagnostics ? "1" : "",
  }
  const discover = async (label: string, args: string[]): Promise<CommandResult> => {
    let stdout = ""
    let stderr = ""
    const result = await runProcess({
      command: options.testBin,
      args,
      env: discoveryEnv,
      timeoutSeconds: options.discoveryTimeoutSeconds,
      label,
      logPath: options.diagnostics ? join(logDir, `${label}.log`) : undefined,
      initialLogText: options.diagnostics
        ? `CATA_TEST_DIAGNOSTIC shard=${label} seed=${diagnosticSeed(testOpts)} filter=${
          args.at(-1) ?? "discovery"
        }\n` +
          `repro=${JSON.stringify([options.testBin, ...args])}\n`
        : undefined,
      streamOutput: options.diagnostics,
      onOutput: (event) => {
        if (event.stream === "stdout") {
          stdout += event.text
        } else {
          stderr += event.text
        }
      },
    })
    if (stderr && !options.diagnostics) {
      console.error(stderr)
    }
    if (result.timedOut || result.code !== 0) {
      throw new Error(`${label} failed with status ${result.code}`)
    }
    return { code: result.code, stdout, stderr }
  }
  const all = await discover("discovery-all", [
    ...testOpts,
    `--user-dir=${join(shardDir, "discovery")}`,
    "--list-tags",
  ])
  const slow = await discover("discovery-slow", [
    ...testOpts,
    `--user-dir=${join(shardDir, "slow-discovery")}`,
    "--list-tags",
    "[slow] ~starting_items",
  ])
  return { allTags: extractFileTags(all.stdout), slowTags: extractFileTags(slow.stdout), testOpts }
}

const defaultCpuShardCompute = (): string | undefined =>
  Deno.build.os === "windows" ? undefined : "cpu"

const runShard = async (
  options: Options & { jobs: number; nonSlowShards: number },
  testOpts: string[],
  shard: Shard,
  logDir: string,
): Promise<number> => {
  const explicitCompute = Deno.env.get("CATA_TEST_COMPUTE_ACCELERATION")
  const compute = explicitCompute ??
    (shard.compute === "gpu_software"
      ? Deno.env.get("CATA_TEST_VISIBILITY_COMPUTE_ACCELERATION") ?? "gpu_software"
      : defaultCpuShardCompute())
  const userDirPrefix = Deno.env.get("CATA_TEST_USER_DIR_PREFIX") ?? "test_user_dir"
  const userDir = `${userDirPrefix}_${shard.name}`
  const filter = shard.filters.join(",")
  const start = Date.now()
  console.log(`Starting shard ${shard.name} with ${compute ?? "default"} compute`)
  const env: Record<string, string> = compute === undefined
    ? {}
    : { CATA_TEST_COMPUTE_ACCELERATION: compute }
  const bufferedOutput = options.diagnostics ? undefined : [] as ProcessEvent[]
  const shardArgs = [...testOpts, `--user-dir=${userDir}`, filter]
  const result = await runProcess({
    command: options.testBin,
    args: shardArgs,
    env: {
      ...env,
      CATA_TEST_SHARD_NAME: shard.name,
      CATA_TEST_SHARD_DIAGNOSTICS: options.diagnostics ? "1" : "",
    },
    timeoutSeconds: options.shardTimeoutSeconds,
    label: shard.name,
    logPath: options.diagnostics ? join(logDir, `${safeFileName(shard.name)}.log`) : undefined,
    initialLogText: options.diagnostics
      ? `CATA_TEST_DIAGNOSTIC shard=${shard.name} seed=${
        diagnosticSeed(testOpts)
      } filter=${filter}\n` +
        `repro=${JSON.stringify([options.testBin, ...shardArgs])}\n`
      : undefined,
    streamOutput: options.diagnostics,
    onOutput: (event) => {
      bufferedOutput?.push(event)
    },
  })
  if (bufferedOutput) {
    for (const event of bufferedOutput) {
      const print = event.stream === "stdout" ? console.log : console.error
      print(event.text.trimEnd())
    }
  }
  const elapsed = Math.round((Date.now() - start) / 1000)
  console.log(
    `Finished shard ${shard.name} in ${elapsed}s with status ${result.code}` +
      (result.timedOut ? " (timed out)" : ""),
  )
  return result.timedOut ? 124 : result.code
}

const run = async (options: Options): Promise<number> => {
  const parsed = await normalizedOptions(options)
  if (!["auto", "file-tags", "tiles", "legacy"].includes(parsed.mode)) {
    throw new Error(`Unknown shard mode: ${parsed.mode}`)
  }
  const mode = parsed.mode === "legacy" ? "legacy" : "file-tags"
  const shardDir = await prepareShardDir()
  const logDir = Deno.env.get("CATA_TEST_SHARD_LOG_DIR") ?? join(shardDir.path, "logs")
  await ensureDir(logDir)
  const testOpts = normalizeRngSeed(parsed.testOpts)
  let completed = false
  try {
    if (mode === "legacy") {
      const shard: Shard = {
        name: "00-legacy",
        filters: ["[slow] ~starting_items,~[slow] ~[.],starting_items"],
        estimatedSeconds: 1,
        compute: "cpu",
      }
      if (parsed.dryRun) {
        console.log(`${shard.name}: ${shard.filters.join(",")}`)
        completed = true
        return 0
      }
      const legacyStatus = await runShard(parsed, testOpts, shard, logDir)
      completed = legacyStatus === 0
      return legacyStatus
    }

    const { allTags, slowTags, testOpts: discoveredTestOpts } = await discoverTags(
      { ...parsed, testOpts },
      shardDir.path,
      logDir,
    )
    const shards = buildShardPlan(allTags, slowTags, parsed.nonSlowShards, parsed.slowShards)
    await writeShardFiles(shardDir.path, shards)
    if (parsed.dryRun) {
      for (const shard of shards.toSorted((a, b) => a.name.localeCompare(b.name))) {
        console.log(`${basename(shard.name)}: ${shard.filters.join(",")}`)
      }
      completed = true
      return 0
    }

    let status = 0
    for await (
      const code of pooledMap(
        parsed.jobs,
        shards,
        (shard) => runShard(parsed, discoveredTestOpts, shard, logDir),
      )
    ) {
      if (code !== 0 && status === 0) {
        status = code
      }
    }
    completed = status === 0
    return status
  } finally {
    if (!completed && parsed.diagnostics && !Deno.env.get("CATA_TEST_SHARD_LOG_DIR")) {
      console.error(`Test shard diagnostics retained at ${logDir}`)
    } else {
      await shardDir.cleanup()
    }
  }
}

if (import.meta.main) {
  try {
    const { options, args, literal } = await new Command()
      .name("run-test-shards")
      .description("Run Cataclysm: Bright Nights tests as filename-tag shards")
      .option("--mode <mode:string>", "auto, file-tags, tiles, or legacy", { default: "auto" })
      .option("--jobs <jobs:string>", "concurrent shard jobs, or auto", { default: "auto" })
      .option("--slow-shards <slowShards:string>", "accepted for compatibility", { default: "4" })
      .option("--non-slow-shards <nonSlowShards:string>", "generated CPU shards, or auto", {
        default: "auto",
      })
      .option("--dry-run", "print shard filters without running tests")
      .option("--diagnostics", "stream and retain tagged shard diagnostics")
      .option("--shard-timeout-seconds <seconds:string>", "per-shard timeout", {
        default: Deno.env.get("CATA_TEST_SHARD_TIMEOUT_SECONDS") ?? "1200",
      })
      .option(
        "--discovery-timeout-seconds <seconds:string>",
        "timeout for each discovery command",
        {
          default: Deno.env.get("CATA_TEST_DISCOVERY_TIMEOUT_SECONDS") ?? "120",
        },
      )
      .arguments("[testBin:string] [...testOpts:string]")
      .parse(Deno.args)
    const jobs = options.jobs === "auto" ? "auto" : parsePositiveInt("--jobs", options.jobs)
    const nonSlowShards = options.nonSlowShards === "auto"
      ? "auto"
      : parsePositiveInt("--non-slow-shards", options.nonSlowShards)
    const testOpts = [
      ...args.slice(1).filter((arg): arg is string => arg !== undefined),
      ...literal,
    ]
    Deno.exit(
      await run({
        mode: options.mode as Mode,
        jobs,
        slowShards: parsePositiveInt("--slow-shards", options.slowShards),
        nonSlowShards,
        dryRun: Boolean(options.dryRun),
        diagnostics: Boolean(options.diagnostics),
        shardTimeoutSeconds: parsePositiveInt(
          "--shard-timeout-seconds",
          options.shardTimeoutSeconds,
        ),
        discoveryTimeoutSeconds: parsePositiveInt(
          "--discovery-timeout-seconds",
          options.discoveryTimeoutSeconds,
        ),
        testBin: args[0] ?? defaultTestBin,
        testOpts: testOpts.length > 0 ? testOpts : defaultTestOpts,
      }),
    )
  } catch (error) {
    const message = error instanceof Error ? error.message : String(error)
    console.error(`Error: ${message}`)
    Deno.exit(1)
  }
}
