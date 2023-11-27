import { assertEquals } from "$std/assert/assert_equals.ts"
import { parseCommit } from "./changelog.ts"

Deno.test("parseCommit", () =>
  assertEquals(
    parseCommit("fix(content,mods/foo)!: Update ergonomic grips to work for shotguns (#3974)"),
    {
      type: "fix",
      scopes: ["content", "mods/foo"],
      desc: "Update ergonomic grips to work for shotguns",
      pr: 3974,
    },
  ))
