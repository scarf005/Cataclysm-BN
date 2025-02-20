import type { JSX } from "preact"

declare global {
  type Page = (data: Lume.Data, helpers: Lume.Helpers) => JSX.Element
}
