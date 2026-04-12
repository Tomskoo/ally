# Intent: User Authentication Module

## Goal
Build a secure, token-based authentication system for the ally CLI that supports:

- API key authentication (primary)
- OAuth2 device flow (stretch goal)
- Session persistence across terminal restarts

## Success Criteria

1. Users can authenticate with `ally auth login`
2. Tokens are stored securely in the OS keychain
3. Expired tokens are refreshed automatically
4. All API calls include proper auth headers

## Constraints

- Must work offline (cached credentials)
- No browser dependency for the primary flow
- Token refresh must be transparent to the user

## Open Questions

- Should we support multiple simultaneous auth profiles?
- What is the token TTL policy?
