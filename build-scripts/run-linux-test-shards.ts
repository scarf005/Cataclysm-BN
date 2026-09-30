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
import { assertEquals, assertThrows } from "@std/assert"
import { pooledMap } from "@std/async"
import { emptyDir, ensureDir } from "@std/fs"
import { basename, dirname, join } from "@std/path"
import * as v from "@valibot/valibot"

type Mode = "auto" | "file-tags" | "tiles" | "legacy"

type Options = {
  mode: Mode
  jobs: number | "auto"
  slowShards: number
  nonSlowShards: number | "auto"
  dryRun: boolean
  testBin: string
  testOpts: string[]
  timingsPath?: string
  writeTimingsPath?: string
  timingPath?: string
}

type TimingProfile = {
  version: 1
  tests: Record<string, number>
  tags: Record<string, number>
}

type Catch2Timing = {
  name: string
  durationSeconds: number
  tags: string[]
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

const timingSecondsSchema = v.pipe(v.number(), v.finite(), v.minValue(0))
const timingProfileSchema = v.object({
  version: v.literal(1),
  tests: v.record(v.string(), timingSecondsSchema),
  tags: v.optional(v.record(v.string(), timingSecondsSchema)),
})

export const parseTimingProfile = (value: unknown): TimingProfile => {
  const result = v.safeParse(timingProfileSchema, value)
  if (!result.success) {
    throw new Error("Timing profile must contain version 1 and non-negative numeric tests")
  }
  return { ...result.output, tags: result.output.tags ?? {} }
}

export const parseCatch2Timings = (xml: string): Catch2Timing[] => {
  const decodeXml = (value: string): string =>
    value.replace(
      /&(?:amp|quot|apos|lt|gt);/g,
      (entity) => ({
        "&amp;": "&",
        "&quot;": '"',
        "&apos;": "'",
        "&lt;": "<",
        "&gt;": ">",
      }[entity] ?? entity),
    )
  const attribute = (attributes: string, name: string): string | undefined => {
    const match = attributes.match(new RegExp(`\\b${name}\\s*=\\s*(["'])(.*?)\\1`))
    return match?.[2] === undefined ? undefined : decodeXml(match[2])
  }
  return [...xml.matchAll(/<TestCase\b([^>]*)>([\s\S]*?)<\/TestCase>/g)].flatMap((match) => {
    const attributes = match[1] ?? ""
    const body = match[2] ?? ""
    const name = attribute(attributes, "name")
    const overallResult = body.match(/<OverallResult\b([^>]*)>/)?.[1] ?? ""
    const duration = attribute(overallResult, "durationInSeconds")
    if (name === undefined || duration === undefined) {
      return []
    }
    const durationSeconds = Number(duration)
    if (!Number.isFinite(durationSeconds) || durationSeconds < 0) {
      return []
    }
    return [{
      name,
      durationSeconds,
      tags: [...(attribute(attributes, "tags") ?? "").matchAll(/\[[^\]]+\]/g)].map((tag) => tag[0]),
    }]
  })
}

export const timingProfileFromXml = (xmlFiles: string[]): TimingProfile => {
  const tests: Record<string, number> = {}
  const tags: Record<string, number> = {}
  for (const xml of xmlFiles) {
    for (const timing of parseCatch2Timings(xml)) {
      tests[timing.name] = (tests[timing.name] ?? 0) + timing.durationSeconds
      for (const tag of timing.tags.filter((tag) => /^\[#.*?_test\]$/.test(tag))) {
        tags[tag] = (tags[tag] ?? 0) + timing.durationSeconds
        tags[`slow:${tag}`] ??= 0
        tags[`non-slow:${tag}`] ??= 0
        if (timing.name !== "starting_items") {
          const prefix = timing.tags.includes("[slow]") ? "slow" : "non-slow"
          const key = `${prefix}:${tag}`
          tags[key] = (tags[key] ?? 0) + timing.durationSeconds
        }
      }
    }
  }
  return parseTimingProfile({ version: 1, tests, tags })
}

const loadTimingProfile = async (path: string): Promise<TimingProfile> =>
  parseTimingProfile(JSON.parse(await Deno.readTextFile(path)))

const writeTimingProfile = async (path: string, xmlPaths: string[]): Promise<void> => {
  const xmlFiles = await Promise.all(xmlPaths.map((xmlPath) => Deno.readTextFile(xmlPath)))
  const profile = timingProfileFromXml(xmlFiles)
  await ensureDir(dirname(path))
  await Deno.writeTextFile(path, `${JSON.stringify(profile, null, 2)}\n`)
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
  const result = await new Deno.Command(command, { args, env, stdout: "piped", stderr: "piped" })
    .output()
  const decoder = new TextDecoder()
  return {
    code: result.code,
    stdout: decoder.decode(result.stdout),
    stderr: decoder.decode(result.stderr),
  }
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

const tagWorkItem = (
  tag: string,
  prefix: "slow" | "non-slow",
  timings?: TimingProfile,
): Shard => ({
  name: tag,
  filters: [prefix === "slow" ? `[slow] ~starting_items ${tag}` : `~[slow] ~[.] ${tag}`],
  estimatedSeconds: timings?.tags[`${prefix}:${tag}`] ?? timings?.tags[tag] ??
    tagWeights.get(tag) ?? 10,
  compute: "cpu",
})

type ShardPlanOptions = {
  allTags: string[]
  slowTags: string[]
  nonSlowShards: number
  timings?: TimingProfile
}

const measuredFixedShard = (shard: Shard, timings?: TimingProfile): Shard => {
  if (timings === undefined) return shard
  const weights = shard.filters.flatMap((filter) => filter.split(",")).flatMap((filter) => {
    const tag = extractFileTags(filter)[0]
    if (tag !== undefined) {
      const duration = timings.tags[tag]
      return duration === undefined ? [] : [duration]
    }
    return Object.entries(timings.tests)
      .filter(([name]) =>
        filter.endsWith("*") ? name.startsWith(filter.slice(0, -1)) : name === filter
      )
      .map(([, duration]) => duration)
  })
  return weights.length === 0
    ? shard
    : { ...shard, estimatedSeconds: weights.reduce((total, seconds) => total + seconds, 0) }
}

export const buildShardPlan = (
  { allTags, slowTags, nonSlowShards, timings }: ShardPlanOptions,
): Shard[] => {
  const measuredFixed = fixedShards.map((shard) => measuredFixedShard(shard, timings))
  const visibility = measuredFixed.filter((shard) => shard.compute === "gpu_software")
  const cpuFixed = measuredFixed.filter((shard) => shard.compute === "cpu")
  const nonSlowTags = allTags.filter((tag) => !specialTagPattern.test(tag))
  const slowItems = slowTags.length > 0
    ? slowTags.map((tag) => tagWorkItem(tag, "slow", timings))
    : [{
      name: "slow",
      filters: ["[slow] ~starting_items"],
      estimatedSeconds: 30,
      compute: "cpu" as const,
    }]
  const cpuItems = [
    ...cpuFixed,
    ...nonSlowTags.map((tag) => tagWorkItem(tag, "non-slow", timings)),
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

const discoverTags = async (
  options: Options & { jobs: number; nonSlowShards: number },
  shardDir: string,
) => {
  const testOpts = ensureFilenameTags(options.testOpts)
  const discoveryEnv = {
    CATA_TEST_COMPUTE_ACCELERATION: Deno.env.get("CATA_TEST_COMPUTE_ACCELERATION") ?? "cpu",
  }
  const all = await commandOutput(options.testBin, [
    ...testOpts,
    `--user-dir=${join(shardDir, "discovery")}`,
    "--list-tags",
  ], discoveryEnv)
  if (all.stderr) {
    console.error(all.stderr)
  }
  const slow = await commandOutput(
    options.testBin,
    [
      ...testOpts,
      `--user-dir=${join(shardDir, "slow-discovery")}`,
      "--list-tags",
      "[slow] ~starting_items",
    ],
    discoveryEnv,
  )
  if (slow.stderr) {
    console.error(slow.stderr)
  }
  return { allTags: extractFileTags(all.stdout), slowTags: extractFileTags(slow.stdout), testOpts }
}

const defaultCpuShardCompute = (): string | undefined =>
  Deno.build.os === "windows" ? undefined : "cpu"

const runShard = async (
  options: Options & { jobs: number; nonSlowShards: number },
  testOpts: string[],
  shard: Shard,
): Promise<number> => {
  const timingPath = options.timingPath
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
  if (timingPath !== undefined) {
    await ensureDir(dirname(timingPath))
  }
  const result = await commandOutput(options.testBin, [
    ...testOpts,
    ...(timingPath === undefined
      ? []
      : ["--reporter", "xml", "--out", timingPath, "--durations", "yes"]),
    `--user-dir=${userDir}`,
    filter,
  ], env)
  if (result.stdout) {
    console.log(result.stdout.trimEnd())
  }
  if (result.stderr) {
    console.error(result.stderr.trimEnd())
  }
  const elapsed = Math.round((Date.now() - start) / 1000)
  console.log(`Finished shard ${shard.name} in ${elapsed}s with status ${result.code}`)
  return result.code
}

const run = async (options: Options): Promise<number> => {
  const parsed = await normalizedOptions(options)
  if (!["auto", "file-tags", "tiles", "legacy"].includes(parsed.mode)) {
    throw new Error(`Unknown shard mode: ${parsed.mode}`)
  }
  const mode = parsed.mode === "legacy" ? "legacy" : "file-tags"
  const shardDir = await prepareShardDir()
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
        return 0
      }
      const timingPath = parsed.writeTimingsPath === undefined
        ? undefined
        : join(shardDir.path, "timings", `${shard.name}.xml`)
      const status = await runShard({ ...parsed, timingPath }, parsed.testOpts, shard)
      if (parsed.writeTimingsPath !== undefined) {
        await writeTimingProfile(parsed.writeTimingsPath, [timingPath!])
      }
      return status
    }

    const timings = parsed.timingsPath === undefined
      ? undefined
      : await loadTimingProfile(parsed.timingsPath)
    const { allTags, slowTags, testOpts } = await discoverTags(parsed, shardDir.path)
    const shards = buildShardPlan({
      allTags,
      slowTags,
      nonSlowShards: parsed.nonSlowShards,
      timings,
    })
    await writeShardFiles(shardDir.path, shards)
    if (parsed.dryRun) {
      for (const shard of shards.toSorted((a, b) => a.name.localeCompare(b.name))) {
        console.log(`${basename(shard.name)}: ${shard.filters.join(",")}`)
      }
      return 0
    }

    const timingPaths = parsed.writeTimingsPath === undefined
      ? []
      : shards.map((shard) => join(shardDir.path, "timings", `${shard.name}.xml`))
    let status = 0
    for await (
      const code of pooledMap(
        parsed.jobs,
        shards,
        (shard) =>
          runShard(
            {
              ...parsed,
              timingPath: parsed.writeTimingsPath === undefined
                ? undefined
                : join(shardDir.path, "timings", `${shard.name}.xml`),
            },
            testOpts,
            shard,
          ),
      )
    ) {
      if (code !== 0 && status === 0) {
        status = code
      }
    }
    if (parsed.writeTimingsPath !== undefined) {
      await writeTimingProfile(parsed.writeTimingsPath, timingPaths)
    }
    return status
  } finally {
    await shardDir.cleanup()
  }
}

Deno.test("extractFileTags returns sorted unique file tags", () => {
  assertEquals(extractFileTags("x [#b_test] [foo] [#a_test] [#b_test]"), ["[#a_test]", "[#b_test]"])
})

Deno.test("parseCatch2Timings uses the test-case OverallResult", () => {
  const timings = parseCatch2Timings(`
    <TestCase name="test &amp; one" tags="[#alpha_test] [slow]">
      <Section name="nested">
        <OverallResults durationInSeconds="99" />
      </Section>
      <OverallResult success="true" durationInSeconds="1.5" />
    </TestCase>
  `)
  assertEquals(timings, [{
    name: "test & one",
    durationSeconds: 1.5,
    tags: ["[#alpha_test]", "[slow]"],
  }])
})

Deno.test("parseTimingProfile rejects malformed and negative timings", () => {
  assertThrows(() => parseTimingProfile({ version: 1, tests: { "bad": -1 } }))
  assertThrows(() => parseTimingProfile({ version: 2, tests: {} }))
  assertThrows(() => parseTimingProfile({ version: 1, tests: { bad: Infinity } }))
  assertEquals(parseTimingProfile({ version: 1, tests: {} }).tags, {})
})

Deno.test("timing assignment keeps every generated tag exactly once", () => {
  const allTags = ["[#a_test]", "[#vision_test]", "[#b_test]"]
  const slowTags = ["[#slow_test]"]
  const shards = buildShardPlan({
    allTags,
    slowTags,
    nonSlowShards: 3,
    timings: {
      version: 1,
      tests: {},
      tags: { "[#a_test]": 50 },
    },
  })
  const assignedTags = shards.flatMap((shard) => extractFileTags(shard.filters.join(" ")))
    .filter((tag) => !specialTagPattern.test(tag))
  assertEquals(assignedTags.toSorted(), ["[#a_test]", "[#b_test]", "[#slow_test]"])
  assertEquals(new Set(assignedTags).size, assignedTags.length)
})

Deno.test("timing profiles separate slow work and exclude fixed starting items", () => {
  const profile = timingProfileFromXml([`
    <TestCase name="slow case" tags="[#mixed_test][slow]">
      <OverallResult durationInSeconds="7" />
    </TestCase>
    <TestCase name="fast case" tags="[#mixed_test]">
      <OverallResult durationInSeconds="2" />
    </TestCase>
    <TestCase name="starting_items" tags="[#mixed_test][slow]">
      <OverallResult durationInSeconds="50" />
    </TestCase>
  `])
  assertEquals(profile.tags["slow:[#mixed_test]"], 7)
  assertEquals(profile.tags["non-slow:[#mixed_test]"], 2)
  assertEquals(
    measuredFixedShard(fixedShards.find((shard) => shard.name === "22-starting-items")!, profile)
      .estimatedSeconds,
    50,
  )
})

Deno.test("measured plans are deterministic and preserve every original filter and compute choice", () => {
  const options = {
    allTags: ["[#a_test]", "[#b_test]", "[#vision_test]"],
    slowTags: ["[#a_test]"],
    nonSlowShards: 3,
  }
  const baseline = buildShardPlan(options)
  const measured = buildShardPlan({
    ...options,
    timings: {
      version: 1,
      tests: { starting_items: 1 },
      tags: { "slow:[#a_test]": 70, "non-slow:[#a_test]": 2, "non-slow:[#b_test]": 10 },
    },
  })
  const filters = (shards: Shard[]) =>
    shards.flatMap((shard) => shard.filters.map((filter) => `${shard.compute}:${filter}`))
      .toSorted()
  assertEquals(filters(measured), filters(baseline))
  assertEquals(
    buildShardPlan({
      ...options,
      allTags: [...options.allTags].reverse(),
      timings: { version: 1, tests: {}, tags: {} },
    }),
    baseline,
  )
})

Deno.test("buildShardPlan keeps visibility out of CPU shards", () => {
  const shards = buildShardPlan({
    allTags: ["[#vision_test]", "[#map_test]"],
    slowTags: [],
    nonSlowShards: 2,
  })
  const cpuFilters = shards.filter((shard) => shard.name.endsWith("cpu")).flatMap((
    shard,
  ) => shard.filters)
  assertEquals(cpuFilters.some((filter) => filter.includes("vision_test")), false)
  assertEquals(cpuFilters.some((filter) => filter.includes("map_test")), true)
})

Deno.test("buildShardPlan keeps visibility on its GPU shard", () => {
  const shards = buildShardPlan({
    allTags: ["[#enchantment_test]", "[#tiny_test]"],
    slowTags: [],
    nonSlowShards: 2,
  })
  const visibility = shards.find((shard) => shard.name === "20-visibility")
  assertEquals(visibility?.compute, "gpu_software")
})

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
      .option("--timings <timingsPath:string>", "load duration-aware shard timings from JSON")
      .option(
        "--write-timings <writeTimingsPath:string>",
        "write per-test Catch2 XML timings as JSON",
      )
      .option("--dry-run", "print shard filters without running tests")
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
        testBin: args[0] ?? defaultTestBin,
        testOpts: testOpts.length > 0 ? testOpts : defaultTestOpts,
        timingsPath: options.timings,
        writeTimingsPath: options.writeTimings,
      }),
    )
  } catch (error) {
    const message = error instanceof Error ? error.message : String(error)
    console.error(`Error: ${message}`)
    Deno.exit(1)
  }
}
