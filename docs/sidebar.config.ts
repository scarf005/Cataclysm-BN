export const sidebar = {
  docs: [
    {
      type: "category",
      label: "Meta",
      items: [
        "meta/how-to-edit-docs",
      ],
    },
    {
      type: "category",
      label: "Development",
      items: [
        "dev/getting-started",
        "dev/building",
        "dev/coding-style",
      ],
    },
  ],
}

export type SidebarItem = {
  type: "category" | "doc"
  label: string
  items?: string[]
}
