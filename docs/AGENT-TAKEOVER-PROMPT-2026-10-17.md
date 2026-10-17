# 给全新开发 Agent 的可复制接手提示词 · 2026-10-17

> 请把下面整段发给全新 Agent。这里的日期和 SHA 是**已验证历史基线**，接手时必须重新查询最新状态，不能只信旧交接文档。

---

你是《大富翁4》重构项目的下一任开发 Agent。**请立刻接手现有 MOD 开发，不要重新创建一个项目或重做已完成的功能。** 用户的任务是长期继续修复游戏机制、Android 表现、动画先后顺序及高额金钱逻辑，并希望每轮都留下测试证据、完整 Git 历史与交接记录。

**项目与权限**：
- 游戏代码：`https://github.com/lly8666/richman4-remastered/tree/mod`，**只准在 `mod` 代码分支提交 MOD 变更**，绝对不许把 MOD 规则合并入或修改原版 `main`。
- 独立 Android MOD 工作流：`https://github.com/lly8666/qairt-sdk-archive/blob/main/.github/workflows/build-richman4-android-mod.yml`。该工作流在**构建仓库**的 `main`，为 MOD 打包时只调整该工作流的 `RICH4_UPSTREAM_SHA` 指向已确认的最新 MOD SHA。不要调用 `build-richman4-mod-android.yml` 旧流程，也不要改原版 `build-richman4-android.yml`。
- APK 包名 `io.divby0.rich4.mod` 与 vanilla 分开。固定签名的公开证书 SHA-256 `36778ecf6ef797072560d5cf75777ff508d67a30aa06e15b3e5fabb74e65577d`；GitHub Actions 私钥仅通过两个已由用户配置的加密 Secrets `RICH4_KEYSTORE_B64` 和 `RICH4_KEYSTORE_PASSWORD` 使用。**严禁读取后输出、日志泄露、上传、生成公开下载链接或提交私钥/密码/Base64 到 Git**。
- 已知历史代码 HEAD `70efe575fd9032b4ee440924cda9b1ed89d96740`；原版 `main` 历史 SHA `f87e8dcaaf6a3fc14c9489829162cf7d0bfeaf7c`。最新一次核心 CI `38997529462` success，固定签名 Android APK 构建 `38997573631` success，Artifact `12087352928` 名 `Richman4-MOD-Android-arm64-test-signed-11`，`versionCode=11`，会在 `2026-10-20T03:12:16Z` 过期。**这些都可能过时；先查询最新。**

**接手的第一批动作**：
1. 先完整读取根目录 `AGENTS.md`、`docs/AGENT-HANDOFF-2026-10-16.md`、`docs/mod-README.md`、`docs/history/COMPLETE-DEVELOPMENT-LOG-2026-10-16.md`、`docs/history/MOD-DEVELOPMENT-SESSION-2026-10-17.md`、`docs/audit/MOD-EVENT-ORDER-2026-10-17.md`、`docs/ANDROID-MOD-SIGNING.md`。不要只读前几行或跳过旧决策和 CI 失败记录。
2. 用 GitHub 接口或真实 Git 命令核对最新 `mod` HEAD、`main` HEAD、最近提交、CI 每个 job、APK 工作流绑定 SHA、正确工作流、最近 APK Artifact/签名证书与 `versionCode`。任何旧文档的旧数字不能覆盖最新 GitHub 事实。
3. 熟悉核心系统 `src/game.cpp`、`src/stocks.cpp`、`src/trading.cpp`、`src/events.cpp`、`src/cards.cpp`、`src/services.cpp`、`tools/native_game.cpp`、`tools/screens/menus.cpp`、`tools/screens/finance.cpp`、`include/rich4/mod_money_format.hpp`、`include/rich4/mod_save.hpp`，对应的 `tests/*`。
4. 不要修改已经实现的 MOD 基本规则：股票报价最高 999999；流通量×10000；单股最大持有 10 亿；道具各 999；卡片各 999 堆叠（最多 15 种卡）并显示数量；起始资金 3000 万和 9 亿（UI `3000萬` 与 `9億`），默认 20 万；物价指数固定 1；资金与企业结算 signed64；ECON-4 附档；已接受的股东赠卡规则。
5. 新需求修改前先追踪原版 EXE 对应流程和 `docs/ui/` 研究，根据 **引擎状态计算/动画视觉呈现** 两套顺序区分问题。历史已修彩票开奖、破产、月底通知先出结果，以及卡片 popup 在事件被消费前提前出现；`EffectEvent.after_events`、`GameSession::run_rule` 的暂停重放/随机状态必须保真。
6. 所有高额金钱和存档相关变更必须覆盖到账、扣款、贷款、股票持仓、AI、事件、屏幕显示、原 DAT 兼容预览和 `.DAT.mod` 侧车。请加 `INT32_MAX` 以上和 `INT64_MAX` 附近的原子拒绝、保存重载测试。不能因“看起来是负资产”就无证据地修改贷款规则。
7. 源码变更后跑 `cmake -S . -B build/ci -DCMAKE_BUILD_TYPE=Release -DRICH4_BUILD_VIEWER=OFF -DRICH4_BUILD_GAME=OFF`、`cmake --build build/ci --parallel`、`ctest --test-dir build/ci --output-on-failure`。Android UI 改动必须另运行 Android MOD 工作流验证 SDL3/NDK/Gradle 编译与签名；**36/36 headless core CI 不等于 Android 真机成功**。只在确实测试的范围内报告结果。
8. 若要发布新版 APK，先 commit 到 `mod`，再仅更新正确 builder workflow 的上游 SHA，核对签名证书、`versionCode` 是否递增、资源完整性、Artifact 的真实名称/有效期。旧临时签名 APK 与新固定签名 APK 无法直接覆盖。安全提醒用户一并备份 `.DAT`、`.DAT.mod` 和 `.rng`。不得假设已具备跨三文件原子存档。
9. 维护 GitHub 开发日志与交接：列出每次 commit、改了哪些文件/为何修改、失败与修复经过、CI run URL、Android run URL、未验证风险及后续任务。报告时不要将计划、启动工作流误写为已完成。
10. 严格防止资料泄露：Git 仓库不允许出现 .p12、.pfx、密钥 Base64、密码、个人设备数据或版权游戏原始资源。私钥不应由你复制或传递到公开对话。

**当前最值得继续的工作**：让用户在 Android 手机上对比开局 `9億/3000萬` 金色下拉界面是否美观；核对卡片堆叠数量/触屏、一次性超大额银行存取、彩票开奖/破产/月底/多事件动画的真实先后；用户若提供负资产存档，则专门排查贷款 8 亿变成 18 亿的来源；最后补 DAT+MOD+RNG 跨文件保存的事务恢复与自动化测试。这些仍是开放问题，不得宣称“已经全部解决”。

现在开始：**先给出简洁的状态确认与下一步执行计划，然后读取仓库资料，核实最新 HEAD 和 CI。对用户已经明确提出的具体需求直接推进，按进展提交，仅提交到合适分支，并提供可核查的证据。**

---
