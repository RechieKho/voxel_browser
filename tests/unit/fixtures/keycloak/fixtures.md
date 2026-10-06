# Keycloak golden fixtures

Generated, do not edit by hand:

    cd tests/e2e && python3 -m vbtest.mock_keycloak --dump-fixtures ../unit/fixtures/keycloak

by `vbtest/fixture_dump.py` from the `MockKeycloak` emulator with a fixed clock (`now = 1800000000`), seeded
ids and the committed TEST-ONLY keys in `tests/e2e/fixtures/`. Re-running it changes nothing unless the
emulator changed. The real-Keycloak workflow (`auth_keycloak.yml`, K5.3) regenerates them from a real
Keycloak into a temp directory and fails with a diff when they differ in shape, which is what keeps
these honest. Tokens expire, so the unit tests pass `now` from `tokens.json` to `verify_id_token`.

| File | What |
|---|---|
| `discovery.json` | `/.well-known/openid-configuration` of realm `e2e` (issuer `https://keycloak.example.test/realms/e2e`) |
| `jwks_rs256.json` | the realm's keys: one RS256 signing key and the RSA-OAEP `enc` key the engine must skip |
| `jwks_es256.json` | the same for an ES256 realm |
| `jwks_rotated.json` | after a key rotation: old and new signing key |
| `jwks_retired.json` | after the old key was retired: only the new one |
| `token_response.json`, `refresh_response.json` | token endpoint answers (code flow, refresh grant) |
| `errors/*.json` | `{status, body}` of every error the client maps (K1.5) |
| `tokens.json` | ID tokens with known claims, signed with the keys above, each with a note |

- kids: before rotation `e2e-key-1`, after `e2e-key-2`
