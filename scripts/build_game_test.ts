import { assertEquals, assertThrows } from "@std/assert"
import { createBuildCommand, type Mode, planBuild } from "./build_game.ts"

Deno.test("planBuild picks the linux tiles preset and game target", () => {
  assertEquals(planBuild({ os: "linux", arch: "x86_64", mode: "tiles", all: false }), {
    configurePreset: "linux-full",
    buildPreset: "linux-full",
    targets: ["cataclysm-bn-tiles"],
  })
})

Deno.test("planBuild builds the curses game with the linux curses preset", () => {
  assertEquals(planBuild({ os: "linux", arch: "x86_64", mode: "curses", all: false }).targets, [
    "cataclysm-bn",
  ])
  assertEquals(
    planBuild({ os: "linux", arch: "x86_64", mode: "curses", all: false }).buildPreset,
    "linux-curses",
  )
})

Deno.test("planBuild with all leaves targets to the build preset", () => {
  assertEquals(planBuild({ os: "windows", arch: "x86_64", mode: "tiles", all: true }), {
    configurePreset: "windows-tiles-sounds-x64-msvc",
    buildPreset: "windows-msvc-relwithdebinfo",
    targets: [],
  })
})

Deno.test("planBuild rejects platforms without a preset", () => {
  assertThrows(() => planBuild({ os: "darwin", arch: "x86_64", mode: "tiles", all: false }))
  assertThrows(() => planBuild({ os: "windows", arch: "x86_64", mode: "curses", all: false }))
})

Deno.test("build command parses mode and all flags", async () => {
  const calls: { mode: Mode; all: boolean }[] = []
  const command = createBuildCommand((options) => {
    calls.push(options)
    return Promise.resolve()
  })
  await command.parse([])
  await command.parse(["--mode", "curses", "--all"])
  assertEquals(calls, [{ mode: "tiles", all: false }, { mode: "curses", all: true }])
})
