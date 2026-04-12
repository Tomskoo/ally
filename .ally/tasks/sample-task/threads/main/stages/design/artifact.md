# Design: Authentication Architecture

## Overview

The auth module follows a layered architecture with clear separation between credential storage, token management, and HTTP integration.

## Component Diagram

```
AuthManager
├── CredentialStore    (OS keychain abstraction)
├── TokenRefresher     (background refresh logic)
└── AuthInterceptor    (HTTP middleware)
```

## Key Types

```cpp
struct AuthToken {
    std::string access_token;
    std::string refresh_token;
    std::chrono::system_clock::time_point expires_at;

    bool is_expired() const {
        return std::chrono::system_clock::now() >= expires_at;
    }
};

class CredentialStore {
 public:
    virtual auto get(const std::string& key) -> std::optional<std::string> = 0;
    virtual void set(const std::string& key, const std::string& value) = 0;
    virtual void remove(const std::string& key) = 0;
    virtual ~CredentialStore() = default;
};
```

## Sequence: Login Flow

1. User runs `ally auth login`
2. `AuthManager::login()` prompts for API key
3. Key is validated against the server
4. On success: `CredentialStore::set("api_key", key)`
5. `TokenRefresher` schedules background refresh

## Error Handling

| Error | Recovery |
|-------|----------|
| Invalid API key | Prompt user to re-enter |
| Network timeout | Use cached token if valid |
| Keychain locked | Fall back to file-based store |
| Token expired + no refresh | Force re-login |
