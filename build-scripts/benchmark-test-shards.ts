#!/usr/bin/env -S deno run --allow-read --allow-write --allow-run --allow-env
/**
 * @module
 * Compare static and measured test shard assignment on the same executable, seed, and runner.
 * Both passes collect XML timings so reporter overhead is matched. Results are single-job samples.
 */
import { Command } from "@cliffy/command"
import { assertEquals, assertThrows } from "@std/assert"
import { ensureDir } from "@std/fs"
import { join } from "@std/path"
import * as v from "@valibot/valibot"

const profileSchema = v.object({
  version: v.literal(1),
  tests: v.record(v.string(), v.pipe(v.number(), v.finite(), v.minValue(0))),
})

export const verifyCoverage = (before: unknown, after: unknown): number => {
  const baseline = Object.keys(v.parse(profileSchema, before).tests).toSorted()
  const candidate = Object.keys(v.parse(profileSchema, after).tests).toSorted()
  if (baseline.length === 0 || JSON.stringify(baseline) !== JSON.stringify(candidate)) {
    throw new Error("Baseline and candidate must run the same nonempty set of test cases")
  }
  return baseline.length
}

const benchmark = async (testBin: string, outputDir: string): Promise<void> => {
  await ensureDir(outputDir)
  const results: { variant: string; seconds: number; exitCode: number; startedAt: string }[] = []
  const baselinePath = join(outputDir, "baseline-profile.json")
  const candidatePath = join(outputDir, "candidate-profile.json")
  for (const variant of ["baseline", "candidate"]) {
    const profilePath = variant === "baseline" ? baselinePath : candidatePath
    const args = [
      "run",
      "--allow-read",
      "--allow-write",
      "--allow-run",
      "--allow-env",
      "build-scripts/run-linux-test-shards.ts",
      "--jobs",
      "4",
      "--non-slow-shards",
      "8",
      "--write-timings",
      profilePath,
      ...(variant === "candidate" ? ["--timings", baselinePath] : []),
      testBin,
      "--",
      "--use-colour",
      "no",
      "--rng-seed",
      "1",
      "--error-format=github-action",
      ...(Deno.build.os === "windows" ? [] : ["--gpu-backend=software"]),
    ]
    const startedAt = new Date().toISOString()
    const start = performance.now()
    const result = await new Deno.Command(Deno.execPath(), {
      args,
      env: { CATA_TEST_USER_DIR_PREFIX: join(outputDir, `${variant}-user`) },
      stdout: "piped",
      stderr: "piped",
    }).output()
    const seconds = (performance.now() - start) / 1000
    await Deno.writeFile(join(outputDir, `${variant}.stdout.log`), result.stdout)
    await Deno.writeFile(join(outputDir, `${variant}.stderr.log`), result.stderr)
    results.push({ variant, seconds, exitCode: result.code, startedAt })
    await Deno.writeTextFile(join(outputDir, "results.json"), JSON.stringify({ results }, null, 2))
    console.log(`${variant}: ${seconds.toFixed(3)} seconds, exit ${result.code}`)
    if (!result.success) {
      throw new Error(`${variant} failed; inspect ${outputDir} logs before comparing performance`)
    }
  }
  const cases = verifyCoverage(
    JSON.parse(await Deno.readTextFile(baselinePath)),
    JSON.parse(await Deno.readTextFile(candidatePath)),
  )
  const [before, after] = results
  const report = {
    methodology:
      "One sequential baseline/candidate pair, same executable, seed 1, 4 jobs, 8 CPU shards, XML reporter on both passes. Includes discovery and profile collection. No native rebuild between passes.",
    results,
    cases,
    reductionPercent: 100 * (before.seconds - after.seconds) / before.seconds,
    speedup: before.seconds / after.seconds,
  }
  await Deno.writeTextFile(join(outputDir, "results.json"), JSON.stringify(report, null, 2))
  console.log(JSON.stringify(report, null, 2))
}

Deno.test("coverage comparison ignores ordering and timing values", () => {
  assertEquals(
    verifyCoverage(
      { version: 1, tests: { a: 1, b: 2 } },
      { version: 1, tests: { b: 8, a: 0 } },
    ),
    2,
  )
})
Deno.test("coverage comparison rejects dropped, added, and empty case sets", () => {
  for (const tests of [{ b: 1 }, { a: 1, b: 2 }, {}]) {
    assertThrows(() => verifyCoverage({ version: 1, tests: { a: 1 } }, { version: 1, tests }))
  }
})

if (import.meta.main) {
  await new Command()
    .name("benchmark-test-shards")
    .description("Compare static and measured shards on one test executable")
    .option("--test-bin <path:string>", "Test executable", { required: true })
    .option("--output-dir <path:string>", "Evidence directory", { default: "out/shard-benchmark" })
    .action(async ({ testBin, outputDir }) => await benchmark(testBin, outputDir))
    .parse(Deno.args)
}
