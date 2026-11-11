/**
 * @module
 * Turns Tracy `csvexport -u` zone lists of an in-game run into per-step and per-hour timings, and
 * summarizes paired base/head runs.
 */

/** One zone instance; times are nanoseconds on the Tracy clock. */
export type Zone = { name: string; start: number; end: number }

/// Zone that spans the main prompt blocking on input. It ends when the key arrives and the next one
/// starts once the game is ready for input again.
export const inputBlockZone = "get_player_input_noanim_blocking_handle_mouseview"

/// Rows are `name,src_file,src_line,ns_since_start,exec_time_ns,thread` plus a trailing `value`
/// column in newer Tracy versions, so the columns are counted from the header.
export const parseZones = (csv: string): Zone[] => {
  const [header, ...rows] = csv.split("\n")
  const tail = header.split(",").length - 1
  return rows.filter((line) => line !== "").map((line) => {
    const c = line.split(",")
    const fields = c.slice(c.length - tail)
    const start = Number(fields[2])
    return { name: c.slice(0, c.length - tail).join(","), start, end: start + Number(fields[3]) }
  })
}

const ms = (ns: number) => ns / 1e6

export const byName = (zones: Zone[], name: string): Zone[] =>
  zones.filter((z) => z.name === name).toSorted((a, b) => a.start - b.start)

/// Time from a key arriving to the prompt blocking again, for each of the last `steps` steps.
export const stepTimesMs = (blocks: Zone[], steps: number): number[] => {
  const used = blocks.slice(-(steps + 1))
  return used.slice(1).map((b, i) => ms(b.start - used[i].end))
}

export type HourSpan = { ms: number; turns: number; draws: number; drawMs: number }

/// Each hour runs from the first turn after the wait key to the prompt blocking again. Needs
/// `hours + 1` complete blocking zones: one per wait key plus one that ends with the final key.
export const waitHours = (
  blocks: Zone[],
  turns: Zone[],
  draws: Zone[],
  hours: number,
): HourSpan[] => {
  const used = blocks.slice(-(hours + 1))
  return used.slice(1).map((next, i) => {
    const first = turns.find((t) => t.start >= used[i].end)
    const start = first?.start ?? used[i].end
    const inSpan = (z: Zone) => z.start >= start && z.start < next.start
    const drawn = draws.filter(inSpan)
    return {
      ms: ms(next.start - start),
      turns: turns.filter(inSpan).length,
      draws: drawn.length,
      drawMs: ms(drawn.reduce((sum, z) => sum + z.end - z.start, 0)),
    }
  })
}

export const mean = (xs: number[]): number => xs.reduce((a, b) => a + b, 0) / xs.length

export const median = (xs: number[]): number => {
  const s = xs.toSorted((a, b) => a - b)
  const mid = s.length >> 1
  return s.length % 2 ? s[mid] : (s[mid - 1] + s[mid]) / 2
}

export type Paired = { baseMedian: number; headMedian: number; changePct: number; faster: string }

/// Median per ref and the median of per-round head/base changes (negative is faster).
export const paired = (base: number[], head: number[]): Paired => {
  const changes = base.map((b, i) => (head[i] - b) / b * 100)
  return {
    baseMedian: median(base),
    headMedian: median(head),
    changePct: median(changes),
    faster: `${changes.filter((c) => c < 0).length}/${changes.length}`,
  }
}
