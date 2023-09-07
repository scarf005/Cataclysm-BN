// @ts-check
import { defineConfig } from "astro/config"
import starlight from "@astrojs/starlight"

const github = "https://github.com/cataclysmbnteam/Cataclysm-BN"

// https://astro.build/config
export default defineConfig({
//   site: 'https://scarf005.github.io',
//   base: '/Cataclysm-BN/',
  integrations: [
    starlight({
      title: "Cataclysm: Bright Nights",
      defaultLocale: "en",
      locales: {
        en: { label: "English" },
        ko: { label: "한국어", lang: "ko-KR" },
      },
      logo: { src: "./src/assets/icon-round.svg" },
      social: { github, discord: "https://discord.gg/XW7XhXuZ89" },
      /* https://starlight.astro.build/guides/css-and-tailwind/#color-theme-editor */
      customCss: ["./src/styles/theme.css"],
      editLink: { baseUrl: `${github}/edit/upload` },
      lastUpdated: true,
      sidebar: [
        {
          label: "Tutorial",
          autogenerate: { directory: "tutorial" },
        },
        {
          label: "Guides",
          autogenerate: { directory: "guides" },
        },
        {
          label: "Reference",
          autogenerate: { directory: "reference" },
        },
        {
          label: "Explanation",
          autogenerate: { directory: "explanation" },
        },
      ],
    }),
  ],
})
