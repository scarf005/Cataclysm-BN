/**
 * https://github.com/cataclysmbnteam/Cataclysm-BN/pull/2911
 */
import { difference } from "https://deno.land/x/set_operations@v1.1.1/mod.ts"
import rawExistingFlags from "https://raw.githubusercontent.com/cataclysmbnteam/Cataclysm-BN/dc43e2353558aa36352b76e0b4b3d577915d6272/data/json/flags_mutation.json" with {
  type: "json",
}
import { queryCli } from "https://deno.land/x/catjazz@v0.0.1/utils/query.ts"
import { z } from "https://deno.land/x/catjazz@v0.0.1/deps/zod.ts"

const existingFlags = new Set(rawExistingFlags.flatMap((x) => x.id))

const flags = z
  .object({
    type: z.literal("mutation"),
    flags: z.array(z.string()),
  })
  .transform((x) => x.flags)

const mutationFlag = {
  type: "mutation_flag",
  "//": "This trait flag is used by cosmetic trait JSON, with no hardcode usage at present.",
  "//generated-by": "https://deno.land/x/catjazz",
}

const main = queryCli({
  desc: "Find all query mutation flags",
  schema: flags,
  map: (xss) => {
    const allFlags = new Set(xss.flat())
    const toAdd = difference(allFlags, existingFlags)
    const result = [...toAdd].map((x) => ({ id: x, ...mutationFlag }))

    return result
  },
})

if (import.meta.main) {
  await main().parse(Deno.args)
}
