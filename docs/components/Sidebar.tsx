import { sidebar, SidebarItem } from "../sidebar.config.ts"

function SidebarCategory({ item }: { item: SidebarItem }) {
  return (
    <li class="menu__list-item">
      <div class="menu__link menu__link--sublist">{item.label}</div>
      <ul class="menu__list">
        {item.items?.map((route) => (
          <li class="menu__list-item">
            <a class="menu__link" href={`/${route}`}>
              {route.split("/").pop()?.replace(/-/g, " ")}
            </a>
          </li>
        ))}
      </ul>
    </li>
  )
}

export function Sidebar() {
  return (
    <div class="docSidebar">
      <nav class="menu thin-scrollbar">
        <ul class="menu__list">
          {sidebar.docs.map((item) => <SidebarCategory item={item} />)}
        </ul>
      </nav>
    </div>
  )
}
