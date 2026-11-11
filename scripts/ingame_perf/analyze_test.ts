import { assertAlmostEquals, assertEquals } from "@std/assert"
import { inputBlockZone, paired, parseZones, stepTimesMs, waitHours, type Zone } from "./analyze.ts"

const z = (name: string, startMs: number, durMs: number): Zone => ({
  name,
  start: startMs * 1e6,
  end: (startMs + durMs) * 1e6,
})

Deno.test("parseZones() reads csvexport rows and keeps commas in names", () => {
  const csv = "name,src_file,src_line,ns_since_start,exec_time_ns,thread,value\n" +
    "a,/src/a.cpp,1,100,50,7,\n" + "b,c,/src/a.cpp,2,200,10,7,\n"
  assertEquals(parseZones(csv), [
    { name: "a", start: 100, end: 150 },
    { name: "b,c", start: 200, end: 210 },
  ])
})

Deno.test("stepTimesMs() measures key arrival to next block over the last steps only", () => {
  const blocks = [
    z(inputBlockZone, 0, 900),
    z(inputBlockZone, 910, 90),
    z(inputBlockZone, 1010, 90),
  ]
  assertEquals(stepTimesMs(blocks, 1), [10])
  assertEquals(stepTimesMs(blocks, 2), [10, 10])
  assertEquals(stepTimesMs(blocks.slice(0, 1), 1), [])
})

Deno.test("waitHours() spans first turn after the key to the next block and counts inside zones", () => {
  const blocks = [
    z(inputBlockZone, 0, 100),
    z(inputBlockZone, 1100, 50),
    z(inputBlockZone, 2200, 10),
  ]
  const turns = [
    z("game::do_turn", 90, 20),
    z("game::do_turn", 130, 5),
    z("game::do_turn", 1160, 5),
  ]
  const draws = [z("game_draw", 140, 4), z("game_draw", 1170, 6), z("game_draw", 1180, 2)]
  const [first, second] = waitHours(blocks, turns, draws, 2)
  assertEquals([first.ms, first.turns, first.draws, first.drawMs], [970, 1, 1, 4])
  assertEquals([second.ms, second.turns, second.draws, second.drawMs], [1040, 1, 2, 8])
})

Deno.test("paired() reports the median per-round change and the rounds that got faster", () => {
  const r = paired([10, 10, 10], [5, 12, 4])
  assertEquals(r.baseMedian, 10)
  assertEquals(r.headMedian, 5)
  assertAlmostEquals(r.changePct, -50)
  assertEquals(r.faster, "2/3")
})
