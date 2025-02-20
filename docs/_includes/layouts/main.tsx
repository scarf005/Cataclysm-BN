import Menu from "../../_components/Menu.tsx"

export default (({ title, children, lang, nav }) => (
  <html lang={lang}>
    <link
      rel="stylesheet"
      href="https://cdn.jsdelivr.net/npm/infima@0.2.0-alpha.45/dist/css/default/default.min.css"
    />
    <head>
      <title>{title}</title>
    </head>
    <body>
      <div class="row">
        <aside class="col col--3">
          <Menu nav={nav} />
        </aside>
        <main class="col col--9">
          {children}
        </main>
      </div>
    </body>
  </html>
)) satisfies Page

const navBar = () => (
  <nav class="navbar">
    <div class="navbar__inner">
      <div class="navbar__items">
        <a class="navbar__brand">Infima</a>
        <a class="navbar__item navbar__link" href="#url">Docs</a>
        <a class="navbar__item navbar__link" href="#url">Tutorial</a>
        <div class="navbar__item dropdown dropdown--hoverable">
          <a class="navbar__link" href="#url">v2.0</a>
          <ul class="dropdown__menu">
            <li>
              <a class="dropdown__link" href="#url">v1.8.0</a>
            </li>
            <li>
              <a class="dropdown__link" href="#url">v1.7.0</a>
            </li>
            <li>
              <a class="dropdown__link" href="#url">v1.6.0</a>
            </li>
            <li>
              <a class="dropdown__link" href="#url">All Versions</a>
            </li>
          </ul>
        </div>
      </div>
      <div class="navbar__items navbar__items--right">
        <form>
          <div class="navbar__search">
            <input class="navbar__search-input" placeholder="Search" />
          </div>
        </form>
      </div>
    </div>
  </nav>
)
