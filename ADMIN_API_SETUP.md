# Admin API Setup Guide

This guide explains how to configure and use the admin authentication API for NextSearch.

## Prerequisites

- OpenSSL library installed on your system
- Access to the `.env` configuration file

## Configuration

1. **Copy the example environment file:**
   ```bash
   cp .env.example .env
   ```

2. **Configure admin credentials in `.env`:**
   ```bash
   # Admin password (use a strong password)
   ADMIN_PASSWORD=your-secure-admin-password-here
   
   # JWT signing secret (generate a strong random key)
   # You can generate one with: openssl rand -base64 32
   JWT_SECRET=your-jwt-signing-secret-here
   
   # JWT expiration in seconds (default: 3600 = 1 hour)
   JWT_EXPIRATION=3600
   ```

3. **Generate a secure JWT secret:**
   ```bash
   openssl rand -base64 32
   ```
   Copy the output and use it as your `JWT_SECRET` value.

## Building

The admin API requires OpenSSL. Make sure it's installed:

**Windows (using vcpkg):**
```bash
vcpkg install openssl:x64-windows
```

**Linux/macOS:**
```bash
# Ubuntu/Debian
sudo apt-get install libssl-dev

# macOS
brew install openssl
```

Then build the project:
```bash
cmake -B build
cmake --build build
```

## API Endpoints

### 1. Admin Login
**POST** `/api/admin/login`

Request:
```json
{
  "password": "your-admin-password"
}
```

Response (200 OK):
```json
{
  "token": "eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9...",
  "expires_in": 3600
}
```

Error (401 Unauthorized):
```json
{
  "error": "Invalid admin password"
}
```

### 2. Verify Token
**GET** `/api/admin/verify`

Headers:
```
Authorization: Bearer <your-jwt-token>
```

Response (200 OK):
```json
{
  "valid": true,
  "expires_at": 1736950800000
}
```

Error (401 Unauthorized):
```json
{
  "valid": false
}
```

### 3. Logout
**POST** `/api/admin/logout`

Headers:
```
Authorization: Bearer <your-jwt-token>
```

Response (200 OK):
```json
{
  "message": "Logged out successfully"
}
```

## Protected Endpoints

The following endpoints now require admin authentication:

- **POST** `/api/add_document` - Add new documents to the index
- **GET** `/api/ai_overview` - Generate AI overviews
- **GET** `/api/ai_summary` - Generate AI summaries
- **GET** `/api/stats` - Get system statistics

To access these endpoints, include the JWT token in the Authorization header:

```bash
curl http://localhost:8080/api/stats \
  -H "Authorization: Bearer <your-jwt-token>"
```

## Statistics Endpoint

**GET** `/api/stats`

Returns information about the search index:

```json
{
  "total_documents": 150000,
  "total_segments": 5,
  "index_size_bytes": 5368709120,
  "last_indexed": "2026-01-15T10:30:00Z",
  "search_stats": {
    "total_searches": 0,
    "avg_latency_ms": 0.0,
    "cache_hit_rate": 0.0
  }
}
```

## Testing

### 1. Login and get token:
```bash
curl -X POST http://localhost:8080/api/admin/login \
  -H "Content-Type: application/json" \
  -d '{"password": "your-admin-password"}'
```

### 2. Verify token:
```bash
curl http://localhost:8080/api/admin/verify \
  -H "Authorization: Bearer eyJ..."
```

### 3. Access protected endpoint:
```bash
curl http://localhost:8080/api/stats \
  -H "Authorization: Bearer eyJ..."
```

### 4. Logout:
```bash
curl -X POST http://localhost:8080/api/admin/logout \
  -H "Authorization: Bearer eyJ..."
```

## Security Best Practices

1. **Use strong passwords**: Generate a random password with at least 20 characters
2. **Keep secrets secure**: Never commit `.env` file to version control
3. **Use HTTPS in production**: Always use HTTPS to protect tokens in transit
4. **Rotate credentials regularly**: Change admin password and JWT secret periodically
5. **Monitor access logs**: Review authentication attempts in server logs

## Token Details

- **Algorithm**: HS256 (HMAC with SHA-256)
- **Expiration**: 1 hour (configurable via `JWT_EXPIRATION`)
- **Claims**: 
  - `role`: "admin"
  - `iat`: Issued at timestamp
  - `exp`: Expiration timestamp

## Troubleshooting

### "Admin authentication not configured"
Make sure `ADMIN_PASSWORD` and `JWT_SECRET` are set in your `.env` file.

### "Unauthorized" errors
- Check that your token hasn't expired (tokens expire after 1 hour by default)
- Verify the Authorization header format: `Authorization: Bearer <token>`
- Make sure you're using the correct admin password

### Build errors about OpenSSL
Install OpenSSL development libraries for your platform (see Building section above).

## Frontend Integration

The frontend already supports admin authentication. It will:
- Automatically include the token in protected requests
- Check token expiry before requests
- Redirect to login when token expires
- Clear token on logout

No additional frontend changes are needed.
