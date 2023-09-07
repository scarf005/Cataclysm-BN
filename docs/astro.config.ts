import { type AstroUserConfig } from "astro/config"
import starlight from "@astrojs/starlight"
import starlightLinksValidator from "starlight-links-validator"

const github = "https://github.com/cataclysmbnteam/Cataclysm-BN"
const itemGuide = "https://cbn-guide.mythoscraft.org"
const discord = "https://discord.gg/XW7XhXuZ89"

const docModes = (dir: string) => [
  { label: "Tutorial", autogenerate: { directory: `${dir}/tutorial` } },
  { label: "Guides", autogenerate: { directory: `${dir}/guides` } },
  { label: "Reference", autogenerate: { directory: `${dir}/reference` } },
  { label: "Explanation", autogenerate: { directory: `${dir}/explanation` } },
]

export default {
  site: "https://scarf005.github.io",
  base: "/Cataclysm-BN",
  integrations: [
    starlightLinksValidator(),
    starlight({
      title: "Cataclysm: Bright Nights",
      defaultLocale: "en",
      locales: {
        en: { label: "English" },
        ko: { label: "한국어", lang: "ko-KR" },
      },
      logo: { src: "./src/assets/icon-round.svg" },
      social: { github, discord },
      /* https://starlight.astro.build/guides/css-and-tailwind/#color-theme-editor */
      customCss: ["./src/styles/theme.css"],
      editLink: { baseUrl: `${github}/edit/upload` },
      lastUpdated: true,
      navbar: {
        json: {
          label: "JSON Mods",
          link: "/json/guides/modding",
          translations: { "ko-KR": "JSON 모딩" },
          items: docModes("json"),
        },
        lua: {
          label: "Lua Mods",
          link: "/lua/guides/modding",
          translations: { "ko-KR": "Lua 모딩" },
          items: docModes("lua"),
        },
        dev: {
          label: "Engine",
          link: "/dev/guides/compiling/cmake",
          translations: { "ko-KR": "게임 엔진" },
          items: docModes("dev"),
        },
        translation: {
          label: "Translation",
          link: "/translation/tutorial/transifex",
          translations: { "ko-KR": "번역" },
          items: docModes("translation"),
        },
        contributing: {
          label: "Contributing",
          link: "/contributing/changelog_guidelines",
          translations: { "ko-KR": "기여하기" },
          items: [
            { label: "Changelog Guidelines", link: "/contributing/changelog_guidelines" },
            { label: "Code style", link: "/dev/explanation/code_style" },
            { label: "JSON Style", link: "/json/explanation/json_style" },
          ],
        },
      },
    }),
  ],
} satisfies AstroUserConfig
