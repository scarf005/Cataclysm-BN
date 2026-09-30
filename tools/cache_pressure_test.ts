import { assertEquals, assertRejects, assertThrows } from "@std/assert"
import {
  baselineKey,
  digestDirectory,
  hotKeysToPrune,
  isOwnedKey,
  policyHotKey,
  policyRareKey,
  runPrefix,
  selectSourceCache,
  SOURCE_CACHE_PREFIX,
  SOURCE_CACHE_VERSION,
  verifyPayloadDigest,
} from "./cache_pressure.ts"

const cache = (key: string, createdAt = "2025-01-01T00:00:00Z") => ({
  id: Math.abs(key.length),
  key,
  version: SOURCE_CACHE_VERSION,
  ref: "refs/heads/ci/build-speed-experiments",
  size_in_bytes: 923057236,
  created_at: createdAt,
  last_accessed_at: createdAt,
})

Deno.test("cache pressure keys are scoped to the current run", () => {
  const runId = "123"
  assertEquals(runPrefix(runId), "bn-speed-pressure-123-")
  assertEquals(isOwnedKey(baselineKey(runId, "rare-a"), runId), true)
  assertEquals(isOwnedKey("bn-speed-pressure-124-baseline-rare-a", runId), false)
  assertEquals(isOwnedKey("bn-speed-pressure-1234-baseline-rare-a", runId), false)
  assertEquals(policyRareKey(runId, "rare-b"), "bn-speed-pressure-123-policy-rare-rare-b")
})

Deno.test("source selection matches timestamped keys by archive version and branch", () => {
  const old = `${SOURCE_CACHE_PREFIX}2026-09-29T00:00:00Z`
  const latest = `${SOURCE_CACHE_PREFIX}2026-09-30T12:26:20.358Z`
  const selected = selectSourceCache([
    cache("unrelated"),
    cache(old),
    cache(latest, "2025-01-02T00:00:00Z"),
    { ...cache(`${SOURCE_CACHE_PREFIX}wrong-version`), version: "wrong" },
    { ...cache(`${SOURCE_CACHE_PREFIX}wrong-branch`), ref: "refs/heads/main" },
  ])
  assertEquals(selected, { key: latest, size_in_bytes: 923057236 })
  assertEquals(selectSourceCache([{ ...cache(latest), version: "wrong" }]), undefined)
})

Deno.test("hot pruning only returns older saved keys from this run", () => {
  const runId = "123"
  const newest = policyHotKey(runId, 3)
  const entries = [
    cache(policyHotKey(runId, 1)),
    cache(policyHotKey(runId, 2)),
    cache(newest),
    cache(policyHotKey("124", 1)),
    cache("unrelated"),
  ]
  assertEquals(
    hotKeysToPrune({
      entries,
      runId,
      savedHotKeys: [policyHotKey(runId, 1), policyHotKey(runId, 2), newest],
      newestHotKey: newest,
    })
      .map(
        (entry) => entry.key,
      ),
    [policyHotKey(runId, 1), policyHotKey(runId, 2)],
  )
})

Deno.test("payload digest covers all files and rejects altered payloads", async () => {
  const root = await Deno.makeTempDir()
  try {
    await Deno.mkdir(`${root}/nested`)
    await Deno.writeTextFile(`${root}/a`, "alpha")
    await Deno.writeTextFile(`${root}/nested/b`, "beta")
    const expected = await digestDirectory(root)
    assertEquals(expected.bytes, 9)
    assertEquals(expected.files, 2)
    verifyPayloadDigest(expected, await digestDirectory(root))
    await Deno.writeTextFile(`${root}/nested/b`, "changed")
    await assertRejects(
      async () => verifyPayloadDigest(expected, await digestDirectory(root)),
      Error,
      "Payload digest mismatch",
    )
  } finally {
    await Deno.remove(root, { recursive: true })
  }
})

Deno.test("payload digest requires a directory", async () => {
  const root = await Deno.makeTempFile()
  try {
    await assertRejects(() => digestDirectory(root), Error, "not a directory")
  } finally {
    await Deno.remove(root)
  }
})

Deno.test("payload digest verifier rejects different metadata", () => {
  assertThrows(
    () =>
      verifyPayloadDigest(
        { digest: "a", bytes: 1, files: 1 },
        { digest: "a", bytes: 2, files: 1 },
      ),
    Error,
    "Payload digest mismatch",
  )
})
