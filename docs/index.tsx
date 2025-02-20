export const layout = "_includes/layouts/main.tsx"

export default (() => (
  <>
    <div class="hero hero--primary">
      <div class="container">
        <h1 class="hero__title">Cataclysm: Bright Nights</h1>
        <p class="hero__subtitle">
          Developer documentation
        </p>
        <div class="padding-vert--md">
          <a
            class="button button--secondary button--lg"
            href="/docs/getting-started"
          >
            Start Here
          </a>
        </div>
      </div>
    </div>
  </>
)) satisfies Page
