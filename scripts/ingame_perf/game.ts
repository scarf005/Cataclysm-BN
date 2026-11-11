/**
 * @module
 * Drives one real `cataclysm-bn-tiles` process on an X display with xdotool and records Tracy
 * captures while it walks and waits.
 */
import { join } from "@std/path"
import {
  byName,
  type HourSpan,
  inputBlockZone,
  mean,
  median,
  parseZones,
  stepTimesMs,
  waitHours,
} from "./analyze.ts"

export type RunOptions = {
  /** Directory holding `cataclysm-bn-tiles` and the data it needs; also the game's cwd. */
  gameDir: string
  /** Output directory for logs, screenshots, captures and `result.json`. */
  outDir: string
  display: string
  tracyDir: string
  port: number
  steps: number
  hours: number
  compute: string
}

export type RunResult = {
  walk: { stepMeanMs: number; stepMedianMs: number; drawMsPerStep: number; steps: number }
  wait: { hours: HourSpan[]; hourMs: number; wallMs: number[] }
}

const sleep = (ms: number) => new Promise((resolve) => setTimeout(resolve, ms))

const run = async (cmd: string, args: string[], env: Record<string, string> = {}) => {
  const out = await new Deno.Command(cmd, { args, env, stdout: "piped", stderr: "piped" }).output()
  if (!out.success) {
    throw new Error(`${cmd} ${args.join(" ")}: ${new TextDecoder().decode(out.stderr)}`)
  }
  return new TextDecoder().decode(out.stdout)
}

/// CPU ticks (10 ms) of the game's threads and their count. Tracy's own threads are left out: they
/// keep running while a capture is connected and would hide the game being idle.
const cpuTicks = async (pid: number) => {
  const stats = await Promise.all(
    (await Array.fromAsync(Deno.readDir(`/proc/${pid}/task`))).map((t) =>
      Deno.readTextFile(`/proc/${pid}/task/${t.name}/stat`).catch(() => "")
    ),
  )
  const threads = stats.filter((stat) => stat !== "" && !stat.includes("(Tracy"))
  const ticks = threads.map((stat) => stat.slice(stat.lastIndexOf(")") + 2).split(" "))
    .reduce((sum, f) => sum + Number(f[11]) + Number(f[12]), 0)
  return { ticks, threads: threads.length }
}

/// Starts `cmd` with stdout and stderr appended to `log`; `exec` keeps the child's pid.
const spawnLogged = (cmd: string, args: string[], opts: Deno.CommandOptions, log: string) =>
  new Deno.Command("sh", {
    ...opts,
    args: ["-c", 'exec "$0" "$@" >>"$LOG" 2>&1', cmd, ...args],
    env: { ...opts.env, LOG: log },
  }).spawn()

/// The game is idle (blocked on input) once its threads used at most one clock tick (10 ms) and
/// started no thread in each of three consecutive 100 ms samples. Resolves with the time of the
/// last busy sample, so the caller gets the end of the work, not the end of the confirmation.
const waitIdle = async (
  pid: number,
  what: string,
  opts: { minMs?: number; timeoutMs?: number } = {},
) => {
  const { minMs = 0, timeoutMs = 600_000 } = opts
  const start = Date.now()
  let last = await cpuTicks(pid)
  let quiet = 0
  let since = start
  while (true) {
    await sleep(100)
    const now = await cpuTicks(pid)
    if (now.ticks - last.ticks > 1 || now.threads !== last.threads) {
      quiet = 0
      since = Date.now()
    } else {
      quiet += 1
    }
    last = now
    if (quiet >= 3 && Date.now() - start >= minMs) return since
    if (Date.now() - start > timeoutMs) throw new Error(`timed out waiting for idle: ${what}`)
  }
}

export const runGame = async (o: RunOptions): Promise<RunResult> => {
  await Deno.mkdir(o.outDir, { recursive: true })
  const userDir = join(o.outDir, "ud")
  const configDir = join(userDir, "config")
  await Deno.mkdir(configDir, { recursive: true })
  const options = [
    ["ANIMATIONS", "false"],
    ["AUTOSAVE", "false"],
    ["FULLSCREEN", "windowedbl"],
    ["COMPUTE_ACCELERATION", o.compute],
  ].map(([name, value]) => ({ name, value }))
  await Deno.writeTextFile(join(configDir, "options.json"), JSON.stringify(options))

  const env = { DISPLAY: o.display }
  const key = (...keys: string[]) => run("xdotool", ["key", "--delay", "250", ...keys], env)
  const shot = (name: string) => run("import", ["-window", "root", join(o.outDir, name)], env)

  const game = spawnLogged(
    join(o.gameDir, "cataclysm-bn-tiles"),
    ["--seed", "ingame-perf", "--userdir", `${userDir}/`, "--configdir", `${configDir}/`],
    {
      cwd: o.gameDir,
      env: {
        ...env,
        LANG: "en_US.UTF-8",
        SDL_AUDIODRIVER: "dummy",
        SDL_VIDEO_DRIVER: "x11",
        TRACY_PORT: String(o.port),
      },
    },
    join(o.outDir, "game.log"),
  )
  const pid = game.pid
  let capture: Deno.ChildProcess | undefined

  const startCapture = async (name: string) => {
    capture = spawnLogged(
      join(o.tracyDir, "tracy-capture"),
      ["-a", "127.0.0.1", "-p", String(o.port), "-o", join(o.outDir, `${name}.tracy`), "-f"],
      {},
      join(o.outDir, `${name}.capture.log`),
    )
    // The first turn after connecting gives complete blocking zones to anchor on.
    await sleep(3000)
    await key("period")
    await waitIdle(pid, "turn after Tracy connected", { minMs: 500 })
  }

  const stopCapture = async (name: string) => {
    capture?.kill("SIGINT")
    await capture?.status
    const trace = join(o.outDir, `${name}.tracy`)
    await Deno.writeTextFile(
      join(o.outDir, `${name}.zones.csv`),
      await run(join(o.tracyDir, "tracy-csvexport"), [trace]),
    )
    const zones = (filter: string) =>
      run(join(o.tracyDir, "tracy-csvexport"), ["-u", "-f", filter, trace]).then(parseZones)
    const [blocks, turns, draws] = await Promise.all([
      zones(inputBlockZone),
      zones("game::do_turn"),
      zones("game_draw"),
    ])
    return {
      blocks: byName(blocks, inputBlockZone),
      turns: byName(turns, "game::do_turn"),
      draws: byName(draws, "game_draw"),
    }
  }

  try {
    // Main menu, then the default New Game entry "Play Now! (Default Scenario)".
    await waitIdle(pid, "main menu", { minMs: 10_000 })
    await key("Down", "Down", "Return")
    await waitIdle(pid, "world generation", { minMs: 15_000 })
    // Dismiss intro popups and pass a few turns so the action prompt is reached.
    for (let i = 0; i < 12; i++) {
      await key("period")
      await waitIdle(pid, "intro turn", { minMs: 300 })
    }
    await shot("start.png")

    // Walk: alternate east/west inside the start submap, one key per step, each only after the
    // previous step finished so that the game never skips a redraw for queued input.
    await startCapture("walk")
    for (let i = 0; i < o.steps; i++) {
      await run("xdotool", ["key", i % 2 === 0 ? "l" : "h"], env)
      await sleep(100)
      await waitIdle(pid, `walk step ${i}`)
    }
    await key("period")
    await waitIdle(pid, "turn after walk", { minMs: 500 })
    await shot("after-walk.png")
    const walkZones = await stopCapture("walk")
    const stepMs = stepTimesMs(walkZones.blocks, o.steps)
    if (stepMs.length !== o.steps) {
      const seen = `${walkZones.blocks.length} blocking, ${walkZones.turns.length} do_turn zones`
      throw new Error(`walk: ${stepMs.length} of ${o.steps} steps in walk.tracy (${seen})`)
    }
    const walkStart = walkZones.blocks.at(-(o.steps + 1))?.end ?? 0
    const walkEnd = walkZones.blocks.at(-1)?.start ?? 0
    const walkDraws = walkZones.draws.filter((z) => z.start >= walkStart && z.start < walkEnd)

    // Wait: wait menu `|`, `w` Wait a while, `3` for one hour.
    await startCapture("wait")
    const wallMs: number[] = []
    for (let h = 0; h < o.hours; h++) {
      await key("bar")
      await waitIdle(pid, "wait menu", { minMs: 300 })
      await key("w")
      await waitIdle(pid, "wait duration menu", { minMs: 300 })
      const t0 = Date.now()
      await run("xdotool", ["key", "3"], env)
      await sleep(500)
      const idleAt = await waitIdle(pid, `wait hour ${h}`)
      wallMs.push(idleAt - t0)
      await shot(`after-wait-${h}.png`)
    }
    await key("period")
    await waitIdle(pid, "turn after wait", { minMs: 500 })
    const waitZones = await stopCapture("wait")
    const hours = waitHours(waitZones.blocks, waitZones.turns, waitZones.draws, o.hours)
    if (hours.length !== o.hours) {
      const seen = `${waitZones.blocks.length} blocking, ${waitZones.turns.length} do_turn zones`
      throw new Error(`wait: ${hours.length} of ${o.hours} hours in wait.tracy (${seen})`)
    }

    const result: RunResult = {
      walk: {
        stepMeanMs: mean(stepMs),
        stepMedianMs: median(stepMs),
        drawMsPerStep: walkDraws.reduce((s, z) => s + z.end - z.start, 0) / 1e6 / o.steps,
        steps: stepMs.length,
      },
      wait: { hours, hourMs: mean(hours.map((h) => h.ms)), wallMs },
    }
    await Deno.writeTextFile(join(o.outDir, "result.json"), JSON.stringify(result, null, 2))
    return result
  } catch (error) {
    const log = await Deno.readTextFile(join(o.outDir, "game.log")).catch(() => "")
    console.error(`game.log tail:\n${log.split("\n").slice(-20).join("\n")}`)
    throw error
  } finally {
    for (const child of [capture, game]) {
      try {
        child?.kill("SIGKILL")
      } catch {
        // already exited
      }
    }
    await game.status
  }
}
