"""Compatibility shim: the old mock OpenID Connect provider is now `MockKeycloak`.

`test_auth.py` (the smoke suite) still imports `MockIdp` and `auth_lua` from here. New tests
use `vbtest.mock_keycloak.MockKeycloak` and the `idp` fixture from conftest.py.
"""
from .idp import auth_lua  # noqa: F401  (re-exported)
from .mock_keycloak import MockKeycloak


class MockIdp(MockKeycloak):
    """A MockKeycloak with the old defaults: a flat realm, no users, `next_user` signs in."""
