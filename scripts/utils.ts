import * as v from "@valibot/valibot"

import { walk, WalkEntry } from "@std/fs"
import type { BaseIssue, BaseSchema, InferOutput } from "@valibot/valibot"
import { createWalkEntry } from "https://jsr.io/@std/fs/1.0.6/_create_walk_entry.ts"
import { deepMerge, omit } from "@std/collections"
import orderedJSON from "npm:json-order"

/**
 * create a parser from given schema that parses an array of objects
 */
export const parseMany = <const TSchema extends BaseSchema<unknown, unknown, BaseIssue<unknown>>>(
  schema: TSchema,
) => {
  const parser = v.safeParser(schema)
  return (xs: unknown[]): InferOutput<TSchema>[] =>
    xs
      .map(parser)
      .filter((x) => x.success)
      .map((x) => x.output)
}

export const mapMany = <const TSchema extends BaseSchema<unknown, unknown, BaseIssue<unknown>>>(
  schema: TSchema,
  xs: JSONFileEntry[],
) => {
  const parser = v.safeParser(schema)
  return xs.map(({ data, ...rest }) => {
    try {
      const result = data.map((obj) => {
        const parsed = parser(obj)
        if (!parsed.success) return obj

        return deepMerge(obj as object, parsed.output as object, {
          arrays: "replace",
          maps: "replace",
          sets: "replace",
        })
      })
      return { ...rest, data: result }
    } catch (e) {
      console.log(rest, data)
      throw e
    }
  })
}

export const writeMany = (xs: JSONFileEntry[]) =>
  Promise.all(
    xs.map((x) =>
      Deno.writeTextFile(x.path, orderedJSON.default.stringify(x.data, x.map, undefined, 2))
    ),
  )

export const looseObjectWithout = <const TEntries extends v.ObjectEntries>(
  entries: TEntries,
  keys: string[],
) =>
  v.pipe(
    v.looseObject(entries),
    v.transform((x) => omit(x, keys)),
  )

export const asUndefined = <const TSchema extends BaseSchema<unknown, unknown, BaseIssue<unknown>>>(
  schema: TSchema,
) => v.pipe(schema, v.transform(() => undefined))

export interface JSONFileEntry extends WalkEntry {
  data: object[]
  map: Record<string, string[]>
}

/**
 * @param rootPath a path to recursively read JSON files from
 * @returns an array of {@link WalkEntry} with unverified JSON object array
 */
export const recursivelyReadJSON = async (rootPath: string): Promise<JSONFileEntry[]> => {
  const isFile = (await Deno.lstat(rootPath)).isFile

  const jsons = isFile
    ? [await createWalkEntry(rootPath)]
    : await Array.fromAsync(walk(rootPath, { exts: [".json"], skip: [/(modinfo|default)\.json/] }))

  const res = jsons.map(async (entry) => {
    const result = orderedJSON.default.parse(await Deno.readTextFile(entry.path))
    return ({
      ...entry,
      data: result.object as object[],
      map: result.map,
    })
  })

  return (await Promise.all(res))
}
