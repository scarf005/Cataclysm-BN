#!/usr/bin/env -S deno run -RW --allow-env

import { bgBrightYellow } from "@std/fmt/colors"
import { Command } from "@cliffy/command"
import {
  asUndefined,
  looseObjectWithout,
  mapMany,
  recursivelyReadJSON,
  writeMany,
} from "./utils.ts"
import * as v from "@valibot/valibot"

export const ArrayWithoutFilthy = v.pipe(
  v.array(v.string()),
  v.transform((xs) => {
    const ys = xs.filter((x) => x !== "FILTHY")
    return ys.length === 0 ? undefined : ys
  }),
)

export const UseActionFilter = v.union([
  v.array(v.unknown()),
  asUndefined(v.picklist(["WASH_SOFT_ITEMS", "WASH_HARD_ITEMS"])),
  looseObjectWithout({}, ["filthy_volume_threshold"]),
])

export const ItemWithoutFilthy = looseObjectWithout({
  use_action: v.optional(UseActionFilter),
  flags: v.optional(ArrayWithoutFilthy),
}, ["squeamish_penalty"])

if (import.meta.main) {
  const { args: [path] } = await new Command()
    .description(`removes ${bgBrightYellow("FILTHY")} flags from items`)
    .arguments("<path:string>")
    .parse(Deno.args)

  const paths = await recursivelyReadJSON(path)
  console.log(paths)
  const mapped = mapMany(ItemWithoutFilthy, paths)
  writeMany(mapped)
}
export const foo = {
  "id": "teleporter_station_deployed",
  "type": "furniture",
  "name": "Teleporter station (deployed)",
  "move_cost_mod": 4,
  "coverage": 0,
  "required_str": 100,
  "description": "A teleporter station, can be used to teleport.",
  "symbol": "O",
  "color": "yellow",
  "flags": ["EASY_DECONSTRUCT"],
  "looks_like": "f_MRI",
}
