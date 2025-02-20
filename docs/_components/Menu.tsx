import type { NavData } from "lume/plugins/nav.ts"

const Menu = ({ navData }: { navData?: NavData }) => {
  const children = navData?.children
  const hasChildren = !(children?.length === 0)
  return (
    <li class="menu__list-item">
      <div class="menu__list-item-collapsible">
        <a class="menu__link">{navData?.data.basename}</a>
        {!hasChildren && <button type="button" class="clean-btn menu__caret"></button>}
      </div>
      {hasChildren && (
        <ul class="menu__list">
          {navData?.children?.map((item) => <Menu key={item.data.id} navData={item} />)}
        </ul>
      )}
    </li>
  )
}

export default (({ nav }: Pick<Lume.Data, "nav">) => {
  return (
    <nav class="menu thin-scrollbar" aria-label="Docs Sidebar">
      <Menu navData={nav.menu()} />
    </nav>
  )
}) satisfies Page
