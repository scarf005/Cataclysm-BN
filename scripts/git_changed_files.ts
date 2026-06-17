#!/usr/bin/env -S deno run --allow-run=git --allow-env=PATH

/**
 * @module
 *
 * Lists pull request file changes using the local git checkout.
 */

import { CommandBuilder } from "@david/dax"

export type PullFileStatus = "added" | "copied" | "modified" | "removed" | "renamed" | "changed"

export type PullFile = {
  filename: string
  previous_filename?: string
  status: PullFileStatus
}

export type ChangedFilesOptions = {
  base?: string
  head?: string
}

const pathEnv = () => ({ PATH: Deno.env.get("PATH") ?? "" })

const git = (args: string[]): Promise<string> =>
  new CommandBuilder().command(["git", ...args]).clearEnv().env(pathEnv()).text()

const parseGitStatus = (status: string): PullFileStatus => {
  switch (status[0]) {
    case "A":
      return "added"
    case "C":
      return "copied"
    case "D":
      return "removed"
    case "M":
      return "modified"
    case "R":
      return "renamed"
    default:
      return "changed"
  }
}

export const parseGitNameStatus = (output: string): PullFile[] =>
  output.trim().split("\n")
    .filter(Boolean)
    .map((line) => {
      const [status, firstPath, secondPath] = line.split("\t")
      if ((status.startsWith("R") || status.startsWith("C")) && secondPath !== undefined) {
        return {
          filename: secondPath,
          previous_filename: firstPath,
          status: parseGitStatus(status),
        }
      }
      return { filename: firstPath, status: parseGitStatus(status) }
    })

export const changedFilesFromGit = async (
  { base = "origin/main", head = "HEAD" }: ChangedFilesOptions = {},
): Promise<PullFile[]> => {
  const output = await git([
    "diff",
    "--name-status",
    "--find-renames",
    "--find-copies",
    `${base}...${head}`,
  ])
  return parseGitNameStatus(output)
}
