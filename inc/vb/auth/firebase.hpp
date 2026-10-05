#pragma once

#include <string>

#include "vb/auth/http.hpp"
#include "vb/auth/signin.hpp"

// Firebase Auth client side (auth.md §4/§7): Identity Toolkit REST. The
// password goes from the player's machine straight to Google, never to the
// game server.

namespace vb::auth {

// POST accounts:signInWithPassword; the returned idToken is a Firebase ID token.
SignInResult firebase_password_sign_in(HttpFetcher &http, const std::string &api_key,
		const std::string &email, const std::string &password);

// POST securetoken.googleapis.com/v1/token (grant_type=refresh_token).
SignInResult firebase_refresh(HttpFetcher &http, const std::string &api_key,
		const std::string &refresh_token);

// Maps an Identity Toolkit error message ("INVALID_PASSWORD", ...) to text safe
// to show the player.
std::string firebase_error_text(const std::string &message);

} // namespace vb::auth
