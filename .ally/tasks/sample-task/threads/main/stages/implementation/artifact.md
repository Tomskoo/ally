# Implementation: Auth Module

## Files Created

- `src/auth/AuthManager.hpp` -- Main auth facade
- `src/auth/AuthManager.cpp` -- Implementation
- `src/auth/CredentialStore.hpp` -- Abstract interface
- `src/auth/KeychainStore.cpp` -- macOS Keychain backend
- `src/auth/FileStore.cpp` -- Fallback file-based storage

## Key Implementation Details

### Token Refresh Logic

```cpp
void TokenRefresher::Start(AuthToken& token) {
    refresh_thread_ = std::thread([this, &token] {
        while (!stop_.load()) {
            auto remaining = token.expires_at -
                             std::chrono::system_clock::now();
            if (remaining < std::chrono::minutes(5)) {
                auto refreshed = client_.refresh(token.refresh_token);
                if (refreshed.has_value()) {
                    token = *refreshed;
                    store_.set("token", serialize(token));
                }
            }
            std::this_thread::sleep_for(std::chrono::seconds(30));
        }
    });
}
```

### HTTP Interceptor

```cpp
auto AuthInterceptor::intercept(Request& req) -> Response {
    auto token = manager_.current_token();
    if (token.has_value()) {
        req.headers["Authorization"] = "Bearer " + token->access_token;
    }
    return next_.handle(req);
}
```

## Changes from Design

```diff
- class CredentialStore {
+ class CredentialStore : public std::enable_shared_from_this<CredentialStore> {
  public:
-   virtual auto get(const std::string& key) -> std::optional<std::string> = 0;
+   virtual auto get(std::string_view key) -> std::optional<std::string> = 0;
-   virtual void set(const std::string& key, const std::string& value) = 0;
+   virtual void set(std::string_view key, std::string_view value) = 0;
    virtual void remove(const std::string& key) = 0;
    virtual ~CredentialStore() = default;
  };
```

## Test Results

- 12 unit tests passing
- Integration test with mock keychain: passing
- Manual test on macOS 14.2: passing
