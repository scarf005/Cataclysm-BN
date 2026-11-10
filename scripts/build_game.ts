#!/usr/bin/env -S deno run --allow-read --allow-run --allow-env

/**
 * @module
 * Build the game for the host OS with its CMake preset: just build [--mode tiles|curses] [--all].
 * Configures only when the preset's build directory has no CMake cache, so an existing build keeps its settings.
 */
import { Command, EnumType } from "@cliffy/command"
import { exists } from "@std/fs"
import { fromFileUrl, join } from "@std/path"

export type Mode = "tiles" | "curses"
export type BuildPlan = { configurePreset: string; buildPreset: string; targets: string[] }

const gameTarget = { tiles: "cataclysm-bn-tiles", curses: "cataclysm-bn" } as const

/** Picks the presets for @p os and @p arch; `all` builds every target of the build preset. */
export const planBuild = (
  { os, arch, mode, all }: { os: string; arch: string; mode: Mode; all: boolean },
): BuildPlan => {
  const targets = all ? [] : [gameTarget[mode]]
  if (os === "linux") {
    const preset = mode === "tiles" ? "linux-full" : "linux-curses"
    return { configurePreset: preset, buildPreset: preset, targets }
  }
  if (os === "darwin" && arch === "aarch64" && mode === "tiles") {
    return { configurePreset: "osx-arm-slim", buildPreset: "osx-arm-slim", targets }
  }
  if (os === "windows" && mode === "tiles") {
    return {
      configurePreset: "windows-tiles-sounds-x64-msvc",
      buildPreset: "windows-msvc-relwithdebinfo",
      targets,
    }
  }
  throw new Error(`No ${mode} preset for ${os} ${arch}.`)
}

const root = fromFileUrl(new URL("..", import.meta.url))

const cmake = async (args: string[]) => {
  console.log(`cmake ${args.join(" ")}`)
  const { success, code } = await new Deno.Command("cmake", { args, cwd: root }).spawn().status
  if (!success) throw new Error(`cmake failed (exit ${code})`)
}

export const build = async ({ mode, all }: { mode: Mode; all: boolean }) => {
  const plan = planBuild({ os: Deno.build.os, arch: Deno.build.arch, mode, all })
  if (!await exists(join(root, "out", "build", plan.configurePreset, "CMakeCache.txt"))) {
    await cmake(["--preset", plan.configurePreset])
  }
  await cmake([
    "--build",
    "--preset",
    plan.buildPreset,
    ...plan.targets.flatMap((t) => ["--target", t]),
  ])
}

export const createBuildCommand = (run = build) =>
  new Command()
    .name("just build")
    .description("Build the game for this OS with its CMake preset.")
    .type("mode", new EnumType(["tiles", "curses"]))
    .option("--mode <mode:mode>", "Frontend to build.", { default: "tiles" as const })
    .option("--all", "Build every target of the preset, tests included.", { default: false })
    .action(async ({ mode, all }) => await run({ mode, all }))

if (import.meta.main) {
  try {
    await createBuildCommand().parse(Deno.args)
  } catch (error) {
    console.error(error instanceof Error ? error.message : String(error))
    Deno.exit(1)
  }
}
