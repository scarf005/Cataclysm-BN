#!/usr/bin/env -S deno run -A
/**
 * @module
 * Measures waiting and walking in two prepared game directories (base and head), interleaved over
 * several rounds, and writes medians and the paired change per scenario.
 *
 * Usage: compare.ts --base DIR --head DIR --tracy DIR --out DIR [--display :99] [--rounds 5]
 *   [--steps 150] [--hours 3] [--compute gpu_software]
 */
import { Command } from "@cliffy/command"
import { join } from "@std/path"
import { type Paired, paired } from "./analyze.ts"
import { runGame, type RunResult } from "./game.ts"

type Ref = "base" | "head"

const { options: args } = await new Command()
  .option("--base <dir:string>", "game directory of the base ref", { required: true })
  .option("--head <dir:string>", "game directory of the head ref", { required: true })
  .option("--tracy <dir:string>", "directory with tracy-capture and tracy-csvexport", {
    required: true,
  })
  .option("--out <dir:string>", "output directory", { required: true })
  .option("--display <display:string>", "X display to use", { default: ":99" })
  .option("--rounds <n:integer>", "interleaved rounds", { default: 5 })
  .option("--steps <n:integer>", "walk steps per run", { default: 150 })
  .option("--hours <n:integer>", "waited hours per run", { default: 3 })
  .option("--compute <mode:string>", "COMPUTE_ACCELERATION option", { default: "gpu_software" })
  .parse(Deno.args)
const { rounds, steps, hours } = args
const dirs: Record<Ref, string> = { base: args.base, head: args.head }
const results: Record<Ref, RunResult[]> = { base: [], head: [] }

for (let round = 0; round < rounds; round++) {
  // Alternate the order so slow drift of the runner does not favour one ref.
  const order: Ref[] = round % 2 === 0 ? ["base", "head"] : ["head", "base"]
  for (const ref of order) {
    console.log(`round ${round + 1}/${rounds}: ${ref}`)
    const result = await runGame({
      gameDir: dirs[ref],
      outDir: join(args.out, `round${round + 1}-${ref}`),
      display: args.display,
      tracyDir: args.tracy,
      port: 8086 + round * 2 + (ref === "head" ? 1 : 0),
      steps,
      hours,
      compute: args.compute,
    })
    console.log(JSON.stringify(result.walk), JSON.stringify({ hourMs: result.wait.hourMs }))
    results[ref].push(result)
  }
}

const column = (ref: Ref, pick: (r: RunResult) => number) => results[ref].map(pick)
const scenarios: [string, (r: RunResult) => number][] = [
  [`wait 1 h, game time (ms per hour, mean of ${hours} hours)`, (r) => r.wait.hourMs],
  [
    `wait 1 h, wall (ms per hour, from the key to idle)`,
    (r) => r.wait.wallMs.reduce((a, b) => a + b, 0) / r.wait.wallMs.length,
  ],
  [`walk key to ready (ms per step, mean of ${steps} steps)`, (r) => r.walk.stepMeanMs],
  [`walk key to ready (ms per step, median of ${steps} steps)`, (r) => r.walk.stepMedianMs],
  [`walk game_draw (ms per step)`, (r) => r.walk.drawMsPerStep],
]
const rows = scenarios.map(([name, pick]) =>
  [name, paired(column("base", pick), column("head", pick))] as [string, Paired]
)
const f = (x: number) => x.toFixed(1)
const table = [
  "| scenario | base median | head median | paired change | head faster in rounds |",
  "|---|---:|---:|---:|---:|",
  ...rows.map(([n, p]) =>
    `| ${n} | ${f(p.baseMedian)} | ${f(p.headMedian)} | ${f(p.changePct)}% | ${p.faster} |`
  ),
].join("\n")
const perRound = [
  "| round | ref | wait ms/h | walk ms/step | draw ms/step |",
  "|---:|---|---:|---:|---:|",
  ...(["base", "head"] as Ref[]).flatMap((ref) =>
    results[ref].map((r, i) =>
      `| ${i + 1} | ${ref} | ${f(r.wait.hourMs)} | ${f(r.walk.stepMeanMs)} | ${
        f(r.walk.drawMsPerStep)
      } |`
    )
  ),
].join("\n")
await Deno.writeTextFile(join(args.out, "summary.md"), `${table}\n\n${perRound}\n`)
await Deno.writeTextFile(
  join(args.out, "results.json"),
  JSON.stringify({ results, rows }, null, 2),
)
console.log(table)
