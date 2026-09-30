#!/usr/bin/env -S deno run --allow-read --allow-write --allow-env --allow-net

/**
 * @module
 *
 * Runs the fork-only GitHub Actions cache quota and retention experiment against
 * one native Windows ccache snapshot.  It deliberately uses the Actions cache
 * SDK rather than a local fixture so that eviction and restore behavior remain
 * observable.
 */

import { Command } from "@cliffy/command"
import * as v from "@valibot/valibot"
import { isAbsolute, join, relative } from "@std/path"

export const SOURCE_CACHE_PREFIX = "ccache-windows-speedtest-windows-ci-ci/build-speed-experiments-"
export const SOURCE_CACHE_VERSION =
  "5fc4fda399480b6dac2a2b8f8dc80aa875cf8b1004279f884605b464e18e3051"
export const EXPERIMENT_BRANCH = "ci/build-speed-experiments"
export const EXPERIMENT_REPOSITORY = "scarf005/Cataclysm-BN"
export const POLL_INTERVAL_MS = 30_000
export const POLL_TIMEOUT_MS = 10 * 60 * 1000

const CacheSchema = v.object({
  id: v.number(),
  key: v.string(),
  version: v.string(),
  ref: v.string(),
  size_in_bytes: v.number(),
  created_at: v.string(),
  last_accessed_at: v.string(),
})
const CacheListSchema = v.object({
  total_count: v.number(),
  actions_caches: v.array(CacheSchema),
})
const RunSchema = v.object({
  id: v.number(),
  status: v.string(),
  conclusion: v.nullable(v.string()),
  head_branch: v.string(),
})
const JobsSchema = v.object({
  jobs: v.array(
    v.object({
      name: v.string(),
      status: v.string(),
      conclusion: v.nullable(v.string()),
      steps: v.array(
        v.object({ name: v.string(), status: v.string(), conclusion: v.nullable(v.string()) }),
      ),
    }),
  ),
})

type CacheEntry = v.InferOutput<typeof CacheSchema>
type CacheList = v.InferOutput<typeof CacheListSchema>
type Run = v.InferOutput<typeof RunSchema>

export type PayloadDigest = {
  digest: string
  files: number
  bytes: number
}

export type SourceSelection = {
  key: string
  size_in_bytes: number
}

const encoder = new TextEncoder()
type CacheSdk = typeof import("npm:@actions/cache@4.0.5")
let cacheSdk: CacheSdk | undefined

const getCacheSdk = async (): Promise<CacheSdk> =>
  cacheSdk ??= await import("npm:@actions/cache@4.0.5")

export const runPrefix = (runId: string): string => `bn-speed-pressure-${runId}-`

export const isOwnedKey = (key: string, runId: string): boolean => key.startsWith(runPrefix(runId))

export const baselineKey = (runId: string, flavor: string): string =>
  `${runPrefix(runId)}baseline-${flavor}`

export const policyRareKey = (runId: string, flavor: string): string =>
  `${runPrefix(runId)}policy-rare-${flavor}`

export const policyHotKey = (runId: string, generation: number): string =>
  `${runPrefix(runId)}policy-hot-${generation}`

export const selectSourceCache = (
  entries: readonly CacheEntry[],
  prefix = SOURCE_CACHE_PREFIX,
  version = SOURCE_CACHE_VERSION,
): SourceSelection | undefined => {
  const candidates = entries
    .filter((entry) =>
      entry.key.startsWith(prefix) && entry.version === version &&
      entry.ref === `refs/heads/${EXPERIMENT_BRANCH}`
    )
    .toSorted((left, right) => Date.parse(right.created_at) - Date.parse(left.created_at))
  const selected = candidates[0]
  return selected === undefined
    ? undefined
    : { key: selected.key, size_in_bytes: selected.size_in_bytes }
}

export const hotKeysToPrune = (
  { entries, runId, savedHotKeys, newestHotKey }: {
    entries: readonly CacheEntry[]
    runId: string
    savedHotKeys: readonly string[]
    newestHotKey: string
  },
): CacheEntry[] => {
  const saved = new Set(savedHotKeys)
  return entries.filter(
    (entry) =>
      isOwnedKey(entry.key, runId) &&
      saved.has(entry.key) &&
      entry.key !== newestHotKey,
  )
}

export const verifyPayloadDigest = (
  expected: PayloadDigest,
  actual: PayloadDigest,
): void => {
  if (
    expected.digest !== actual.digest ||
    expected.files !== actual.files ||
    expected.bytes !== actual.bytes
  ) {
    throw new Error(
      `Payload digest mismatch: expected ${JSON.stringify(expected)}, got ${
        JSON.stringify(actual)
      }`,
    )
  }
}

const apiBase = (): string => "https://api.github.com"

const requiredEnv = (name: string): string => {
  const value = Deno.env.get(name)
  if (value === undefined || value === "") {
    throw new Error(`Missing required environment variable: ${name}`)
  }
  return value
}

const githubRequest = async (path: string, init: RequestInit = {}): Promise<Response> => {
  const token = requiredEnv("GITHUB_TOKEN")
  const headers = new Headers(init.headers)
  headers.set("accept", "application/vnd.github+json")
  headers.set("authorization", `Bearer ${token}`)
  headers.set("x-github-api-version", "2022-11-28")
  headers.set("user-agent", "cataclysm-bn-cache-pressure")
  return await fetch(`${apiBase()}${path}`, { ...init, headers })
}

const githubJson = async (path: string): Promise<unknown> => {
  const response = await githubRequest(path)
  const body = await response.json()
  if (!response.ok) throw new Error(`GitHub API ${response.status} for ${path}`)
  return body
}

const repositoryPath = (): string => `/repos/${requiredEnv("GITHUB_REPOSITORY")}`

const readRun = async (runId: string): Promise<Run> =>
  v.parse(RunSchema, await githubJson(`${repositoryPath()}/actions/runs/${runId}`))

export const ensureFinishedPrerequisite = async (waitRunId: string): Promise<void> => {
  const currentRunId = Deno.env.get("GITHUB_RUN_ID")
  if (currentRunId !== undefined && currentRunId === waitRunId) {
    throw new Error("wait_run_id must identify a different completed workflow run")
  }
  const run = await readRun(waitRunId)
  if (run.status !== "completed" || run.head_branch !== EXPERIMENT_BRANCH) {
    throw new Error(`Prerequisite run ${waitRunId} must finish on ${EXPERIMENT_BRANCH}`)
  }
  const jobs = v.parse(
    JobsSchema,
    await githubJson(`${repositoryPath()}/actions/runs/${waitRunId}/jobs?per_page=100`),
  )
  const benchmark = jobs.jobs.find((job) => job.name.endsWith(" / Windows shard comparison"))
  const build = benchmark?.steps.find((step) => step.name === "Build CBN (windows msvc)")
  const comparison = benchmark?.steps.find((step) =>
    step.name === "Compare measured Windows test shards"
  )
  if (
    benchmark?.status !== "completed" || build?.conclusion !== "success" ||
    comparison?.status !== "completed" ||
    !["success", "failure"].includes(comparison.conclusion ?? "")
  ) {
    throw new Error(
      `Prerequisite Windows comparison in run ${waitRunId} must finish after a successful native build`,
    )
  }
}

export const listCaches = async (): Promise<CacheEntry[]> => {
  const entries: CacheEntry[] = []
  for (let page = 1;; page++) {
    const body = v.parse(
      CacheListSchema,
      await githubJson(`${repositoryPath()}/actions/caches?per_page=100&page=${page}`),
    )
    entries.push(...body.actions_caches)
    if (body.actions_caches.length < 100 || entries.length >= body.total_count) return entries
  }
}

const deleteCache = async (entry: CacheEntry): Promise<void> => {
  const response = await githubRequest(`${repositoryPath()}/actions/caches/${entry.id}`, {
    method: "DELETE",
  })
  if (!response.ok && response.status !== 404) {
    throw new Error(`Could not delete cache ${entry.id}: ${response.status}`)
  }
}

const deleteEntries = async (entries: readonly CacheEntry[]): Promise<void> => {
  for (const entry of entries) await deleteCache(entry)
}

const writeJson = async (path: string, value: unknown): Promise<void> => {
  await Deno.writeTextFile(path, `${JSON.stringify(value, null, 2)}\n`)
}

const ensureDirectory = async (path: string): Promise<void> => {
  await Deno.mkdir(path, { recursive: true })
}

const removeIfPresent = async (path: string): Promise<void> => {
  try {
    await Deno.remove(path, { recursive: true })
  } catch (error) {
    if (!(error instanceof Deno.errors.NotFound)) throw error
  }
}

const digestBytes = async (bytes: Uint8Array): Promise<string> => {
  const buffer = bytes.buffer.slice(
    bytes.byteOffset,
    bytes.byteOffset + bytes.byteLength,
  ) as ArrayBuffer
  const digest = await crypto.subtle.digest("SHA-256", buffer)
  return Array.from(new Uint8Array(digest), (byte) => byte.toString(16).padStart(2, "0")).join("")
}

const digestDirectoryEntries = async (
  root: string,
  directory: string,
): Promise<{ records: string[]; files: number; bytes: number }> => {
  const records: string[] = []
  let files = 0
  let bytes = 0
  const entries = (await Array.fromAsync(Deno.readDir(directory))).toSorted((left, right) =>
    left.name.localeCompare(right.name)
  )
  for (const entry of entries) {
    const path = join(directory, entry.name)
    if (entry.isDirectory) {
      const nested = await digestDirectoryEntries(root, path)
      records.push(...nested.records)
      files += nested.files
      bytes += nested.bytes
      continue
    }
    if (!entry.isFile) throw new Error(`Unsupported ccache directory entry: ${path}`)
    const content = await Deno.readFile(path)
    const digest = await digestBytes(content)
    const name = relative(root, path).replaceAll("\\", "/")
    records.push(`${JSON.stringify(name)}\t${content.byteLength}\t${digest}`)
    files++
    bytes += content.byteLength
  }
  return { records, files, bytes }
}

export const digestDirectory = async (root: string): Promise<PayloadDigest> => {
  const stat = await Deno.stat(root)
  if (!stat.isDirectory) throw new Error(`Payload path is not a directory: ${root}`)
  const { records, files, bytes } = await digestDirectoryEntries(root, root)
  return { digest: await digestBytes(encoder.encode(records.join("\n"))), files, bytes }
}

const copyDirectory = async (source: string, destination: string): Promise<void> => {
  await ensureDirectory(destination)
  const entries = await Array.fromAsync(Deno.readDir(source))
  for (const entry of entries) {
    const sourcePath = join(source, entry.name)
    const destinationPath = join(destination, entry.name)
    if (entry.isDirectory) await copyDirectory(sourcePath, destinationPath)
    else if (entry.isFile) await Deno.copyFile(sourcePath, destinationPath)
    else throw new Error(`Unsupported ccache directory entry: ${sourcePath}`)
  }
}

const writeInventory = async (
  evidenceDirectory: string,
  name: string,
  entries: readonly CacheEntry[],
): Promise<void> => {
  await writeJson(join(evidenceDirectory, `${name}.json`), {
    captured_at: new Date().toISOString(),
    total_count: entries.length,
    caches: entries,
  })
}

const delay = (milliseconds: number): Promise<void> =>
  new Promise((resolve) => setTimeout(resolve, milliseconds))

const assertEnvironment = (allowEviction: boolean, runId: string): void => {
  if (requiredEnv("GITHUB_REPOSITORY") !== EXPERIMENT_REPOSITORY) {
    throw new Error("This workflow is restricted to scarf005/Cataclysm-BN")
  }
  if (requiredEnv("GITHUB_REF_NAME") !== EXPERIMENT_BRANCH) {
    throw new Error(`This workflow is restricted to ${EXPERIMENT_BRANCH}`)
  }
  if (requiredEnv("GITHUB_RUN_ID") !== runId) throw new Error("run-id does not match GITHUB_RUN_ID")
  if (!allowEviction) throw new Error("allow_eviction must be explicitly true")
  if (Deno.env.get("ACTIONS_CACHE_SERVICE_V2") !== "true") {
    throw new Error("ACTIONS_CACHE_SERVICE_V2 must be enabled")
  }
  requiredEnv("ACTIONS_RUNTIME_TOKEN")
  requiredEnv("ACTIONS_RESULTS_URL")
}

const writeLog = async (path: string, line: string): Promise<void> => {
  await Deno.writeTextFile(path, `${new Date().toISOString()} ${line}\n`, { append: true })
}

const restoreAndDigest = async (
  key: string,
  path: string,
  expected: PayloadDigest,
): Promise<{ key: string | undefined; digest?: PayloadDigest }> => {
  await removeIfPresent(path)
  const restoredKey = await (await getCacheSdk()).restoreCache([path], key)
  if (restoredKey === undefined) return { key: undefined }
  if (restoredKey !== key) throw new Error(`Restore selected ${restoredKey} instead of ${key}`)
  const actual = await digestDirectory(path)
  verifyPayloadDigest(expected, actual)
  await removeIfPresent(path)
  return { key: restoredKey, digest: actual }
}

const savePayload = async (
  path: string,
  key: string,
  expected: PayloadDigest,
): Promise<number> => {
  const actual = await digestDirectory(path)
  verifyPayloadDigest(expected, actual)
  return await (await getCacheSdk()).saveCache([path], key)
}

const inventoryUntil = async (
  evidenceDirectory: string,
  name: string,
  predicate: (entries: CacheEntry[]) => boolean,
): Promise<CacheEntry[]> => {
  const deadline = Date.now() + POLL_TIMEOUT_MS
  for (let poll = 0;; poll++) {
    const entries = await listCaches()
    await writeInventory(evidenceDirectory, `${name}-${String(poll).padStart(3, "0")}`, entries)
    if (predicate(entries)) return entries
    if (Date.now() >= deadline) throw new Error(`Timed out waiting for ${name}`)
    await delay(POLL_INTERVAL_MS)
  }
}

const runExperiment = async ({
  evidenceDirectory,
  runId,
  waitRunId,
  allowEviction,
  policyOnly,
}: {
  evidenceDirectory: string
  runId: string
  waitRunId: string
  allowEviction: boolean
  policyOnly: boolean
}): Promise<void> => {
  assertEnvironment(allowEviction, runId)
  await ensureFinishedPrerequisite(waitRunId)
  await ensureDirectory(evidenceDirectory)

  const workspace = requiredEnv("GITHUB_WORKSPACE")
  if (!isAbsolute(workspace)) throw new Error("GITHUB_WORKSPACE must be absolute")
  const ccachePath = join(workspace, ".ccache")
  const goldenPath = join(workspace, ".cache-pressure-golden")
  const logPath = join(evidenceDirectory, "cache-pressure.log")
  const results: Record<string, unknown> = {
    run_id: runId,
    wait_run_id: waitRunId,
    policy_only: policyOnly,
  }
  const rareFlavors = ["rare-a", "rare-b", "rare-c"]
  const baselineRareKeys = rareFlavors.map((flavor) => baselineKey(runId, flavor))
  const policyRareKeys = rareFlavors.map((flavor) => policyRareKey(runId, flavor))
  const baselineHotKeys = Array.from(
    { length: 13 },
    (_, index) => baselineKey(runId, `hot-${index + 1}`),
  )
  const policyHotKeys = Array.from({ length: 13 }, (_, index) => policyHotKey(runId, index + 1))
  const owned = (entry: CacheEntry): boolean => isOwnedKey(entry.key, runId)

  try {
    await writeLog(
      logPath,
      `starting source inventory; source_prefix=${SOURCE_CACHE_PREFIX}; version=${SOURCE_CACHE_VERSION}`,
    )
    const before = await listCaches()
    await writeInventory(evidenceDirectory, "inventory-before-source", before)
    const source = selectSourceCache(before)
    if (source === undefined) throw new Error(`Source cache is unavailable: ${SOURCE_CACHE_PREFIX}`)
    if (source.size_in_bytes < 900_000_000) {
      throw new Error(`Source cache is unexpectedly small: ${source.size_in_bytes} bytes`)
    }
    results.source_inventory = source

    await removeIfPresent(ccachePath)
    const restoredSourceKey = await (await getCacheSdk()).restoreCache([ccachePath], source.key)
    if (restoredSourceKey !== source.key) {
      throw new Error(
        `Source restore selected ${restoredSourceKey ?? "nothing"} instead of ${source.key}`,
      )
    }
    const sourceDigest = await digestDirectory(ccachePath)
    if (sourceDigest.bytes < 900_000_000) {
      throw new Error(`Restored source payload is unexpectedly small: ${sourceDigest.bytes} bytes`)
    }
    await removeIfPresent(goldenPath)
    await copyDirectory(ccachePath, goldenPath)
    const goldenDigest = await digestDirectory(goldenPath)
    verifyPayloadDigest(sourceDigest, goldenDigest)
    results.source = { key: source.key, digest: sourceDigest, golden: goldenDigest }
    await writeJson(join(evidenceDirectory, "source-result.json"), results.source)
    await writeLog(
      logPath,
      `source restored; bytes=${sourceDigest.bytes}; digest=${sourceDigest.digest}`,
    )

    if (!policyOnly) {
      const baselineSaves: Array<{ key: string; cache_id: number }> = []
      for (const key of [...baselineRareKeys, ...baselineHotKeys]) {
        const cacheId = await savePayload(ccachePath, key, sourceDigest)
        baselineSaves.push({ key, cache_id: cacheId })
        await writeLog(logPath, `baseline saved; key=${key}; cache_id=${cacheId}`)
        await writeInventory(evidenceDirectory, `inventory-after-${key}`, await listCaches())
      }
      results.baseline_saves = baselineSaves
      await writeJson(join(evidenceDirectory, "baseline-result.json"), { saves: baselineSaves })

      const baselineRareSet = new Set(baselineRareKeys)
      const settled = await inventoryUntil(
        evidenceDirectory,
        "inventory-baseline-eviction",
        (entries) => !entries.some((entry) => baselineRareSet.has(entry.key)),
      )
      results.baseline_settled = settled
      await writeLog(logPath, "baseline automatic eviction settled; probing rare keys")

      const baselineMisses = []
      for (const key of baselineRareKeys) {
        const restored = await restoreAndDigest(key, ccachePath, sourceDigest)
        baselineMisses.push({
          key,
          restored_key: restored.key ?? null,
          digest: restored.digest ?? null,
        })
        if (restored.key !== undefined) throw new Error(`Expected baseline restore miss for ${key}`)
      }
      results.baseline_restore_probes = baselineMisses
      await writeJson(join(evidenceDirectory, "baseline-restore-probes.json"), baselineMisses)
    }

    const beforePolicyCleanup = await listCaches()
    await writeInventory(
      evidenceDirectory,
      "inventory-before-baseline-cleanup",
      beforePolicyCleanup,
    )
    await deleteEntries(
      beforePolicyCleanup.filter((entry) =>
        entry.key.startsWith(`${runPrefix(runId)}baseline-`) && owned(entry)
      ),
    )
    const afterPolicyCleanup = await inventoryUntil(
      evidenceDirectory,
      "inventory-after-baseline-cleanup",
      (entries) =>
        !entries.some((entry) =>
          entry.key.startsWith(`${runPrefix(runId)}baseline-`) && owned(entry)
        ),
    )
    results.after_baseline_cleanup = afterPolicyCleanup.filter(owned)

    await copyDirectory(goldenPath, ccachePath)
    verifyPayloadDigest(sourceDigest, await digestDirectory(ccachePath))
    const policySaves: Array<{ key: string; cache_id: number }> = []
    for (const key of policyRareKeys) {
      const cacheId = await savePayload(ccachePath, key, sourceDigest)
      policySaves.push({ key, cache_id: cacheId })
      await writeLog(logPath, `policy rare saved; key=${key}; cache_id=${cacheId}`)
    }
    const hotSaves: Array<{ key: string; cache_id: number }> = []
    for (const [index, key] of policyHotKeys.entries()) {
      const cacheId = await savePayload(ccachePath, key, sourceDigest)
      hotSaves.push({ key, cache_id: cacheId })
      const entries = await listCaches()
      const oldHot = hotKeysToPrune({
        entries,
        runId,
        savedHotKeys: hotSaves.map((save) => save.key),
        newestHotKey: key,
      })
      await deleteEntries(oldHot)
      await writeInventory(
        evidenceDirectory,
        `inventory-policy-generation-${index + 1}`,
        await listCaches(),
      )
      await writeLog(
        logPath,
        `policy hot saved and pruned; key=${key}; cache_id=${cacheId}; pruned=${oldHot.length}`,
      )
    }
    results.policy_saves = { rare: policySaves, hot: hotSaves }

    const finalEntries = await inventoryUntil(
      evidenceDirectory,
      "inventory-policy-final",
      (entries) => {
        const ownKeys = entries.filter(owned).map((entry) => entry.key)
        return ownKeys.length === 4 &&
          policyRareKeys.every((key) => ownKeys.includes(key)) &&
          ownKeys.includes(policyHotKeys.at(-1) ?? "")
      },
    )
    const finalOwnKeys = finalEntries.filter(owned).map((entry) => entry.key)
    const expectedFinalKeys = [...policyRareKeys, policyHotKeys.at(-1) as string]
    if (finalOwnKeys.toSorted().join("\n") !== expectedFinalKeys.toSorted().join("\n")) {
      throw new Error(`Unexpected final own cache set: ${JSON.stringify(finalOwnKeys)}`)
    }
    results.final_inventory = finalEntries.filter(owned)

    const policyRestores = []
    for (const key of [...policyRareKeys, policyHotKeys.at(-1) as string]) {
      const restored = await restoreAndDigest(key, ccachePath, sourceDigest)
      if (restored.key !== key) throw new Error(`Policy restore miss for ${key}`)
      policyRestores.push({ key, restored_key: restored.key, digest: restored.digest })
    }
    results.policy_restores = policyRestores
    await writeJson(join(evidenceDirectory, "experiment-result.json"), results)
    await writeLog(logPath, "policy restore verification succeeded; retaining four policy entries")
  } catch (error) {
    const message = error instanceof Error ? error.message : String(error)
    results.failure = message
    await writeJson(join(evidenceDirectory, "failure-result.json"), results)
    await writeLog(logPath, `failure: ${message}`)
    try {
      const beforeCleanup = await listCaches()
      await writeInventory(evidenceDirectory, "inventory-failure-before-cleanup", beforeCleanup)
      await deleteEntries(beforeCleanup.filter((entry) => isOwnedKey(entry.key, runId)))
      const afterCleanup = await listCaches()
      await writeInventory(evidenceDirectory, "inventory-failure-after-cleanup", afterCleanup)
    } catch (cleanupError) {
      const cleanupMessage = cleanupError instanceof Error
        ? cleanupError.message
        : String(cleanupError)
      await writeLog(logPath, `cleanup failure: ${cleanupMessage}`)
    }
    throw error
  }
}

if (import.meta.main) {
  const command = new Command()
    .name("cache-pressure")
    .description("Run the fork-only real GitHub Actions cache quota experiment")
    .option("--run-id <run-id:string>", "Current workflow run ID", { required: true })
    .option(
      "--wait-run-id <wait-run-id:string>",
      "Completed Windows comparison prerequisite run ID",
      {
        required: true,
      },
    )
    .option("--evidence-dir <directory:string>", "Directory for raw inventories and results", {
      default: join(Deno.cwd(), "cache-pressure-evidence"),
    })
    .option("--allow-eviction", "Explicitly authorize automatic eviction in the personal fork")
    .option(
      "--policy-only",
      "Validate native retention separately when natural eviction was unobserved",
    )
    .action(async ({ runId, waitRunId, evidenceDir, allowEviction, policyOnly }) => {
      await runExperiment({
        evidenceDirectory: evidenceDir,
        runId,
        waitRunId,
        allowEviction: allowEviction ?? false,
        policyOnly: policyOnly ?? false,
      })
    })
  await command.parse(Deno.args)
}
