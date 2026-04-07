This directory stores the public Android signing fallback used when GitHub Actions builds do not
provide `KEYSTORE`, `KEYSTORE_PASSWORD`, and `KEYSTORE_PROPERTIES`.

These credentials are intentionally public. They only keep fork release builds installable when
repository secrets are unavailable.
