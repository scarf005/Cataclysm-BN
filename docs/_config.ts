import lume from "lume/mod.ts"
import jsx_preact from "lume/plugins/jsx_preact.ts"
import code_highlight from "lume/plugins/code_highlight.ts"
import mdx from "lume/plugins/mdx.ts"
import minify_html from "lume/plugins/minify_html.ts"
import on_demand from "lume/plugins/on_demand.ts"
import relative_urls from "lume/plugins/relative_urls.ts"
import resolve_urls from "lume/plugins/resolve_urls.ts"
import source_maps from "lume/plugins/source_maps.ts"
import multilanguage from "lume/plugins/multilanguage.ts"

const site = lume()

site
  .data("layout", "_includes/layouts/main.tsx")
  .data("lang", "en")
  .ignore("README.md", "LICENSE")

site
  .use(jsx_preact())
  .use(code_highlight())
  .use(mdx())
  .use(minify_html())
  .use(on_demand())
  .use(relative_urls())
  .use(resolve_urls())
  .use(source_maps())
  .use(multilanguage({ languages: ["en"] }))

export default site
