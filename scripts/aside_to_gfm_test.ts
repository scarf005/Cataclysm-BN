import { assertEquals } from "@std/assert"

import { asideToGfm } from "./aside_to_gfm.ts"

const given = `:::caution

This guide is quite old and requires manual dependency management.

For modern alternative, see [CMake Visual Studio build with vcpkg](./vs_cmake.md)

:::

some text

:::note

- \`.md\` in \`CONTRIBUTING.md\` stands for markdown files
- \`.mdx\` in \`docs.mdx\` stands for [MarkDown eXtended](https://mdxjs.com)
  - it's a superset of markdown with javascript and [jsx component][jsx] support
  - they are a bit more complicated but allows to use interactive components

[jsx]: https://www.typescriptlang.org/docs/handbook/jsx.html

:::
`

const expected = `> [!CAUTION]
> This guide is quite old and requires manual dependency management.
>
> For modern alternative, see [CMake Visual Studio build with vcpkg](./vs_cmake.md)

some text

> [!NOTE]
> - \`.md\` in \`CONTRIBUTING.md\` stands for markdown files
> - \`.mdx\` in \`docs.mdx\` stands for [MarkDown eXtended](https://mdxjs.com)
>   - it's a superset of markdown with javascript and [jsx component][jsx] support
>   - they are a bit more complicated but allows to use interactive components
>
> [jsx]: https://www.typescriptlang.org/docs/handbook/jsx.html
`

Deno.test("asideToGfm() handles cautions", () => assertEquals(asideToGfm(given), expected))
