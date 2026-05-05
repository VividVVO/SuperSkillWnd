# 授权系统说明

## 重要变更

- 当前 C++ 客户端已经改为只接收服务端下发的完整授权文件。
- 客户端不再读取本地 `template.hfy`，也不再在本地生成 `hfy` 授权内容。
- 客户端激活成功后写出 `C:\Windows\hfy` 和 `C:\Windows\time`。

这个目录是一套独立的授权方案，不会改动你当前 `SuperSkillWnd` 的现有业务代码。

现在服务端支持两种存储模式：

- 配了 `MYSQL_DSN`：使用 MySQL
- 没配 `MYSQL_DSN`：默认使用本地 SQLite 文件

## 目录结构

```text
auth_system/
├─ server/                 Go 服务端 + MySQL + 后台管理
└─ client/AuthClient/      Win32 C++ 授权客户端
```

## 已实现内容

- Go 服务端：
  - `POST /api/v1/activate` 按客户端 IP 做网络授权校验
  - `GET /api/v1/ip` 返回服务端观察到的客户端 IP
  - 内置管理后台，支持用户增删改查
  - 支持 MySQL 或本地 SQLite 存储授权数据
  - 管理后台 UI 已升级为 Vue 3 + Element Plus 风格单页面板
  - 添加用户时支持按 `天 / 周 / 月 / 年` 快速生成到期时间
  - 后台默认仅允许内网访问，可通过环境变量放开

- C++ 客户端：
  - 按截图样式做了一个 Win32 小窗口
  - 启动时自动获取当前公网 IP
  - 点击 `激活授权` 后向 Go 服务端发请求
  - 授权成功后把验证数据写到 `C:\Windows\hfy`
  - 同时写出到期时间文件 `C:\Windows\time`
  - 点击 `解除授权` 删除本地授权文件和地址文件
  - 失败提示：`尚未获得授权或已经到期，请联系作者`
  - 成功提示：`激活授权成功`

- `FIXED_TEXT` 兼容：
  - 服务端按严格格式生成
  - 客户端兼容你给出的 `hfy` 加解密逻辑
  - 服务端直接返回完整加密 blob，客户端只负责落盘

## FIXED_TEXT 格式

服务端严格按下面结构拼接：

```text
产品名|添加时间|到期时间|提示语|绑定IP|1|1|1|绑定IP重复|1|绑定QQ|固定尾段
```

例如：

```text
085二区-自定义属性-伤害统计-发面板包|2025/2/23 23:43:29|2135/1/1 10:46:14|本程序仅做学习交流之用，不得用于商业用途！如作他用所承受的法律责任一概与作者无关（下载使用即代表你同意上述观点）|183.141.76.29|1|1|1|183.141.76.29 |1|475862105|VSD...
```

注意：上面这个样例来自你标注为 `095` 的 hfy 样本，最终落盘文件名是 `C:\Windows\hfy`。后台默认版本码 `095` 只用于选择这套产品名/尾段；你标注为 `085` 的 hfy 样本对应 `神木村-角色潜能-自定义属性-发面板包` 和 `XFPM...` 尾段。

## 数据库字段

后台管理使用的核心字段：

- 用户名
- 产品名
- 添加时间
- 到期时间
- 绑定 IP
- 绑定 QQ
- 是否启用
- 备注

## 服务端运行

1. 如果你要用 MySQL，先创建数据库，例如：

```sql
CREATE DATABASE auth_system CHARACTER SET utf8mb4 COLLATE utf8mb4_unicode_ci;
```

2. 复制 [server/.env.example](/g:/code/c++/SuperSkillWnd/auth_system/server/.env.example) 为你自己的环境变量配置。

3. 关键配置：

- `MYSQL_DSN`
- `SQLITE_PATH=./data/auth_system.db`
- `ADMIN_USERNAME`
- `ADMIN_PASSWORD`
- `ADMIN_ALLOW_PUBLIC=false`
- `ADMIN_ALLOWED_CIDRS=127.0.0.0/8,::1/128,10.0.0.0/8,172.16.0.0/12,192.168.0.0/16,fc00::/7`
- `HFY_TEMPLATE_PATH`
- `BLOB_KEY=heifengye111`

4. 存储选择规则：

- `MYSQL_DSN` 有值：走 MySQL
- `MYSQL_DSN` 为空：走 `SQLITE_PATH` 指向的本地 SQLite 文件

5. 启动：

```powershell
cd auth_system\server
go run .\cmd\authserver
```

6. 打开后台：

```text
http://127.0.0.1:8080/admin/licenses
```

默认使用 Basic Auth。

## 客户端运行

1. 复制 [client/AuthClient/auth_client.ini.example](/g:/code/c++/SuperSkillWnd/auth_system/client/AuthClient/auth_client.ini.example) 为 `auth_client.ini`。

2. 根据需要修改：

- `server_url`
- `internal_server_url`
- `output_path=C:\Windows\hfy`
- `timeout_ms=5000`

3. 编译客户端：

```powershell
cd auth_system\client\AuthClient
cmd /c build.bat
```

5. 启动 `AuthClient.exe`，点击 `激活授权`。

## 你给的示例用户

可以直接在后台新增一条记录：

- 添加时间：`2023/12/17 22:01:59`
- 到期时间：`2135/1/1 10:46:14`
- 绑定 IP：`159.75.226.54`
- 绑定 QQ：`475862105`
- 产品名：`085二区-自定义属性-伤害统计-发面板包`
- 提示语：使用默认提示语

## 本地时间与 IP 校验

当前实现包含以下基础校验：

- 服务端按来源 IP 匹配授权
- 客户端校验服务端返回的 `server_time` / `expires_at`
- 本机时间明显早于服务端时间时拒绝激活
- 本机时间超过到期时间时拒绝激活

## 当前限制

- `C:\Windows\hfy` / `C:\Windows\time` 写入通常需要管理员权限
- 重新激活前和清除授权时，客户端会删除 `C:\Windows\time`，并兼容清理误写出的 `C:\Windows\timg`
- 当前后台 UI 使用 Element Plus 浏览器 CDN，离线内网环境下建议后续改成自带本地静态资源
- 我没有帮你实现对抗分析、虚拟化壳、VM 加密、规避逆向或指定加壳工具/版本

## 建议的合规加固方向

如果你只是做正规的商业授权，建议优先做这些：

- 全链路 HTTPS
- 服务端审计日志
- 服务端二次校验和吊销能力
- 客户端代码签名
- 配置文件签名或 HMAC
- 关键字段只在服务端判定，不把规则完全下放到客户端
