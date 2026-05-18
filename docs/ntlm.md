# NTLM / 用户名密码认证（Sunshine-ntlm 扩展）

本分支在上游 Sunshine 的基础上，为 **流相关 HTTP 接口**（`applist` / `launch` / `resume` / `cancel` / `appasset`）增加了一套基于「用户名 + 密码」的认证模式，用来替代或补充原有的 SSL 客户端证书 pairing 机制。

> 注意：这套机制只影响 nvhttp 服务（默认端口 47984/47989 等），**不会**改变 Web UI（confighttp，端口 47990）的登录流程。Web UI 仍然使用 `sunshine_state.json` 里的 username/password/salt（哈希存储）。

---

## 1. 三种认证模式

启动时由 `enable_user_pass_auth()` 根据配置自动选择，互斥选其一：

| 模式 | 触发条件 | 行为 |
|------|----------|------|
| **A. 上游标准模式（默认）** | 既未配置 `stream_username/stream_password`，也未配置 `apiserver` | 走原版 SSL 客户端证书 pairing 验证。客户端必须先 PIN 配对，连接才会被接受 |
| **B. 本地凭据模式** | 配置了 `stream_username` **且** `stream_password`（不论是否同时配了 `apiserver`） | 关闭 SSL 客户端证书校验，每个被保护的请求必须携带 `?username=&password=`，与本地配置**明文**比对 |
| **C. 远程 NTLM 模式** | 仅配置了 `apiserver`（未配置本地凭据） | 关闭 SSL 客户端证书校验，每个被保护的请求把 `{uuid, user}` POST 给远程 `apiserver`，由远程返回的密码与客户端密码比对 |

**优先级**：B > C。同时配置时只走 B，并打印日志 `Apiserver also configured but ignored`。

⚠️ 模式 B/C 启用后 **SSL 客户端证书验证会被绕过**。这意味着任何能连到端口的客户端都能尝试发起请求，**安全性完全依赖于用户名/密码是否泄露**。请确保：

- 服务只对受信网络/VPN 开放；
- 不要在不可信网络中使用空密码或弱密码；
- 远程 `apiserver` 通过 HTTPS 暴露，且后端做好访问控制。

---

## 2. 配置项

写入 `sunshine.conf`（位置因平台而异，详见上游 `getting_started.md`）：

```ini
# ------- 模式 B：本地用户名密码 -------
stream_username = alice
stream_password = s3cret-plaintext

# ------- 模式 C：远程 NTLM 校验 -------
apiserver = auth.example.com           # 仅域名/IP[:port]，不要带协议
                                       # 实际请求地址：
                                       # https://<apiserver>/api/custom/public/ntlmcheckuuid

# ------- 可选：覆盖主板 UUID -------
# 默认会从主板读（Linux: /sys/class/dmi/id/board_serial；Windows: WMI Win32_BaseBoard.SerialNumber；
# macOS: 固定为 00000000-0000-0000-0000-000000000000）。
# 如需固定上报给 apiserver 的设备身份，可手动指定：
uuid = 12345678-1234-1234-1234-1234567890ab
```

> 历史提醒：早期版本里这两个键叫 `username` / `password`。由于和 Web UI 凭据字段同名导致 C++ 重复声明（CI 编译失败），且语义不同（Web UI 存哈希，流认证用明文），现已改名为 `stream_username` / `stream_password`。**升级时务必同步改 key 名**，否则会回退到模式 A 或 C。

---

## 3. 客户端如何发请求

模式 B/C 启用后，被保护的端点要求 query string 中带凭据：

```
GET /applist?uniqueid=<id>&uuid=<client-uuid>&username=alice&password=s3cret-plaintext
```

未携带或不匹配会返回：

```xml
<root status_code="401" status_message="Authentication required"/>
```

被强制要求认证的端点：

- `GET /applist`
- `GET /launch`
- `GET /resume`
- `GET /cancel`
- `GET /appasset`

其它端点（如 `serverinfo`、`pair`）仍走上游逻辑。

---

## 4. 远程 NTLM（模式 C）协议

### 4.1 请求

- 方法：`POST`
- URL：`https://<apiserver>/api/custom/public/ntlmcheckuuid`
- 头：`Content-Type: application/json`
- TLS：当前实现 **不校验** 服务端证书（`CURLOPT_SSL_VERIFYPEER=0`、`VERIFYHOST=0`），且强制 TLSv1.2，超时 30 秒。
- Body：

  ```json
  {
    "uuid": "<本机 uuid，来自 sunshine.conf 的 uuid 或主板 UUID>",
    "user": "<客户端传来的 username>"
  }
  ```

### 4.2 响应

期望 HTTP 200，body 为：

```json
{
  "success": true,
  "data": {
    "pass": "<该 user/uuid 对应的合法明文密码>"
  }
}
```

Sunshine 端会比对 `data.pass` 与客户端传来的 `password`，相等才放行。

失败响应示例：

```json
{
  "success": false,
  "data": { "message": "user not bound to this uuid" }
}
```

---

## 5. 主板 UUID 来源

由 `util::board_uuid::get_board_uuid()` 提供，缓存一次后复用：

- **Linux**：读取 `/sys/class/dmi/id/board_serial`
- **Windows**：通过 WMI 查询 `Win32_BaseBoard.SerialNumber`
- **macOS**：固定返回 `00000000-0000-0000-0000-000000000000`
- **其它平台 / 读取失败**：`FFFFFFFF-FFFF-FFFF-FFFF-FFFFFFFFFFFF`
- 若 `sunshine.conf` 配了 `uuid`，则优先使用配置值（会经过格式校验）。

UUID 主要用于模式 C 把「设备身份」上报给 apiserver，apiserver 据此查出该设备绑定的合法凭据。

---
