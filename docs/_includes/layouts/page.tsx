import { Sidebar } from "../../components/Sidebar.tsx"

export const layout = "_includes/layouts/main.tsx"

export default (({ children }) => (
  <div class="container margin-vert--lg">
    <div class="row">
      <aside class="col col--3">
        <Sidebar />
      </aside>
      <main class="col col--9">
        {children}
      </main>
    </div>
  </div>
)) satisfies Page
