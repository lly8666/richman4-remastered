# Android MOD 固定签名密钥的安全交付

项目源码在公开 `mod` 分支；私人签名密钥**不得**以任何形式（包括加密 .p12 和
base64 字符串）提交到公开 Git 历史。工作流仓库的安全公开文件：
[`signing/richman4-mod-signing-public.pem`](https://github.com/lly8666/qairt-sdk-archive/blob/main/signing/richman4-mod-signing-public.pem)。

本轮已生成全新的 PKCS12（别名 `rich4-local-test`）、独立强密码与公开证书，签名
SHA-256 指纹 `36778ecf6ef797072560d5cf75777ff508d67a30aa06e15b3e5fabb74e65577d`。
**之前临时生成的 APK 私钥已丢失；这个新证书只能保证从启用它的第一版往后的覆盖升级，
不能覆盖安装先前签名不同的 MOD APK。**

GitHub Actions 只有通过 `qairt-sdk-archive` 的仓库加密 Secrets 才使用此新密钥：
`RICH4_KEYSTORE_B64` 设置为整个 .p12 文件的 base64 文本，
`RICH4_KEYSTORE_PASSWORD` 设置为对应的强密码。见构建仓库
[`signing/README.md`](https://github.com/lly8666/qairt-sdk-archive/blob/main/signing/README.md)。
两项 Secret 目前**无法由此 GitHub 连接工具写入**，必须由仓库所有者在 GitHub Settings
里设置。若未配置，Android MOD 工作流应拒绝输出不稳定的临时签名包。

项目 `.gitignore` 已屏蔽 .p12、.pfx、.jks、.keystore 和含签名密码的文本，避免
误将密钥纳入 Git；仓库只允许保存公开证书与流程文档。正式配置成功后务必妥善双份备份
私钥与密码；不要用易忘的生成密钥替换已固定的旧密钥。

升级到新证书前先备份旧应用存档 `.DAT`、`.DAT.mod`、`.rng`。


## 2026-10-17 已完成配置并成功固定签名（后续状态覆盖旧段落）

用户已在 `qairt-sdk-archive` 设置两项加密 Secrets，实际 [成功工作流 #38997573631](https://github.com/lly8666/qairt-sdk-archive/actions/runs/38997573631) 的日志显示 `Using retained private key from GitHub Actions encrypted Secrets.`，签名证书 SHA-256 与上面的固定值严格相符，APK `versionCode=11`，artifact `Richman4-MOD-Android-arm64-test-signed-11`。**此日志证明当次 Secrets 工作，不证明可查询或显示其秘密内容**。请勿再次请求用户分享密码或将 .p12 发到公开渠道。采用同一固定证书、同一包名、后继 `versionCode` 才能从这一版继续覆盖升级。旧临时签名 APK 仍不相容。
